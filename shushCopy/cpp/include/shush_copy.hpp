/**
 * shush_copy.hpp  --  shushCopy C++ API
 *
 * A thin RAII wrapper around the C library.  The C layer does all the
 * real work; this header adds:
 *   - std::string / std::string_view ergonomics
 *   - Exception-based error reporting  (ShushError)
 *   - Move semantics on Connection
 *
 * Depends on:  shush_copy.h  (the C API) + libssh2 (transitively)
 */

#pragma once

#include <exception>
#include <optional>
#include <string>
#include <string_view>

/* Forward-declare the C context so callers don't need shush_copy.h. */
struct shush_ctx;

namespace shush {

/* ------------------------------------------------------------------ */
/* Error codes (mirror shush_err_t)                                    */
/* ------------------------------------------------------------------ */

enum class Error : int {
    Ok        =  0,
    Param     = -1,
    Socket    = -2,
    Init      = -3,
    Handshake = -4,
    Auth      = -5,
    Chan      = -6,
    Scp       = -7,
    Io        = -8,
    Timeout   = -9,
};

/* ------------------------------------------------------------------ */
/* Exception type                                                      */
/* ------------------------------------------------------------------ */

class ShushError final : public std::exception {
public:
    explicit ShushError(Error code, std::string message = {});

    const char *what()  const noexcept override;
    Error        code() const noexcept;

private:
    Error       _code;
    std::string _message;
};

/* ------------------------------------------------------------------ */
/* Connection  (non-copyable, movable)                                 */
/* ------------------------------------------------------------------ */

class Connection {
public:
    /**
     * Connect to host:port using password authentication.
     * timeout_ms: session-level timeout for all blocking operations (0 = none).
     * Throws ShushError on any failure with a specific error code.
     */
    Connection(std::string_view host, int port,
               std::string_view user, std::string_view password,
               long timeout_ms = 10000);

    ~Connection();

    Connection(const Connection &)            = delete;
    Connection &operator=(const Connection &) = delete;

    Connection(Connection &&)            noexcept;
    Connection &operator=(Connection &&) noexcept;

    /**
     * Execute a remote command.  Returns combined stdout + stderr.
     * exit_code receives the remote process exit status on success;
     * pass nullptr to ignore.  Throws ShushError on channel failure.
     */
    std::string exec(std::string_view command, int *exit_code = nullptr);

    /**
     * SCP local_path -> remote_path on the server.
     * Throws ShushError on failure.
     */
    void scp_send(std::string_view local_path, std::string_view remote_path);

    /**
     * SCP remote_path on the server -> local_path on disk.
     * Throws ShushError on failure.
     */
    void scp_recv(std::string_view remote_path, std::string_view local_path);

private:
    shush_ctx *_ctx{nullptr};
};

/* ------------------------------------------------------------------ */
/* VM lifecycle  (Windows only)                                        */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

/**
 * Start a VM by running vm_start_cmd and block until SSH is ready.
 *
 * vm_start_cmd  Shell command to start the VM, e.g.
 *               "VBoxManage startvm MyVM --type headless".
 *               Must exit after submitting the start request.
 * host_port     SSH port on 127.0.0.1 to poll (default 9998).
 * timeout_ms    Maximum wait in ms (default 10000; 0 = no limit).
 *
 * Returns true if the VM was launched, false if already running.
 * Throws ShushError on failure.
 */
bool spawn_vm(std::string_view vm_start_cmd,
              int host_port  = 9998,
              int timeout_ms = 10000);

/**
 * Stop a VM by running vm_stop_cmd and block until the port closes.
 *
 * vm_stop_cmd  Shell command to stop the VM, e.g.
 *              "VBoxManage controlvm MyVM poweroff".
 * host_port    SSH port on 127.0.0.1 to monitor (default 9998).
 *
 * Returns true if a VM was stopped, false if nothing was running.
 * Throws ShushError on failure.
 */
bool kill_vm(std::string_view vm_stop_cmd, int host_port = 9998);

#endif /* _WIN32 */

} // namespace shush
