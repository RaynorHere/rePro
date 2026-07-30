/*
 * shush_vm.c  --  shushCopy VM lifecycle  (Windows only)
 *
 * Runs user-supplied shell commands to start/stop a virtual machine and
 * polls 127.0.0.1:host_port for an SSH-2 banner to confirm readiness.
 * Any hypervisor whose start/stop can be expressed as a single command
 * (VirtualBox, QEMU, Hyper-V, â€¦) will work.
 *
 * Winsock is initialised and torn down inside each public function so that
 * shush_spawn_vm / shush_kill_vm can be called independently of ShushConn.
 */

#ifdef _WIN32

#include "shush_vm.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SSH_BANNER       "SSH-"
#define POLL_INTERVAL_MS  500u
#define STOP_GRACE_MS   10000u

/* ------------------------------------------------------------------ */
/* Network helpers                                                     */
/* ------------------------------------------------------------------ */

static int is_port_reachable(int port)
{
    SOCKET           s;
    struct sockaddr_in addr;
    u_long           nb = 1;
    fd_set           wset, eset;
    struct timeval   tv;
    int              ok;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ioctlsocket(s, FIONBIO, &nb);
    connect(s, (struct sockaddr *)&addr, sizeof(addr));

    FD_ZERO(&wset); FD_SET(s, &wset);
    FD_ZERO(&eset); FD_SET(s, &eset);
    tv.tv_sec = 0; tv.tv_usec = 500000;

    ok = (select(0, NULL, &wset, &eset, &tv) == 1 &&
          FD_ISSET(s, &wset) && !FD_ISSET(s, &eset));
    closesocket(s);
    return ok;
}

static int is_ssh_ready(int port)
{
    SOCKET           s;
    struct sockaddr_in addr;
    DWORD            tmo = 1000;
    char             buf[256];
    int              n, ok = 0;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return 0;

    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof(tmo));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        n = recv(s, buf, (int)sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            ok = (strncmp(buf, SSH_BANNER, strlen(SSH_BANNER)) == 0);
        }
    }
    closesocket(s);
    return ok;
}

static int wait_for_port_closed(int port, DWORD timeout_ms)
{
    DWORD start = GetTickCount();
    while (is_port_reachable(port)) {
        if (GetTickCount() - start >= timeout_ms) return 0;
        Sleep(POLL_INTERVAL_MS);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int shush_spawn_vm(const char *vm_start_cmd, int host_port, int timeout_ms)
{
    WSADATA wsa;
    DWORD   start_tick;
    int     r;

    if (!vm_start_cmd || !vm_start_cmd[0] || host_port <= 0)
        return SHUSH_ERR_PARAM;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return SHUSH_ERR_INIT;

    if (is_ssh_ready(host_port)) { r = 0; goto done; }

    system(vm_start_cmd);

    start_tick = GetTickCount();
    r = SHUSH_ERR_TIMEOUT;
    for (;;) {
        Sleep(POLL_INTERVAL_MS);
        if (is_ssh_ready(host_port)) { r = 1; break; }
        if (timeout_ms > 0 &&
            (int)(GetTickCount() - start_tick) >= timeout_ms) {
            fprintf(stderr,
                    "shush spawn: SSH on port %d did not respond within %d ms\n",
                    host_port, timeout_ms);
            break;
        }
    }

done:
    WSACleanup();
    return r;
}

int shush_kill_vm(const char *vm_stop_cmd, int host_port)
{
    WSADATA wsa;
    int     r;

    if (!vm_stop_cmd || !vm_stop_cmd[0] || host_port <= 0)
        return SHUSH_ERR_PARAM;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return SHUSH_ERR_INIT;

    if (!is_port_reachable(host_port)) { r = 0; goto done; }

    system(vm_stop_cmd);

    r = wait_for_port_closed(host_port, STOP_GRACE_MS) ? 1 : SHUSH_ERR_TIMEOUT;
    if (r < 0)
        fprintf(stderr, "shush kill: VM on port %d could not be stopped\n",
                host_port);

done:
    WSACleanup();
    return r;
}

#endif /* _WIN32 */
