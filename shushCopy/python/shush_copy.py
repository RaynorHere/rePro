"""
shush_copy.py  --  shushCopy Python port

Mirrors the C API surface (connect / disconnect / exec / scp_send / scp_recv)
but expresses it as a context-manager class using paramiko under the hood.

Dependencies:
    pip install paramiko

Example:
    with ShushConn.connect("127.0.0.1", 9998, "myuser", "mypass") as conn:
        output, rc = conn.exec("uname -a")
        print(output, end="")
        conn.scp_send("payload.zip", "/tmp/payload.zip")
        conn.scp_recv("/tmp/result.log", "result.log")
"""

from __future__ import annotations

import getpass as _getpass
import socket as _socket
import subprocess as _subprocess
import sys
import time as _time
from pathlib import Path

import logging

import paramiko

# Suppress paramiko's internal logger so only shushCopy's own messages reach
# stderr.  The original exception is still accessible via ShushError.__cause__
# for callers that need low-level diagnostic detail.
logging.getLogger("paramiko").setLevel(logging.CRITICAL)


class ShushError(Exception):
    """Raised for any shushCopy operation failure."""


class ShushConn:
    """
    SSH + SCP connection to a remote host.

    Prefer using as a context manager so the connection is always closed:

        with ShushConn.connect(host, port, user, password) as conn:
            conn.exec("ls -la")
    """

    def __init__(self, client: paramiko.SSHClient) -> None:
        self._client = client

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    @classmethod
    def connect(cls, host: str, port: int,
                user: str, password: str,
                timeout_ms: int = 10000) -> "ShushConn":
        """
        Connect to SSH server at host:port using password authentication.
        timeout_ms: operation timeout in milliseconds (0 = no timeout).
        Raises ShushError on failure.
        """
        if not host or not user or not password or port <= 0:
            raise ShushError("invalid connection parameters")

        timeout_s = (timeout_ms / 1000.0) if timeout_ms > 0 else None

        client = paramiko.SSHClient()
        # In a controlled environment (known VM) auto-accept is appropriate;
        # swap for RejectPolicy + known_hosts load in stricter deployments.
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        try:
            client.connect(
                hostname=host,
                port=port,
                username=user,
                password=password,
                timeout=timeout_s,
                auth_timeout=timeout_s,
                look_for_keys=False,
                allow_agent=False,
            )
        except paramiko.AuthenticationException as exc:
            raise ShushError(f"authentication rejected: {exc}") from exc
        except Exception as exc:
            raise ShushError(f"connection failed: {exc}") from exc

        return cls(client)

    def disconnect(self) -> None:
        """Close the SSH connection.  Safe to call multiple times."""
        self._client.close()

    # ------------------------------------------------------------------
    # Operations
    # ------------------------------------------------------------------

    def exec(self, command: str) -> tuple[str, int]:
        """
        Execute a remote shell command.
        Returns (output, exit_code) where output is combined stdout + stderr
        and exit_code is the remote process exit status.
        Raises ShushError on channel failure.
        """
        if not command:
            raise ShushError("command must not be empty")
        try:
            _, stdout, stderr = self._client.exec_command(command)
            out = stdout.read().decode(errors="replace")
            err = stderr.read().decode(errors="replace")
            exit_code = stdout.channel.recv_exit_status()
            return out + err, exit_code
        except Exception as exc:
            raise ShushError(f"exec failed: {exc}") from exc

    def scp_send(self, local_path: str | Path, remote_path: str) -> None:
        """
        Copy local_path -> remote_path on the server (via SFTP, SCP semantics).
        Raises ShushError on failure.
        """
        local = Path(local_path)
        if not local.is_file():
            raise ShushError(f"local file not found: {local}")
        try:
            with self._client.open_sftp() as sftp:
                sftp.put(str(local), remote_path)
        except Exception as exc:
            raise ShushError(f"scp_send failed: {exc}") from exc

    def scp_recv(self, remote_path: str, local_path: str | Path) -> None:
        """
        Copy remote_path on the server -> local_path on disk.
        On failure, any partially written local file is deleted before
        raising ShushError so the caller never sees a truncated file.
        """
        local = Path(local_path)
        try:
            with self._client.open_sftp() as sftp:
                sftp.get(remote_path, str(local))
        except Exception as exc:
            local.unlink(missing_ok=True)
            raise ShushError(f"scp_recv failed: {exc}") from exc

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self) -> "ShushConn":
        return self

    def __exit__(self, *_: object) -> None:
        self.disconnect()


# ------------------------------------------------------------------
# VM lifecycle
# ------------------------------------------------------------------

# Local config directory: <repo-root>/config/
_VMAPPDEFS_DIR = Path(__file__).resolve().parent.parent / "config"

# Config file: config/shush.cfg  (gitignored — never committed)
_CONFIG_PATH = _VMAPPDEFS_DIR / "shush.cfg"

# Built-in defaults surfaced as prompt hints in 'shush config'
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 9998
_DEFAULT_USER = "user"


def _is_ssh_ready(host: str, port: int) -> bool:
    """Return True when the SSH daemon on host:port sends an SSH-2 banner."""
    try:
        with _socket.create_connection((host, port), timeout=1.0) as sock:
            data = sock.recv(256)
            return data.startswith(b"SSH-")
    except OSError:
        return False


def _is_port_reachable(host: str, port: int) -> bool:
    """Return True if a TCP connection to host:port succeeds (any service)."""
    try:
        with _socket.create_connection((host, port), timeout=0.5):
            return True
    except OSError:
        return False


def _wait_for_port_closed(host: str, port: int, timeout_s: float) -> bool:
    """Poll until host:port stops accepting TCP connections.  Returns True if
    closed within timeout_s, False if it is still open when the deadline passes."""
    deadline = _time.monotonic() + timeout_s
    while _time.monotonic() < deadline:
        if not _is_port_reachable(host, port):
            return True
        _time.sleep(0.5)
    return False


def spawn_vm(
    vm_start_cmd: str,
    host_port: int = 9998,
    poll_interval_ms: int = 500,
    timeout_ms: int = 10000,
) -> bool:
    """
    Start a VM by running vm_start_cmd and block until SSH is ready on
    127.0.0.1:host_port.

    vm_start_cmd must exit after submitting the start request; the VM itself
    can boot asynchronously.  Example:
        spawn_vm("VBoxManage startvm MyVM --type headless")

    Returns True if the VM was launched and SSH is ready, False if SSH was
    already responding (VM was already running).
    Raises ShushError on timeout.
    """
    if _is_ssh_ready("127.0.0.1", host_port):
        return False

    _subprocess.run(vm_start_cmd, shell=True, capture_output=True)

    deadline = (
        _time.monotonic() + timeout_ms / 1000.0 if timeout_ms > 0 else None
    )
    interval_s = poll_interval_ms / 1000.0

    while not _is_ssh_ready("127.0.0.1", host_port):
        if deadline is not None and _time.monotonic() >= deadline:
            raise ShushError(
                f"SSH on port {host_port} did not respond within {timeout_ms} ms."
            )
        _time.sleep(interval_s)

    return True


def kill_vm(
    vm_stop_cmd: str,
    host_port: int = 9998,
) -> bool:
    """
    Stop a VM by running vm_stop_cmd and block until host_port closes.

    Returns True if a VM was stopped, False if nothing was running.
    Raises ShushError if the port does not close within 10 s.
    """
    if not _is_port_reachable("127.0.0.1", host_port):
        return False

    _subprocess.run(vm_stop_cmd, shell=True, capture_output=True)

    if _wait_for_port_closed("127.0.0.1", host_port, timeout_s=10.0):
        return True
    raise ShushError(
        f"VM on port {host_port} could not be stopped within 10 s."
    )


# ------------------------------------------------------------------
# Config file support  (config/shush.cfg)
# ------------------------------------------------------------------

def _load_config() -> tuple[str, int, str, str, str, str]:
    """
    Load connection config from config/shush.cfg.
    Returns (host, port, user, password, vm_start, vm_stop).
    Raises ShushError with a helpful message if the file is missing or incomplete.
    """
    if not _CONFIG_PATH.is_file():
        raise ShushError(
            f"No config found at {_CONFIG_PATH}.\n"
            "  Run 'shush config' to set up connection details."
        )
    keys: dict[str, str] = {}
    with _CONFIG_PATH.open() as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            key, _, val = line.partition('=')
            keys[key.strip()] = val.rstrip('\r\n')  # preserve value exactly; passwords may contain '='
    required = ('host', 'port', 'user', 'password')
    missing = [k for k in required if k not in keys]
    if missing:
        raise ShushError(
            f"Config at {_CONFIG_PATH} is missing: {', '.join(missing)}.\n"
            "  Run 'shush config' to reconfigure."
        )
    try:
        port = int(keys['port'])
    except ValueError:
        raise ShushError(
            f"Config at {_CONFIG_PATH} has invalid port '{keys['port']}'.\n"
            "  Run 'shush config' to reconfigure."
        )
    return (keys['host'], port, keys['user'], keys['password'],
            keys.get('vm_start', ''), keys.get('vm_stop', ''))


def _run_config() -> int:
    """Interactive setup: prompts for all connection values and writes shush.cfg."""
    print("shush config — enter VM connection details (saved to config/shush.cfg)")
    host = input(f"  Host [{_DEFAULT_HOST}]: ").strip() or _DEFAULT_HOST
    port_str = input(f"  Port [{_DEFAULT_PORT}]: ").strip() or str(_DEFAULT_PORT)
    try:
        port = int(port_str)
        if port <= 0:
            raise ValueError()
    except ValueError:
        print(f"shush config: invalid port '{port_str}'", file=sys.stderr)
        return 1
    user = input(f"  User [{_DEFAULT_USER}]: ").strip() or _DEFAULT_USER
    password = _getpass.getpass("  Password: ")
    if not password:
        print("shush config: password must not be empty", file=sys.stderr)
        return 1
    vm_start = input("  VM start command [VBoxManage startvm MyVM --type headless]: ").strip() \
               or "VBoxManage startvm MyVM --type headless"
    vm_stop  = input("  VM stop command  [VBoxManage controlvm MyVM poweroff]: ").strip() \
               or "VBoxManage controlvm MyVM poweroff"
    _CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with _CONFIG_PATH.open('w') as f:
        f.write(f"host={host}\n")
        f.write(f"port={port}\n")
        f.write(f"user={user}\n")
        f.write(f"password={password}\n")
        f.write(f"vm_start={vm_start}\n")
        f.write(f"vm_stop={vm_stop}\n")
    print(f"  Saved to {_CONFIG_PATH}")
    return 0


# ------------------------------------------------------------------
# CLI entry point  (mirrors main.c)
# ------------------------------------------------------------------

def _usage(prog: str) -> None:
    print(
        f"Usage:\n"
        f"  {prog} config\n"
        f"  {prog} exec  <command>\n"
        f"  {prog} send  <local>  <remote>\n"
        f"  {prog} recv  <remote> <local>\n"
        f"  {prog} spawn\n"
        f"  {prog} kill",
        file=sys.stderr,
    )


def main() -> int:
    args = sys.argv[1:]
    prog = sys.argv[0]

    if not args:
        _usage(prog)
        return 1

    op = args[0]
    rest = args[1:]

    if op == "help":
        _usage(prog)
        return 0

    if op == "config":
        return _run_config()

    if op == "spawn" or op == "kill":
        try:
            host, port, user, password, vm_start, vm_stop = _load_config()
        except ShushError as exc:
            print(f"shush: {exc}", file=sys.stderr)
            return 1
        try:
            if op == "spawn":
                launched = spawn_vm(vm_start, host_port=port)
                print("VM ready." if launched else "VM is already running.")
            else:
                was_running = kill_vm(vm_stop, host_port=port)
                print("VM stopped." if was_running else "No VM was running.")
        except ShushError as exc:
            print(f"shush: {exc}", file=sys.stderr)
            return 1
        return 0

    # Validate operation-specific args before loading config.
    if op == "exec":
        if not rest:
            print("shush: exec requires <command>", file=sys.stderr)
            _usage(prog)
            return 1
    elif op in ("send", "recv"):
        if len(rest) < 2:
            print(f"shush: {op} requires <local> <remote>", file=sys.stderr)
            _usage(prog)
            return 1
    else:
        print(f"shush: unknown operation '{op}'", file=sys.stderr)
        _usage(prog)
        return 1

    try:
        host, port, user, password, vm_start, vm_stop = _load_config()
    except ShushError as exc:
        print(f"shush: {exc}", file=sys.stderr)
        return 1

    try:
        with ShushConn.connect(host, port, user, password) as conn:
            if op == "exec":
                output, remote_rc = conn.exec(rest[0])
                print(output, end="")
                if remote_rc != 0:
                    print(f"shush exec: remote process exited with code {remote_rc}",
                          file=sys.stderr)
                return remote_rc
            elif op == "send":
                conn.scp_send(rest[0], rest[1])
            elif op == "recv":
                conn.scp_recv(rest[0], rest[1])
    except ShushError as exc:
        print(f"shush: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
