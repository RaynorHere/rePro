/*
 * shush_copy.cpp  --  shushCopy C++ implementation
 *
 * Delegates everything to the C API in ../c/src/shush_copy.c.
 * This file just adds the C++ wrapper glue.
 */

#include "shush_copy.hpp"

/* Pull in the C API -- the CMake build compiles shush_copy.c alongside. */
#include "shush_copy.h"   /* from ../c/include -- see shush_copy_cpp.vcxproj */

#ifdef _WIN32
#include "shush_vm.h"     /* C VM lifecycle API */
#endif

#include <utility>   /* std::exchange */
#include <vector>

namespace shush {

/* ------------------------------------------------------------------ */
/* ShushError                                                          */
/* ------------------------------------------------------------------ */

ShushError::ShushError(Error code, std::string message)
    : _code(code)
    , _message(message.empty()
                   ? shush_strerror(static_cast<int>(code))
                   : std::move(message))
{}

const char *ShushError::what()  const noexcept { return _message.c_str(); }
Error        ShushError::code() const noexcept { return _code; }

/* ------------------------------------------------------------------ */
/* Internal helper: throw on non-zero C error code                    */
/* ------------------------------------------------------------------ */

static void check(int rc)
{
    if (rc != SHUSH_OK)
        throw ShushError(static_cast<Error>(rc));
}

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

Connection::Connection(std::string_view host, int port,
                       std::string_view user, std::string_view password,
                       long timeout_ms)
{
    shush_err_t conn_err = SHUSH_OK;
    _ctx = shush_connect(
        std::string(host).c_str(),
        port,
        std::string(user).c_str(),
        std::string(password).c_str(),
        timeout_ms,
        &conn_err
    );

    if (!_ctx)
        throw ShushError(static_cast<Error>(conn_err),
                         shush_strerror(conn_err));
}

Connection::~Connection()
{
    shush_disconnect(_ctx);   /* null-safe in the C layer */
}

Connection::Connection(Connection &&other) noexcept
    : _ctx(std::exchange(other._ctx, nullptr))
{}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other) {
        shush_disconnect(_ctx);
        _ctx = std::exchange(other._ctx, nullptr);
    }
    return *this;
}

std::string Connection::exec(std::string_view command, int *exit_code)
{
    std::vector<char> buf(65536, '\0');
    check(shush_exec(_ctx, std::string(command).c_str(),
                     buf.data(), buf.size(), exit_code));
    return std::string(buf.data());
}

void Connection::scp_send(std::string_view local_path,
                           std::string_view remote_path)
{
    check(shush_scp_send(_ctx,
                         std::string(local_path).c_str(),
                         std::string(remote_path).c_str()));
}

void Connection::scp_recv(std::string_view remote_path,
                           std::string_view local_path)
{
    check(shush_scp_recv(_ctx,
                         std::string(remote_path).c_str(),
                         std::string(local_path).c_str()));
}

/* ------------------------------------------------------------------ */
/* VM lifecycle  (Windows only)                                        */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

/* Maps the C VM int return to bool, or throws on negative.
 * C convention:  >0 = did work,  0 = already-done,  <0 = error. */
static bool vm_check(int r)
{
    if (r < 0)
        throw ShushError(static_cast<Error>(r));
    return r == 1;
}

bool spawn_vm(std::string_view vm_start_cmd,
              int host_port,
              int timeout_ms)
{
    std::string cmd(vm_start_cmd);
    return vm_check(shush_spawn_vm(cmd.c_str(), host_port, timeout_ms));
}

bool kill_vm(std::string_view vm_stop_cmd, int host_port)
{
    std::string cmd(vm_stop_cmd);
    return vm_check(shush_kill_vm(cmd.c_str(), host_port));
}

#endif /* _WIN32 */

} // namespace shush
