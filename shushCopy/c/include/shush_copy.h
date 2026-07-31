/**
 * shush_copy.h  --  shushCopy public C API
 *
 * Two capabilities:
 *   1. Execute a remote command over SSH
 *   2. SCP a file to/from a remote host
 *
 * All functions are blocking.  Thread-safety: one shush_ctx_t per thread.
 *
 * Initialization model: shush_connect() calls libssh2_init() (and WSAStartup
 * on Windows) on every connection; shush_disconnect() calls the matching
 * teardown.  This is correct for single-connection use.  See the commented-out
 * shush_library_init / shush_library_shutdown declarations below (after the
 * Lifecycle section) for the process-lifetime alternative needed if concurrent
 * connections are ever required.
 */

#ifndef SHUSH_COPY_H
#define SHUSH_COPY_H

#include <stddef.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Error codes                                                          */
/* ------------------------------------------------------------------ */

typedef enum shush_err {
    SHUSH_OK              =  0,  /* success                               */
    SHUSH_ERR_PARAM       = -1,  /* NULL or invalid argument              */
    SHUSH_ERR_SOCKET      = -2,  /* TCP connect failed                    */
    SHUSH_ERR_INIT        = -3,  /* libssh2 / Winsock initialisation      */
    SHUSH_ERR_HANDSHAKE   = -4,  /* SSH key exchange / handshake failed   */
    SHUSH_ERR_AUTH        = -5,  /* authentication rejected               */
    SHUSH_ERR_CHAN         = -6,  /* SSH channel open / exec failed        */
    SHUSH_ERR_SCP         = -7,  /* SCP channel open failed               */
    SHUSH_ERR_IO          = -8,  /* local file I/O or read/write          */
    SHUSH_ERR_TIMEOUT     = -9,  /* operation timed out                   */
} shush_err_t;

/* ------------------------------------------------------------------ */
/* Connection handle (opaque)                                          */
/* ------------------------------------------------------------------ */

typedef struct shush_ctx shush_ctx_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * Connect to an SSH server at host:port using password authentication.
 *
 * Returns a heap-allocated context on success, NULL on any failure.
 * All resources are owned by the returned pointer; free with shush_disconnect().
 *
 * timeout_ms -- session-level timeout in milliseconds applied to all
 *   blocking libssh2 operations on this connection (handshake, auth,
 *   exec reads/writes, SCP transfers).  Pass 0 for no timeout.
 *
 * err  -- out-parameter for the specific failure reason when NULL is returned:
 *   SHUSH_ERR_PARAM      invalid arguments
 *   SHUSH_ERR_SOCKET     TCP connect failed (host unreachable / wrong port)
 *   SHUSH_ERR_INIT       libssh2 / Winsock initialisation failed
 *   SHUSH_ERR_HANDSHAKE  SSH handshake failed (key exchange, version mismatch)
 *   SHUSH_ERR_AUTH       credentials rejected by the server
 *   SHUSH_ERR_TIMEOUT    handshake or auth exceeded timeout_ms
 *   Pass NULL to ignore.
 */
shush_ctx_t *shush_connect(const char *host, int port,
                            const char *user, const char *password,
                            long timeout_ms,
                            shush_err_t *err);

/**
 * Disconnect and free all resources.  Safe to call with ctx == NULL.
 */
void shush_disconnect(shush_ctx_t *ctx);

/* ------------------------------------------------------------------
 * ALTERNATIVE: process-lifetime init / shutdown  (currently unused)
 *
 * shush_connect() / shush_disconnect() above call libssh2_init() and
 * WSAStartup() (Windows) per connection, and the matching teardown per
 * disconnect.  This is safe when only one connection is alive at a time.
 *
 * Concurrent connections break this model: shush_disconnect() on
 * connection A calls libssh2_exit() / WSACleanup(), tearing down
 * process-wide state that connection B is still actively using.
 * Result: undefined behaviour -- crash or silent corruption.
 *
 * The two functions below move that responsibility to process scope.
 * Call shush_library_init() once at startup before creating any
 * connections, and shush_library_shutdown() once at exit.  You must
 * also remove the init/exit calls from shush_connect / shush_disconnect
 * (see the HOW TO SWITCH block in shush_copy.c and README.md
 * §"Initialization design" for step-by-step instructions).
 * ------------------------------------------------------------------
 */
/*
shush_err_t shush_library_init(void);
void        shush_library_shutdown(void);
*/

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */

/**
 * Execute a remote shell command and capture stdout + stderr into buf.
 * buf is always NUL-terminated on success.  Output that exceeds buf_len-1
 * bytes is silently truncated.
 *
 * exit_code  -- out-parameter for the remote process exit status.
 *   On any API/channel error (return != SHUSH_OK) it is set to -1.
 *   On SHUSH_OK it holds the actual remote exit code (typically 0-255).
 *   A non-zero exit_code with a SHUSH_OK return means the SSH channel
 *   worked but the remote command itself reported failure -- the caller
 *   must check BOTH: return value for API health, exit_code for command
 *   health.  Pass NULL to ignore the exit code.
 *
 * Returns SHUSH_OK or a negative shush_err_t value.
 */
int shush_exec(shush_ctx_t *ctx, const char *command,
               char *buf, size_t buf_len,
               int *exit_code);

/**
 * SCP a local file to the remote host.
 *   local_path  -- path to the source file on disk
 *   remote_path -- absolute destination path on the server
 *
 * Returns SHUSH_OK or a negative shush_err_t value.
 */
int shush_scp_send(shush_ctx_t *ctx,
                   const char *local_path, const char *remote_path);

/**
 * SCP a file from the remote host to local disk.
 *   remote_path -- absolute source path on the server
 *   local_path  -- destination path on local disk
 *
 * On success the file at local_path is complete and closed.
 * On any failure after the local file has been opened, it is deleted
 * before returning so the caller never sees a silently partial file.
 * Returns SHUSH_ERR_IO if the channel closes before all bytes are
 * received (truncated transfer).
 *
 * Returns SHUSH_OK or a negative shush_err_t value.
 */
int shush_scp_recv(shush_ctx_t *ctx,
                   const char *remote_path, const char *local_path);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Human-readable description of an error code. Never returns NULL. */
const char *shush_strerror(int err_code);

#ifdef __cplusplus
}
#endif

#endif /* SHUSH_COPY_H */
