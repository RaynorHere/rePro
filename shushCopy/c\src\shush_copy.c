/*
 * shush_copy.c  --  shushCopy C implementation
 *
 * Depends on:  libssh2  (https://www.libssh2.org/)
 * Portable:    Windows (Winsock2) and POSIX (BSD sockets)
 */

/* ------------------------------------------------------------------ */
/* Platform socket shim                                                */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET            shush_sock_t;
#  define SHUSH_BAD_SOCK     INVALID_SOCKET
#  define shush_closesock(s) closesocket(s)
   /* stat with 64-bit file sizes on Windows */
#  include <sys/stat.h>
   typedef struct __stat64   shush_stat_t;
#  define shush_stat(p, s)   _stat64((p), (s))
   /* MSVC does not define ssize_t; BaseTsd.h is pulled in transitively
    * by winsock2.h which provides SSIZE_T.                             */
   typedef SSIZE_T           ssize_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
   typedef int               shush_sock_t;
#  define SHUSH_BAD_SOCK     (-1)
#  define shush_closesock(s) close(s)
#  include <sys/stat.h>
   typedef struct stat       shush_stat_t;
#  define shush_stat(p, s)   stat((p), (s))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libssh2.h>
#include "shush_copy.h"

/* ------------------------------------------------------------------ */
/* Internal context                                                    */
/* ------------------------------------------------------------------ */

struct shush_ctx {
    shush_sock_t     sock;
    LIBSSH2_SESSION *session;
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/** Open a TCP connection to host:port.  Returns SHUSH_BAD_SOCK on error. */
static shush_sock_t tcp_connect(const char *host, int port)
{
    struct addrinfo  hints;
    struct addrinfo *results = NULL;
    struct addrinfo *rp;
    shush_sock_t     sock = SHUSH_BAD_SOCK;
    char             port_str[8];

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof port_str, "%d", port);

    if (getaddrinfo(host, port_str, &hints, &results) != 0)
        return SHUSH_BAD_SOCK;

    for (rp = results; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == SHUSH_BAD_SOCK)
            continue;
        if (connect(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0)
            break;          /* success */
        shush_closesock(sock);
        sock = SHUSH_BAD_SOCK;
    }

    freeaddrinfo(results);
    return sock;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

shush_ctx_t *shush_connect(const char *host, int port,
                            const char *user, const char *password,
                            long timeout_ms,
                            shush_err_t *err)
{
    shush_ctx_t *ctx = NULL;
    /* e is declared without an initial value because every code path that
     * reaches fail_early assigns it explicitly before the goto.  The
     * initialiser below is intentionally left commented out: cppcheck flags
     * it as an unread variable (the initial SHUSH_OK is always overwritten
     * before use), and the compiler will warn if a future code path ever
     * reaches fail_early without setting e first -- which is the safety net
     * we actually want.
     *
     * Restore the initialiser if a code path is added that can reach
     * fail_early without a prior assignment, e.g.:
     *   if (some_new_check_that_jumps_directly) goto fail_early;
     *                                                     ^-- e would be uninitialised
     * In that case: shush_err_t e = SHUSH_OK;             */
    shush_err_t  e;
    int          rc;

    if (err) *err = SHUSH_OK;

    if (!host || !user || !password || port <= 0 || port > 65535) {
        e = SHUSH_ERR_PARAM;
        goto fail_early;
    }

#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            e = SHUSH_ERR_INIT;
            goto fail_early;
        }
    }
#endif

    if (libssh2_init(0) != 0) {
        e = SHUSH_ERR_INIT;
        goto fail_wsa;
    }

    ctx = (shush_ctx_t *)calloc(1, sizeof *ctx);
    if (!ctx) {
        e = SHUSH_ERR_INIT;
        goto fail_init;
    }

    ctx->sock = tcp_connect(host, port);
    if (ctx->sock == SHUSH_BAD_SOCK) {
        e = SHUSH_ERR_SOCKET;
        goto fail_sock;
    }

    ctx->session = libssh2_session_init();
    if (!ctx->session) {
        e = SHUSH_ERR_INIT;
        goto fail_session;
    }

    libssh2_session_set_blocking(ctx->session, 1);

    /* Apply the session-level timeout before any blocking SSH operations. */
    if (timeout_ms > 0)
        libssh2_session_set_timeout(ctx->session, timeout_ms);

    rc = libssh2_session_handshake(ctx->session, ctx->sock);
    if (rc != 0) {
        e = (rc == LIBSSH2_ERROR_TIMEOUT) ? SHUSH_ERR_TIMEOUT : SHUSH_ERR_HANDSHAKE;
        goto fail_handshake;
    }

    rc = libssh2_userauth_password(ctx->session, user, password);
    if (rc != 0) {
        e = (rc == LIBSSH2_ERROR_TIMEOUT) ? SHUSH_ERR_TIMEOUT : SHUSH_ERR_AUTH;
        goto fail_handshake;
    }

    return ctx;  /* success -- *err already set to SHUSH_OK */

    /* --- cleanup ladder (each label falls through to the next) --- */
fail_handshake:
    libssh2_session_disconnect(ctx->session, "bye");
    libssh2_session_free(ctx->session);
fail_session:
    shush_closesock(ctx->sock);
fail_sock:
    free(ctx);
fail_init:
    libssh2_exit();
fail_wsa:
#ifdef _WIN32
    WSACleanup();
#endif
fail_early:
    if (err) *err = e;
    return NULL;
}

/* ------------------------------------------------------------------ */

void shush_disconnect(shush_ctx_t *ctx)
{
    if (!ctx)
        return;

    libssh2_session_disconnect(ctx->session, "Normal shutdown");
    libssh2_session_free(ctx->session);
    shush_closesock(ctx->sock);
    free(ctx);

    libssh2_exit();
#ifdef _WIN32
    WSACleanup();
#endif
}

/* ==================================================================
 * ALTERNATIVE: process-lifetime library init / shutdown
 *
 * WHY THIS EXISTS
 * ---------------
 * shush_connect() calls libssh2_init(0) and (on Windows) WSAStartup()
 * at the start of each connection.  shush_disconnect() calls the
 * matching libssh2_exit() / WSACleanup() at teardown.  One connection
 * at a time: harmless.  Two or more connections overlapping: danger.
 *
 * The exact failure path:
 *   Connection A and connection B are both live.
 *   shush_disconnect(A) runs -- calls libssh2_exit() and WSACleanup().
 *   libssh2_exit() releases process-wide crypto state.
 *   WSACleanup() decrements the Winsock reference count; if it hits
 *   zero, Winsock tears down and invalidates every open socket in the
 *   process -- including B's.
 *   Any subsequent use of B is undefined behaviour.
 *
 * libssh2 maintains an internal reference count across multiple
 * init/exit pairs, so an immediate crash isn't guaranteed -- but
 * relying on an undocumented implementation detail is fragile, and
 * the Winsock reference-count behaviour is OS-controlled, not
 * something libssh2 can protect against.
 *
 * HOW TO SWITCH
 * ---------------
 * 1. Uncomment shush_library_init / shush_library_shutdown below
 *    (and their declarations in shush_copy.h).
 * 2. Call shush_library_init() once at process startup; check the
 *    return value before creating any connections.
 * 3. In shush_connect():
 *      - Remove the #ifdef _WIN32 / WSAStartup block.
 *      - Remove the libssh2_init(0) call and its fail_wsa / fail_init
 *        cleanup labels.
 * 4. In shush_disconnect():
 *      - Remove the libssh2_exit() call.
 *      - Remove the #ifdef _WIN32 / WSACleanup block.
 * 5. Call shush_library_shutdown() at process exit (atexit() is fine).
 *
 * See README.md §"Initialization design" for the full rationale.
 * ==================================================================
 */

/*
static int g_shush_lib_init = 0;

shush_err_t shush_library_init(void)
{
    if (g_shush_lib_init)
        return SHUSH_OK;

#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return SHUSH_ERR_INIT;
    }
#endif

    if (libssh2_init(0) != 0) {
#ifdef _WIN32
        WSACleanup();
#endif
        return SHUSH_ERR_INIT;
    }

    g_shush_lib_init = 1;
    return SHUSH_OK;
}

void shush_library_shutdown(void)
{
    if (!g_shush_lib_init)
        return;

    libssh2_exit();
#ifdef _WIN32
    WSACleanup();
#endif
    g_shush_lib_init = 0;
}
*/

/* ------------------------------------------------------------------ */

int shush_exec(shush_ctx_t *ctx, const char *command,
               char *buf, size_t buf_len,
               int *exit_code)
{
    LIBSSH2_CHANNEL *ch;
    size_t           total = 0;
    ssize_t          n;

    /* Pre-set sentinel so any early error return leaves a defined value. */
    if (exit_code != NULL)
        *exit_code = -1;

    if (!ctx || !command || !buf || buf_len == 0)
        return SHUSH_ERR_PARAM;

    ch = libssh2_channel_open_session(ctx->session);
    if (!ch)
        return SHUSH_ERR_CHAN;

    if (libssh2_channel_exec(ch, command) != 0) {
        libssh2_channel_free(ch);
        return SHUSH_ERR_CHAN;
    }

    /* Drain stdout; leave room for the NUL terminator. */
    while (total + 1 < buf_len) {
        n = libssh2_channel_read(ch, buf + total, buf_len - total - 1);
        if (n == LIBSSH2_ERROR_TIMEOUT) {
            buf[total] = '\0';
            libssh2_channel_free(ch);
            return SHUSH_ERR_TIMEOUT;
        }
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    buf[total] = '\0';

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);

    /* Capture the remote process exit status before freeing the channel.
     * Must be called after wait_closed() -- the status is not available
     * until the server sends the exit-status message on channel close. */
    if (exit_code != NULL)
        *exit_code = libssh2_channel_get_exit_status(ch);

    libssh2_channel_free(ch);
    return SHUSH_OK;
}

/* ------------------------------------------------------------------ */

int shush_scp_send(shush_ctx_t *ctx,
                   const char *local_path, const char *remote_path)
{
    shush_stat_t     st;
    FILE            *fp;
    LIBSSH2_CHANNEL *ch;
    char             buf[16384];
    size_t           n;
    ssize_t          sent;

    if (!ctx || !local_path || !remote_path)
        return SHUSH_ERR_PARAM;

    if (shush_stat(local_path, &st) != 0)
        return SHUSH_ERR_IO;

    fp = fopen(local_path, "rb");
    if (!fp)
        return SHUSH_ERR_IO;

    ch = libssh2_scp_send64(ctx->session,
                             remote_path,
                             (int)(st.st_mode & 0777),
                             (libssh2_uint64_t)st.st_size,
                             0, 0);
    if (!ch) {
        int last_err = libssh2_session_last_errno(ctx->session);
        fclose(fp);
        return (last_err == LIBSSH2_ERROR_TIMEOUT) ? SHUSH_ERR_TIMEOUT : SHUSH_ERR_SCP;
    }

    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
        char  *ptr       = buf;
        size_t remaining = n;

        while (remaining > 0) {
            sent = libssh2_channel_write(ch, ptr, remaining);
            if (sent == LIBSSH2_ERROR_TIMEOUT) {
                libssh2_channel_free(ch);
                fclose(fp);
                return SHUSH_ERR_TIMEOUT;
            }
            if (sent <= 0) {
                libssh2_channel_free(ch);
                fclose(fp);
                return SHUSH_ERR_IO;
            }
            ptr       += sent;
            remaining -= (size_t)sent;
        }
    }

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    fclose(fp);
    return SHUSH_OK;
}

/* ------------------------------------------------------------------ */

int shush_scp_recv(shush_ctx_t *ctx,
                   const char *remote_path, const char *local_path)
{
    LIBSSH2_CHANNEL      *ch;
    libssh2_struct_stat   fileinfo;
    libssh2_int64_t       remaining;
    FILE                 *fp;
    char                  buf[16384];
    ssize_t               n;

    if (!ctx || !remote_path || !local_path)
        return SHUSH_ERR_PARAM;

    ch = libssh2_scp_recv2(ctx->session, remote_path, &fileinfo);
    if (!ch) {
        int last_err = libssh2_session_last_errno(ctx->session);
        return (last_err == LIBSSH2_ERROR_TIMEOUT) ? SHUSH_ERR_TIMEOUT : SHUSH_ERR_SCP;
    }

    fp = fopen(local_path, "wb");
    if (!fp) {
        libssh2_channel_free(ch);
        return SHUSH_ERR_IO;
    }

    remaining = (libssh2_int64_t)fileinfo.st_size;

    while (remaining > 0) {
        size_t  to_read = ((libssh2_int64_t)sizeof buf < remaining)
                            ? sizeof buf : (size_t)remaining;
        n = libssh2_channel_read(ch, buf, to_read);
        if (n == LIBSSH2_ERROR_TIMEOUT) {
            fclose(fp);
            remove(local_path);
            libssh2_channel_free(ch);
            return SHUSH_ERR_TIMEOUT;
        }
        if (n <= 0)
            break;
        if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
            fclose(fp);
            remove(local_path);
            libssh2_channel_free(ch);
            return SHUSH_ERR_IO;
        }
        remaining -= (libssh2_int64_t)n;
    }

    fclose(fp);
    libssh2_channel_free(ch);

    /* If the loop exited with bytes still outstanding the server closed
     * the channel before the full file was delivered.  Delete the partial
     * file so the caller never encounters a silently incomplete result. */
    if (remaining > 0) {
        remove(local_path);
        return SHUSH_ERR_IO;
    }

    return SHUSH_OK;
}

/* ------------------------------------------------------------------ */

const char *shush_strerror(int err_code)
{
    switch ((shush_err_t)err_code) {
    case SHUSH_OK:            return "success";
    case SHUSH_ERR_PARAM:     return "invalid parameter";
    case SHUSH_ERR_SOCKET:    return "TCP connect failed";
    case SHUSH_ERR_INIT:      return "libssh2 / Winsock initialisation failed";
    case SHUSH_ERR_HANDSHAKE: return "SSH handshake failed";
    case SHUSH_ERR_AUTH:      return "SSH authentication rejected";
    case SHUSH_ERR_TIMEOUT:   return "operation timed out";
    case SHUSH_ERR_CHAN:      return "SSH channel error";
    case SHUSH_ERR_SCP:       return "SCP channel error";
    case SHUSH_ERR_IO:        return "local file I/O error";
    default:                  return "unknown error";
    }
}
