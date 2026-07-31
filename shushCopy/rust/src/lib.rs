//! shushCopy Rust port — SSH exec and SCP send/recv via the `ssh2` crate.
//!
//! Key differences from the C implementation:
//!
//! * **Error handling** — `Result<T, ShushError>` everywhere; the compiler
//!   forces callers to handle every failure case.  No out-parameters, no
//!   integer codes to forget to check.
//!
//! * **Memory** — no `malloc`/`free`.  `ShushConn` owns its resources;
//!   `Drop` disconnects cleanly when it goes out of scope, whether by normal
//!   return, early `?`, or a panic.
//!
//! * **Buffers** — `String` and `Vec<u8>` grow to fit; exec output is never
//!   silently truncated.  `scp_recv` buffers the full transfer in memory
//!   before touching disk, so a failed transfer never leaves a partial file.
//!
//! * **Concurrency** — Rust's borrow checker prevents two `&mut ShushConn`
//!   references from existing simultaneously; the concurrent-connection risk
//!   documented in the C implementation is a compile error here.

use ssh2::Session;
use std::fmt;
use std::fs;
use std::io::{self, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::path::Path;
use std::time::Duration;

// ------------------------------------------------------------------ //
// Error type                                                          //
// ------------------------------------------------------------------ //

/// All errors that shushCopy operations can produce.
#[derive(Debug)]
pub enum ShushError {
    /// NULL or invalid argument.
    Param(String),
    /// TCP connect or local file I/O error.
    Socket(io::Error),
    /// SSH key exchange / handshake failed.
    Handshake(ssh2::Error),
    /// Authentication credentials rejected by the server.
    Auth,
    /// SSH channel open or exec failed.
    Channel(ssh2::Error),
    /// SCP channel open failed.
    Scp(ssh2::Error),
    /// Operation exceeded the configured timeout.
    Timeout,
    /// VM lifecycle operation failed (Windows only).
    Vm(String),
}

impl fmt::Display for ShushError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ShushError::Param(msg)   => write!(f, "invalid parameter: {}", msg),
            ShushError::Socket(e)    => write!(f, "TCP / I/O error: {}", e),
            ShushError::Handshake(e) => write!(f, "SSH handshake failed: {}", e),
            ShushError::Auth         => write!(f, "SSH authentication rejected"),
            ShushError::Channel(e)   => write!(f, "SSH channel error: {}", e),
            ShushError::Scp(e)       => write!(f, "SCP error: {}", e),
            ShushError::Timeout      => write!(f, "operation timed out"),
            ShushError::Vm(msg)      => write!(f, "VM lifecycle error: {}", msg),
        }
    }
}

impl std::error::Error for ShushError {}

/// Convert `io::Error` → `ShushError`, detecting socket timeouts automatically.
///
/// This `From` impl is what makes the `?` operator work on any `io::Result`
/// inside functions that return `Result<_, ShushError>`.
impl From<io::Error> for ShushError {
    fn from(e: io::Error) -> Self {
        if e.kind() == io::ErrorKind::TimedOut || e.kind() == io::ErrorKind::WouldBlock {
            ShushError::Timeout
        } else {
            ShushError::Socket(e)
        }
    }
}

/// Returns true when a libssh2 error is a timeout (LIBSSH2_ERROR_TIMEOUT = -43).
fn is_ssh_timeout(e: &ssh2::Error) -> bool {
    e.code() == ssh2::ErrorCode::Session(-43)
}

// ------------------------------------------------------------------ //
// Connection                                                          //
// ------------------------------------------------------------------ //

/// An authenticated SSH connection.
///
/// Dropping `ShushConn` sends a clean SSH disconnect automatically.
pub struct ShushConn {
    session: Session,
}

impl ShushConn {
    /// Connect to `host:port` using password authentication.
    ///
    /// `timeout_ms` is applied to the TCP connect and to all subsequent
    /// blocking SSH operations on this session.  Pass `0` for no timeout.
    pub fn connect(
        host:       &str,
        port:       u16,
        user:       &str,
        password:   &str,
        timeout_ms: u64,
    ) -> Result<Self, ShushError> {
        if host.is_empty() || user.is_empty() {
            return Err(ShushError::Param("host and user must not be empty".into()));
        }
        if port == 0 {
            return Err(ShushError::Param("port 0 is not valid; must be 1-65535".into()));
        }

        let addr = format!("{}:{}", host, port);
        let socket_addr = addr
            .to_socket_addrs()?                  // io::Error → ShushError
            .next()
            .ok_or_else(|| ShushError::Param(format!("could not resolve '{}'", addr)))?;

        let timeout = (timeout_ms > 0).then(|| Duration::from_millis(timeout_ms));

        let tcp = match timeout {
            Some(t) => TcpStream::connect_timeout(&socket_addr, t)?,
            None    => TcpStream::connect(socket_addr)?,
        };

        /* Explicitly restore blocking mode: connect_timeout uses non-blocking
         * connect + select() internally on Windows and may leave the socket
         * non-blocking, which prevents libssh2 from detecting connection resets
         * immediately (it sees WSAEWOULDBLOCK and retries until session timeout). */
        tcp.set_nonblocking(false)?;

        /* Do NOT set OS socket-level timeouts here.  The C implementation only
         * uses libssh2_session_set_timeout (below), not SO_RCVTIMEO/SO_SNDTIMEO.
         * Setting both causes the OS timeout to govern the wait on connection
         * resets, which prevents libssh2 from detecting them immediately and
         * produces TIMEOUT instead of HANDSHAKE for fast-closing peers. */

        let mut session = Session::new().map_err(ShushError::Handshake)?;
        session.set_tcp_stream(tcp);
        session.set_blocking(true);

        /* Session-level timeout mirrors libssh2_session_set_timeout in C. */
        if timeout_ms > 0 {
            session.set_timeout(timeout_ms as u32);
        }

        session.handshake().map_err(|e| {
            if is_ssh_timeout(&e) { ShushError::Timeout } else { ShushError::Handshake(e) }
        })?;

        session.userauth_password(user, password).map_err(|e| {
            if is_ssh_timeout(&e) { ShushError::Timeout } else { ShushError::Auth }
        })?;

        Ok(ShushConn { session })
    }

    // ------------------------------------------------------------------ //
    // Operations                                                          //
    // ------------------------------------------------------------------ //

    /// Execute a remote shell command.
    ///
    /// Returns `(combined stdout + stderr, remote exit code)`.
    /// The exit code is always present on `Ok`; no separate out-parameter needed.
    pub fn exec(&mut self, command: &str) -> Result<(String, i32), ShushError> {
        let mut channel = self.session
            .channel_session()
            .map_err(ShushError::Channel)?;

        channel.exec(command).map_err(|e| {
            if is_ssh_timeout(&e) { ShushError::Timeout } else { ShushError::Channel(e) }
        })?;

        let mut output = String::new();
        channel.read_to_string(&mut output)?;    // io::Error → ShushError via From

        channel.wait_close().map_err(ShushError::Channel)?;

        let exit_code = channel.exit_status().map_err(ShushError::Channel)?;
        Ok((output, exit_code))
    }

    /// Copy `local_path` → `remote_path` on the server.
    pub fn scp_send(
        &mut self,
        local_path:  &Path,
        remote_path: &Path,
    ) -> Result<(), ShushError> {
        let data = fs::read(local_path)?;        // io::Error → ShushError

        let mut channel = self.session
            .scp_send(remote_path, 0o644, data.len() as u64, None)
            .map_err(|e| if is_ssh_timeout(&e) { ShushError::Timeout } else { ShushError::Scp(e) })?;

        channel.write_all(&data)?;               // io::Error → ShushError
        channel.send_eof().map_err(ShushError::Channel)?;
        channel.wait_eof().map_err(ShushError::Channel)?;
        channel.wait_close().map_err(ShushError::Channel)?;

        Ok(())
    }

    /// Copy `remote_path` on the server → `local_path` on disk.
    ///
    /// All bytes are buffered in memory before writing so a failed network
    /// transfer never leaves a partial file on disk.  If the final disk write
    /// fails, any partially created file is removed before returning.
    pub fn scp_recv(
        &mut self,
        remote_path: &Path,
        local_path:  &Path,
    ) -> Result<(), ShushError> {
        let (mut channel, stat) = self.session
            .scp_recv(remote_path)
            .map_err(|e| if is_ssh_timeout(&e) { ShushError::Timeout } else { ShushError::Scp(e) })?;

        // Pre-allocate to the expected file size; avoids repeated reallocations.
        let mut data = Vec::with_capacity(stat.size() as usize);
        channel.read_to_end(&mut data)?;         // io::Error → ShushError

        // Full transfer received; now write to disk.
        // If the write fails, attempt cleanup so no partial file is left.
        fs::write(local_path, &data).map_err(|e| {
            let _ = fs::remove_file(local_path);
            ShushError::from(e)
        })
    }
}

/// Sends a clean SSH disconnect when the connection is dropped.
/// Rust calls this automatically on any exit path — normal return,
/// early `?`, or panic — with no explicit cleanup code needed at call sites.
impl Drop for ShushConn {
    fn drop(&mut self) {
        let _ = self.session.disconnect(None, "Normal shutdown", None);
    }
}

// ------------------------------------------------------------------ //
// VM lifecycle (Windows only)                                         //
// ------------------------------------------------------------------ //
//
// spawn_vm / kill_vm live in a separate module to keep the platform-
// specific subprocess code isolated from the cross-platform SSH/SCP
// implementation.

#[cfg(target_os = "windows")]
mod vm;

/// Start a VM by running vm_start_cmd and block until SSH is ready.
///
/// Returns `Ok(true)` if the VM was launched, `Ok(false)` if it was
/// already running.  Windows only.
#[cfg(target_os = "windows")]
pub fn spawn_vm(
    vm_start_cmd: &str,
    host_port:    u16,
    timeout_ms:   u64,
) -> Result<bool, ShushError> {
    vm::spawn_vm(vm_start_cmd, host_port, timeout_ms)
}

/// Stop a VM by running vm_stop_cmd and block until the port closes.
///
/// Returns `Ok(true)` if a VM was stopped, `Ok(false)` if nothing was
/// running.  Windows only.
#[cfg(target_os = "windows")]
pub fn kill_vm(vm_stop_cmd: &str, host_port: u16) -> Result<bool, ShushError> {
    vm::kill_vm(vm_stop_cmd, host_port)
}
