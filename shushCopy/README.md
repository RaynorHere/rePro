# shushCopy

A lightweight SSH + SCP wrapper library in four languages, implemented identically
to make the language differences easy to compare.

Primary target: a virtual machine accessible over a loopback SSH tunnel
(`127.0.0.1:9998` on the host → `sshd:8889` inside the guest).

---

## API Surface

All four implementations expose the same five operations:

| Operation   | Description                              |
|-------------|------------------------------------------|
| `exec`      | Run a remote shell command, capture output |
| `send`      | Copy a local file to the remote host (`scp_send` in library code) |
| `recv`      | Copy a remote file to local disk (`scp_recv` in library code) |
| `spawn_vm`  | Start the VM and block until SSH is ready (Windows only — all four languages) |
| `kill_vm`   | Stop the VM and wait for the port to close (Windows only — all four languages) |

---

## Prerequisites

### C and C++ Ports (Windows)

| Requirement | Where to get it | Notes |
|---|---|---|
| Visual Studio 2022 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/) | Install the **"Desktop development with C++"** workload. Brings MSBuild, MSVC, and vcxproj support. The lighter **"Build Tools for Visual Studio 2022"** works too if you don't need the IDE. |
| vcpkg | `git clone https://github.com/microsoft/vcpkg.git C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics` | One-time machine-level install. Run `C:\vcpkg\vcpkg.exe integrate install` afterwards so MSBuild finds packages automatically. |
| libssh2 | `C:\vcpkg\vcpkg.exe install libssh2:x64-windows-static-md` | The triplet `x64-windows-static-md` gives a static lib (no DLL to ship) with the default `/MD` CRT linkage. |

`build\deps.props` resolves libssh2 via `%VCPKG_ROOT%` if set, then falls back
to `C:\vcpkg`.  Install vcpkg anywhere — as long as `VCPKG_ROOT` points at it
the build will find the libraries automatically.

### Rust Port (Windows)

| Requirement | Where to get it |
|---|---|
| Rust toolchain | `winget install Rustlang.Rustup` or [rustup.rs](https://rustup.rs) |
| vcpkg + libssh2 | Same as C/C++ above |
| `VCPKG_ROOT` env var | Set to your vcpkg root (e.g. `C:\vcpkg`) so Cargo locates libssh2 |

### Python Port (Any Platform)

| Requirement | Where to get it |
|---|---|
| Python 3.9+ | [python.org](https://www.python.org/) or `winget install Python.Python.3` |
| paramiko | `pip install -r python/requirements.txt` |

No compiler or native toolchain needed.  The SSH / SCP operations (`exec`,
`scp_send`, `scp_recv`) run on any platform.  `spawn_vm` and `kill_vm` are
`spawn_vm` and `kill_vm` are **Windows-only** across all four languages.
The VM start and stop commands are configured via `shush config`.

---

## VM SSH Tunnel Setup

shushCopy connects over `127.0.0.1` using a NAT port-forwarding rule configured
in your hypervisor.  The default port mapping is `9998` on the host → `8889`
on the guest (where sshd listens).

**VirtualBox example:**
```powershell
VBoxManage modifyvm "MyVM" --natpf1 "ssh,tcp,,9998,,8889"
```
Start the VM, confirm sshd is running on port 8889 inside the guest, then run
`shush config` to store the connection details.

Any hypervisor with NAT port forwarding (QEMU, Hyper-V, VMware, etc.) works
the same way — just substitute the appropriate start/stop commands when
prompted by `shush config`.

---

All four CLIs expose identical operations.  Replace `<shush>` with the
invocation for your port (build instructions in §Implementations below):

| Port | `<shush>` |
|------|-----------|
| C | `x64\Release\shush.exe` |
| C++ | `x64\Release\shushcpp.exe` |
| Python | `python python/shush_copy.py` |
| Rust | `.\target\release\shush_rs.exe` |

```
<shush> config                   — one-time setup; stores connection details in config/shush.cfg
<shush> exec  <command>          — run a remote shell command; output to stdout
<shush> send  <local>  <remote>  — copy a local file to the VM
<shush> recv  <remote> <local>   — copy a file from the VM to local disk
<shush> spawn                    — start the VM and wait for SSH  (Windows only)
<shush> kill                     — stop the VM and close the SSH port (Windows only)
```

---

## Implementations

### `c/`  — Straight C (Primary)

Open `shushCopy.sln` in Visual Studio 2022 or build
from the command line (run from the `shushCopy\` directory):

```bat
cd shushCopy
msbuild shushCopy.sln /p:Configuration=Release /p:Platform=x64
```

Invocation: `x64\Release\shush.exe` — see §Command Syntax for the full operation list.

VM lifecycle (`spawn` / `kill`) commands and connection details are stored in
`config/shush.cfg` (written by `shush config`).

### `cpp/`  — C++ Port

RAII wrapper around the C library.  Shares the same `shush_copy.c` source.
Built by the same solution — `shushcpp.exe` is produced alongside `shush.exe`:

```bat
cd shushCopy
msbuild shushCopy.sln /p:Configuration=Release /p:Platform=x64
```

Invocation: `x64\Release\shushcpp.exe` — see §Command Syntax for the full operation list.

Or use the class and free functions directly:

```cpp
#include "shush_copy.hpp"

// SSH / SCP  (RAII connection)
shush::Connection conn{"127.0.0.1", 9998, "myuser", "mypass"};
std::cout << conn.exec("uname -a");
conn.scp_send("payload.zip", "/tmp/payload.zip");
conn.scp_recv("/tmp/result.log", "result.log");

// VM lifecycle  (free functions, Windows only)
bool launched     = shush::spawn_vm("VBoxManage startvm MyVM --type headless");
bool was_running  = shush::kill_vm("VBoxManage controlvm MyVM poweroff");
```

`spawn_vm` / `kill_vm` are thin wrappers around the C functions that convert
the C int return (`+1` / `0` / negative error) to `bool + ShushError throw`.

### `rust/`  — Rust Port

```bat
cd shushCopy\rust
cargo build --release
```

Invocation: `.\target\release\shush_rs.exe` — see §Command Syntax for the full operation list.

Or use the library directly:

```rust
use shush_copy::ShushConn;

let mut conn = ShushConn::connect("127.0.0.1", 9998, "myuser", "mypass", 10_000)?;
let (output, rc) = conn.exec("uname -a")?;
print!("{}", output);
conn.scp_send("payload.zip".as_ref(), "/tmp/payload.zip".as_ref())?;
conn.scp_recv("/tmp/result.log".as_ref(), "result.log".as_ref())?;

let launched     = shush_copy::spawn_vm("VBoxManage startvm MyVM --type headless", 9998, 10_000)?;
shush_copy::kill_vm("VBoxManage controlvm MyVM poweroff", 9998)?;
```

`spawn_vm` / `kill_vm` are in `src/vm.rs` (a `#[cfg(target_os = "windows")]`
submodule).  Both use `std::process::Command` to run the user-supplied shell
commands; no unsafe code or platform crates required.

### `python/`  — Python Port

Invocation: `python python/shush_copy.py` — see §Command Syntax for the full operation list.

#### `spawn_vm` — Starting the VM (Windows only)

`spawn_vm` runs the configured `vm_start` command and polls until the SSH
daemon responds.  The start command and SSH port are stored by `shush config`.
Any hypervisor whose start operation can be expressed as a single shell command
works.

**CLI:**

```sh
python python/shush_copy.py spawn
# prints: VM ready.
```

**From a script:**

```python
from shush_copy import spawn_vm

spawn_vm("VBoxManage startvm MyVM --type headless")
# blocks until sshd sends SSH-2 banner (10 s timeout)
```

**What `spawn_vm` does internally:**
1. Checks if SSH is already responding on `host_port`.  If yes, returns `False` —
   the running VM is left untouched.
2. Runs `vm_start_cmd` as a shell command (exits quickly; VM boots async).
3. Polls every 500 ms until the SSH daemon responds with an SSH-2 banner.
4. Returns `True` on success; raises `ShushError` on timeout
   (default 10 000 ms; pass `timeout_ms=0` for no limit).

CLI output: `VM ready.` (launched) or `VM is already running.` (was up).

#### `kill_vm` — Stopping the VM (Windows only)

Runs the configured `vm_stop` command and blocks until the SSH port closes.

**CLI:**

```sh
python python/shush_copy.py kill
# prints: VM stopped.   (or: No VM was running.)
```

**From a script:**

```python
from shush_copy import kill_vm

was_running = kill_vm("VBoxManage controlvm MyVM poweroff")
# True if a VM was stopped, False if nothing was running
```

`kill_vm` runs `vm_stop_cmd`, then polls until the port closes (10 s timeout).
Raises `ShushError` if the VM cannot be confirmed down.

#### SSH / SCP — direct class usage

```python
from shush_copy import ShushConn

with ShushConn.connect("127.0.0.1", 9998, "myuser", "mypass") as conn:
    output, rc = conn.exec("uname -a")
    print(output, end="")
    conn.scp_send("payload.zip", "/tmp/payload.zip")
    conn.scp_recv("/tmp/result.log", "result.log")
```

---

## Design Notes

- **C** owns the implementation.  `libssh2` handles the SSH wire protocol.
- **Python** is an independent re-implementation via `paramiko` (uses SFTP
  internally for file transfer — functionally equivalent to SCP).
- **C++** is a pure wrapper: no duplication of logic, just RAII + exceptions
  on top of the C API.
- **Rust** uses the `ssh2` crate — a safe Rust wrapper around libssh2.  Error
  handling is via `Result<T, E>` with compiler-enforced exhaustive handling;
  resource cleanup is via `Drop` (Rust's built-in RAII).
- Password authentication only for now.  Key-based auth is a natural next step
  (`shush_connect_key` in C, `connect(pkey=...)` in Python).
- **C cross-platform portability:** the C implementation compiles from the same
  source on both Windows (MSVC/cl.exe via MSBuild) and Linux (gcc), confirmed
  by a native Debian build during the QA pass.  On Linux, two packages are
  sufficient: a C11 compiler and `libssh2-1-dev`.  The `#ifdef _WIN32` platform
  shim handles socket types, stat variants, and Winsock boilerplate.  One
  dependency to note: the Linux build requires `-D_POSIX_C_SOURCE` to expose
  `getaddrinfo`, making the code POSIX-dependent rather than purely ISO C11.
  This is standard for any real Linux environment but relevant if the target
  is an unusual bare-metal or non-POSIX platform.

### Error Handling

All four ports share the same error vocabulary but express it differently.

#### Error Codes

| Value | C / C++ constant | Rust variant | Meaning |
|---|---|---|---|
| 0 | `SHUSH_OK` | `Ok(...)` | Success |
| ΓêÆ1 | `SHUSH_ERR_PARAM` | `ShushError::Param` | NULL or invalid argument |
| ΓêÆ2 | `SHUSH_ERR_SOCKET` | `ShushError::Io` | TCP connect failed |
| ΓêÆ3 | `SHUSH_ERR_INIT` | `ShushError::Init` | libssh2 / Winsock init failed |
| ΓêÆ4 | `SHUSH_ERR_HANDSHAKE` | `ShushError::Handshake` | SSH key exchange failed |
| ΓêÆ5 | `SHUSH_ERR_AUTH` | `ShushError::Auth` | Credentials rejected by server |
| ΓêÆ6 | `SHUSH_ERR_CHAN` | `ShushError::Channel` | SSH channel open / exec failed |
| ΓêÆ7 | `SHUSH_ERR_SCP` | `ShushError::Scp` | SCP channel error |
| ΓêÆ8 | `SHUSH_ERR_IO` | `ShushError::Io` | Local file I/O error |
| ΓêÆ9 | `SHUSH_ERR_TIMEOUT` | `ShushError::Timeout` | Operation timed out |
| n/a | n/a | `ShushError::Vm(String)` | VM lifecycle failure (spawn/kill) — Rust only; C maps these to `SHUSH_ERR_PARAM` / `SHUSH_ERR_TIMEOUT` with descriptive `stderr` output |

Python raises `ShushError` with a descriptive message string.  The original library
exception is preserved in `__cause__` and available to any caller that needs
low-level diagnostic detail.

Rust returns `Result<T, ShushError>` where `ShushError` is an enum.  The compiler
enforces exhaustive handling — it is not possible to silently ignore an error.

#### Default Timeout

All CLI tools and direct-use examples default to **10,000 ms (10 s)**.  Pass
`timeout_ms = 0` to disable the timeout entirely.  For the typical use case
(local loopback to a local VM), 10 s is generous;
operations typically complete in well under 500 ms.

#### Known Behavioral Difference — Rust HANDSHAKE vs TIMEOUT

**Scenario:** a TCP server accepts the connection but closes it before sending an
SSH banner (e.g., a wrong port, a non-SSH service, or a firewall RST).

| Port | Error reported | Latency |
|---|---|---|
| C | `SSH handshake failed` | Immediate |
| Python | `connection failed: Error reading SSH protocol banner` | Immediate |
| C++ | `SSH handshake failed` | Immediate |
| Rust | `operation timed out` | After `timeout_ms` (default 10 s) |

**Root cause investigated:** the call path through the `ssh2` crate is identical
to C at the libssh2 API level — same `libssh2_session_handshake`, same blocking
mode, same session timeout value.  Despite this, libssh2 returns
`LIBSSH2_ERROR_TIMEOUT` via the Rust path on Windows rather than
`LIBSSH2_ERROR_SOCKET_RECV`.  The exact cause was not resolved without attaching
a source-level debugger to libssh2 itself; this is noted as a future
investigation item.

**Practical impact:** this scenario does not arise in the intended use case.
A local VM sshd will always respond immediately with an SSH banner in normal
operation.

#### Known Limitation — exec Output Truncation (C and C++ Only)

`shush_exec` in the C implementation writes into a caller-supplied fixed-size
buffer (default 65,535 bytes in `main.c` and the C++ wrapper).  Output
exceeding this limit is **silently truncated**: the function returns `SHUSH_OK`
and the exit code is correct, but the output string is incomplete.

Python and Rust are not affected — both use dynamically-growing buffers
(`stdout.read()` and `read_to_string()` respectively) with no fixed ceiling.

**Practical impact:** at 65,535 bytes, roughly 820 lines of 80-character text,
this limit is far above typical exec output for the intended use case (Python
script prints a one-line status message; large data is returned via
`scp_recv`, not `exec`).  It becomes relevant only if `exec` is used to
stream large datasets or log files to stdout directly.

**Future options if this limit becomes a constraint:**

| Option | Cost | Gain |
|---|---|---|
| Larger fixed buffer (e.g. 1 MB) | One-line change in caller | Higher ceiling, still silently truncates |
| Detect truncation (`SHUSH_ERR_TRUNCATED`) | Small API addition | Silent truncation becomes explicit error |
| `shush_exec_dyn` — dynamic `malloc`/`realloc` buffer | New function + caller must `free()` | No truncation; caller owns the allocation |

### Language Comparison

The four ports implement the same API but differ in how they handle errors,
manage memory, depend on external libraries, and deploy:

| Aspect | C | Python | C++ | Rust |
|---|---|---|---|---|
| Error handling | Return codes + out-param | Exceptions | Exceptions | `Result<T,E>` — compiler-enforced |
| Memory management | Manual `malloc`/`free` | Garbage collector | RAII (destructors) | Ownership (compile-time RAII) |
| SSH library | libssh2 (static) | paramiko (pure Python) | libssh2 via C wrapper | `ssh2` crate (libssh2 wrapper) |
| Build system | MSBuild + vcpkg | pip | MSBuild + vcpkg | Cargo (self-managing) |
| Deployment | Single binary (`.exe` on Windows; native binary on Linux with `gcc` + `libssh2-1-dev`) | Needs Python interpreter | Single `.exe` (Windows; shares C source) | Single binary, no runtime |
| Concurrent connections | Documented risk (see §Initialization Design) | Safe | Documented risk | Safe — `ssh2` uses process-lifetime init (see §Initialization Design) |

**A note on scaling — async Rust and `russh`:**
The Rust port here uses `ssh2` in synchronous blocking mode, mirroring the
one-connection-at-a-time model of the C implementation — the right fit for the
current use case.  If shushCopy ever needs to orchestrate multiple simultaneous
VM connections (fleet management, parallel agent tasks),
[`russh`](https://github.com/warp-tech/russh) is the natural evolution: a
pure-Rust async SSH implementation built on `tokio`, no C dependency,
trivially cross-compilable, and designed for concurrent connections.  It would be
deliberate overkill today; it is the correct answer at scale.

### Security Considerations

Current design choices and known gaps to address before wider roll-out:

**What is handled today:**
- All traffic travels over an SSH-2 encrypted channel (libssh2 / paramiko).
  Data in transit is protected against interception and tampering.
- Password authentication is validated server-side by sshd; a rejected credential
  surfaces as `SHUSH_ERR_AUTH` rather than a silent failure.
- The session timeout (`timeout_ms`, default 10 s) limits exposure from a
  stalled or unresponsive server.
- Connection details and the VM password are stored in `config/shush.cfg`
  (gitignored — never committed).  `shush config` writes this file; the password
  is entered with echo off so it never appears on screen.  Credentials never
  appear in CLI arguments, shell history, or log files.
- The VM start and stop commands are stored in `config/shush.cfg` alongside
  the connection details and run as shell commands — no hypervisor-specific
  binaries are hardcoded.

**Known gaps and recommendations for production:**

| Gap | Risk | Recommended fix |
|-----|------|-----------------|
| Password auth | Credential exposure if the calling process leaks args or memory | Switch to key-based auth (`LIBSSH2_HOSTKEY_*` / paramiko `pkey=`) |
| Host key not verified | Susceptible to MITM on first connect (`AutoAddPolicy` in Python; no known-hosts check in C) | Load a known-hosts file; reject unknown keys |
| Credentials passed as plain strings in library API | Password lives as plaintext in heap memory between `load_config()` and connection establishment.  **Per-port zeroing status:** C — `SecureZeroMemory` on the stack struct immediately after `shush_connect()` returns Γ£à; C++ — `std::fill` on the `std::string` buffer immediately after `Connection` constructor returns Γ£à; Rust — `drop(cfg)` deallocates the `String` but does **not** zero the heap bytes before the allocator reclaims them ΓÜá∩╕Å; Python — no explicit zeroing; CPython string lifetime is GC-controlled and indeterminate ΓÜá∩╕Å.  The exposure window is milliseconds for a live process; a heap dump taken during that window or after a crash can reveal the password in all four ports.  CLI arg exposure resolved in v1.5. | C / C++: already mitigated at the call site.  Rust: use `zeroize` crate (`Zeroizing<String>` wrapper) to guarantee zeroing on drop.  Python: use `bytearray` instead of `str` for the password and explicitly zero it.  All ports: Windows Credential Manager or a hardware-backed secrets store eliminates user-space plaintext entirely. |
| `shush.cfg` stored as plaintext | Password on disk in `config/shush.cfg`; readable by any process with file-system access to the user's account | Acceptable for a local dev VM; for production use Windows Credential Manager or an encrypted secrets store |
| No mutual TLS / certificate pinning | Relies entirely on SSH layer | Acceptable for SSH; add cert pinning if protocol is ever extended beyond SSH |
| Static analysis (`_CRT_SECURE_NO_WARNINGS`) | Suppresses MSVC warnings for standard C functions (`fopen` etc.) to preserve cross-platform portability — these calls are safe as written but warrant review if the code is extended | Review any new file I/O additions for bounds safety |
| Silent success on misformed `exec` commands | If the command string is accidentally malformed (e.g. a doubled subcommand token causes the unintended word to be sent as the remote command), and that command exits 0 with no output, the result is indistinguishable from correct execution.  Note: if the unintended command fails on the remote (e.g. `~/Desktop/test` with no executable bit), exit code 127 is correctly propagated — the silent case is specifically when the unintended command happens to succeed quietly | Add a `--verbose` / dry-run flag that echoes the exact command string before opening the SSH channel, giving the caller a chance to catch mistakes before they fire |

### Initialization Design

The C library (and the C++ wrapper built on top of it) currently initialises
`libssh2` and the platform socket layer (`WSAStartup` on Windows) **per
connection** inside `shush_connect()`, and tears them down inside
`shush_disconnect()`.

This is correct and sufficient for the current constraint: a single SSH tunnel
to one VM, one caller at a time.

**The risk if that assumption ever changes:** if two `shush_ctx_t` connections
are live simultaneously, `shush_disconnect()` on the first will call
`libssh2_exit()` and `WSACleanup()`, tearing down process-wide crypto and
socket state that the second connection is still actively using.  Result:
undefined behaviour — most likely a crash or silent data corruption.

`c/src/shush_copy.c` contains a commented-out `shush_library_init()` /
`shush_library_shutdown()` pair that moves initialisation to process-lifetime
scope.  The comment block immediately above those functions contains
step-by-step migration instructions.  Matching declarations are commented out
in `c/include/shush_copy.h`.

**How the Rust port relates:** The `ssh2` crate addresses this problem
differently — it initialises libssh2 once at process startup using a `Once`
guard and deliberately never calls `libssh2_exit()`.  Multiple simultaneous
`ShushConn` instances are therefore safe with the current Rust port, with no
changes to user code.  The `russh` alternative (noted in §Language Comparison)
eliminates the concern entirely: with no libssh2 dependency there is no global
C library state to manage.

---

## QA Record

This section documents the testing and static-analysis pass performed against
initial concept (commit `14bdaa8` baseline, QA applied on top).

### Error Scenario Battery

All four ports were exercised against each error condition using the CLI tools
against a live WSL Debian target (sshd on `127.0.0.1:8889`).

| Scenario | Trigger | C | Python | C++ | Rust |
|---|---|---|---|---|---|
| AUTH | Wrong password | `SSH authentication rejected` \| exit 1 | `authentication rejected` \| exit 1 | `SSH authentication rejected` \| exit 1 | `SSH authentication rejected` \| exit 1 |
| SOCKET | Wrong port (nothing listening) | `TCP connect failed` \| exit 1 | `connection failed` \| exit 1 | `TCP connect failed` \| exit 1 | `TCP / I/O error` \| exit 1 |
| IO | Non-existent local file on send | `local file I/O error` \| exit 1 | `local file not found` \| exit 1 | `local file I/O error` \| exit 1 | `TCP / I/O error` \| exit 1 |
| PARAM | Port 0 | `invalid parameter` \| exit 1 | `invalid connection parameters` \| exit 1 | `invalid parameter` \| exit 1 | `port 0 is not valid` \| exit 1 |
| HANDSHAKE | Fake TCP listener (non-SSH service) | `SSH handshake failed` \| immediate | `Error reading SSH protocol banner` \| immediate | `SSH handshake failed` \| immediate | `operation timed out` \| after `timeout_ms` |

See §Error Handling for the detailed write-up of the Rust HANDSHAKE
behavioural difference and its investigation.

### Findings and Dispositions

| # | Finding | Port | Severity | Action |
|---|---|---|---|---|
| 1 | Port 0 gave TCP error instead of PARAM | Rust | Minor | Fixed: added explicit `port == 0` guard in `lib.rs` |
| 2 | paramiko internal tracebacks leaked to stderr on connection failure | Python | Minor | Fixed: `logging.getLogger("paramiko").setLevel(logging.CRITICAL)` |
| 3 | HANDSHAKE failure reports TIMEOUT (after `timeout_ms`) instead of immediately | Rust | Minor | Investigated exhaustively; root cause in `ssh2` crate / libssh2 Windows interaction not resolved without debugger. Documented. Attaching a source-level debugger is a noted future item. |
| 4 | exec output silently truncated at 65,535 bytes (C and C++ only) | C, C++ | Minor | Documented as known limitation; does not affect intended use case. Future options noted in §Error Handling. |
| 5 | Dead variable initialiser (`e = SHUSH_OK`) flagged by cppcheck | C | Style | Addressed: initialiser commented out with explanation of when to restore it. |

### Static Analysis

| Tool | Scope | Result | Finding |
|---|---|---|---|
| `cargo clippy -- -D warnings` | Rust (`rust/src/`) | Γ£à Clean after fix | Redundant borrow `&socket_addr` in `TcpStream::connect()` call — removed |
| `cppcheck --enable=warning,style,performance,portability` | C (`c/src/`), C++ (`cpp/src/`) | Γ£à Clean after fix | Dead initialiser on `e` in `shush_connect` — see finding #5 above |
| Valgrind (WSL Debian, gcc debug build) | C | Γ£à Clean | Zero errors, zero leaks across happy path (exec, send, recv) and AUTH error path. 8,550 allocs / 8,550 frees per run. Compiled with `gcc -g -O0 -std=c11 -D_POSIX_C_SOURCE=200809L` against system libssh2. |
