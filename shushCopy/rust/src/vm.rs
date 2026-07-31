//! VM lifecycle — hypervisor-agnostic (Windows only).
//!
//! Runs user-supplied shell commands to start/stop a virtual machine and
//! polls 127.0.0.1:host_port for an SSH-2 banner to confirm readiness.
//! Any hypervisor whose start/stop can be expressed as a single shell command
//! (VirtualBox, QEMU, Hyper-V, etc.) will work.

use std::io::Read;
use std::net::TcpStream;
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant};

use crate::ShushError;

const POLL_INTERVAL: Duration = Duration::from_millis(500);
const STOP_GRACE:    Duration = Duration::from_secs(10);

// ------------------------------------------------------------------ //
// Network readiness checks                                            //
// ------------------------------------------------------------------ //

fn is_port_reachable(port: u16) -> bool {
    TcpStream::connect_timeout(
        &format!("127.0.0.1:{}", port).parse().unwrap(),
        Duration::from_millis(500),
    ).is_ok()
}

fn is_ssh_ready(port: u16) -> bool {
    let stream = TcpStream::connect_timeout(
        &format!("127.0.0.1:{}", port).parse().unwrap(),
        Duration::from_secs(1),
    );
    let mut stream = match stream {
        Ok(s)  => s,
        Err(_) => return false,
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(1)));
    let mut buf = [0u8; 256];
    match stream.read(&mut buf) {
        Ok(n) if n >= 4 => buf[..4] == *b"SSH-",
        _               => false,
    }
}

fn wait_for_port_closed(port: u16, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    while is_port_reachable(port) {
        if Instant::now() >= deadline { return false; }
        thread::sleep(POLL_INTERVAL);
    }
    true
}

// ------------------------------------------------------------------ //
// Command runner                                                      //
// ------------------------------------------------------------------ //

/// Run a shell command and ignore the result.  Uses `cmd /C` on Windows,
/// `sh -c` elsewhere.
fn run_command(cmd: &str) {
    #[cfg(target_os = "windows")]
    { Command::new("cmd").args(["/C", cmd]).status().ok(); }
    #[cfg(not(target_os = "windows"))]
    { Command::new("sh").args(["-c", cmd]).status().ok(); }
}

// ------------------------------------------------------------------ //
// Public API (re-exported by lib.rs)                                 //
// ------------------------------------------------------------------ //

pub fn spawn_vm(
    vm_start_cmd: &str,
    host_port:    u16,
    timeout_ms:   u64,
) -> Result<bool, ShushError> {
    if vm_start_cmd.is_empty() || host_port == 0 {
        return Err(ShushError::Param("vm_start_cmd and host_port must be set".into()));
    }

    if is_ssh_ready(host_port) { return Ok(false); }

    run_command(vm_start_cmd);

    let deadline = (timeout_ms > 0)
        .then(|| Instant::now() + Duration::from_millis(timeout_ms));

    loop {
        thread::sleep(POLL_INTERVAL);
        if is_ssh_ready(host_port) { return Ok(true); }
        if deadline.map(|t| Instant::now() >= t).unwrap_or(false) {
            return Err(ShushError::Vm(format!(
                "SSH on port {} did not respond within {} ms", host_port, timeout_ms
            )));
        }
    }
}

pub fn kill_vm(
    vm_stop_cmd: &str,
    host_port:   u16,
) -> Result<bool, ShushError> {
    if vm_stop_cmd.is_empty() || host_port == 0 {
        return Err(ShushError::Param("vm_stop_cmd and host_port must be set".into()));
    }

    if !is_port_reachable(host_port) { return Ok(false); }

    run_command(vm_stop_cmd);

    if wait_for_port_closed(host_port, STOP_GRACE) {
        Ok(true)
    } else {
        Err(ShushError::Vm(format!("VM on port {} could not be stopped", host_port)))
    }
}