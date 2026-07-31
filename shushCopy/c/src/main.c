/*
 * main.c  --  shushCopy CLI demo (C)
 *
 * Usage:
 *   shush config
 *   shush exec  <command>
 *   shush send  <local>  <remote>
 *   shush recv  <remote> <local>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shush_copy.h"
#include "shush_vm.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Config file support  (config/shush.cfg)                             */
/* ------------------------------------------------------------------ */

/*
 * Connection config lives in config/shush.cfg.
 * Format: key=value, one entry per line.  The password never appears in
 * CLI args or shell history; it is written once by 'shush config' and
 * read silently at connect time.
 *
 * config/shush.cfg is listed in .gitignore and is never committed.
 */

typedef struct {
    char host[256];
    int  port;
    char user[256];
    char password[256];
    char vm_start[1024];
    char vm_stop[1024];
} shush_config_t;

#ifdef _WIN32

/* Remove the last path component from path (modifies in place).
 * Returns 0 if there is no parent to remove. */
static int _pop_dir(char *path)
{
    char *last = strrchr(path, '\\');
    if (!last || last == path) return 0;
    *last = '\0';
    return 1;
}

/* Locate the config/ directory by walking up from the executable.
 * Returns 1 and fills out on success, 0 if not found within 6 levels. */
static int _find_vmappdefs(char out[MAX_PATH])
{
    char  base[MAX_PATH];
    char  cand[MAX_PATH];
    DWORD attr;
    int   depth;

    if (!GetModuleFileNameA(NULL, base, MAX_PATH)) return 0;
    _pop_dir(base);   /* strip exe filename, leaving its directory */

    for (depth = 0; depth < 6; depth++) {
        _snprintf_s(cand, MAX_PATH, _TRUNCATE, "%s\\config", base);
        attr = GetFileAttributesA(cand);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            strcpy_s(out, MAX_PATH, cand);
            return 1;
        }
        if (!_pop_dir(base)) break;
    }
    return 0;
}

/* Read a line from the console with echo disabled.
 * Emits a newline after input.  buf is NUL-terminated on return. */
static void _read_password(char *buf, int bufsize)
{
    HANDLE  h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD   mode_old, nread;
    wchar_t wbuf[512];

    GetConsoleMode(h, &mode_old);
    SetConsoleMode(h, mode_old & ~ENABLE_ECHO_INPUT);

    nread = 0;
    ReadConsoleW(h, wbuf, (DWORD)((sizeof(wbuf) / sizeof(wchar_t)) - 1), &nread, NULL);

    SetConsoleMode(h, mode_old);
    putchar('\n');

    /* Strip trailing CR / LF */
    while (nread > 0 && (wbuf[nread - 1] == L'\r' || wbuf[nread - 1] == L'\n'))
        nread--;
    wbuf[nread] = L'\0';

    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, bufsize, NULL, NULL);
    SecureZeroMemory(wbuf, sizeof(wbuf));
}

#endif /* _WIN32 */

/* Parse one "key=value\n" line.  Everything after the first '=' is the
 * value; trailing CR/LF is stripped.  Returns 1 on success. */
static int _parse_kv(const char *line,
                     char *key,  int ksize,
                     char *val,  int vsize)
{
    const char *eq  = strchr(line, '=');
    int         klen, vlen;

    if (!eq) return 0;
    klen = (int)(eq - line);
    if (klen <= 0 || klen >= ksize) return 0;

    strncpy_s(key, ksize, line, klen);
    key[klen] = '\0';

    eq++;   /* skip '=' */
    vlen = (int)strlen(eq);
    while (vlen > 0 && (eq[vlen - 1] == '\r' || eq[vlen - 1] == '\n'))
        vlen--;
    if (vlen >= vsize) vlen = vsize - 1;
    strncpy_s(val, vsize, eq, vlen);
    val[vlen] = '\0';

    return 1;
}

/* Load config/shush.cfg into cfg.
 * Prints an error and returns 0 on any failure. */
static int load_config(shush_config_t *cfg)
{
#ifndef _WIN32
    fprintf(stderr, "shush: config file lookup is not supported on this platform.\n");
    return 0;
#else
    char vmappdefs[MAX_PATH];
    char cfg_path[MAX_PATH];
    char line[512], key[64], val[256];
    FILE *f;

    if (!_find_vmappdefs(vmappdefs)) {
        fprintf(stderr,
            "shush: cannot locate config/ directory.\n"
            "  Run shush from the repository root or its build output.\n");
        return 0;
    }
    _snprintf_s(cfg_path, MAX_PATH, _TRUNCATE, "%s\\shush.cfg", vmappdefs);

    if (fopen_s(&f, cfg_path, "r") != 0) {
        fprintf(stderr,
            "shush: no config found at %s\n"
            "  Run 'shush config' to set up connection details.\n", cfg_path);
        return 0;
    }

    memset(cfg, 0, sizeof(*cfg));
    while (fgets(line, sizeof(line), f)) {
        if (!_parse_kv(line, key, sizeof(key), val, sizeof(val))) continue;
        if      (strcmp(key, "host")     == 0) strcpy_s(cfg->host,     sizeof(cfg->host),     val);
        else if (strcmp(key, "port")     == 0) cfg->port = atoi(val);
        else if (strcmp(key, "user")     == 0) strcpy_s(cfg->user,     sizeof(cfg->user),     val);
        else if (strcmp(key, "password") == 0) strcpy_s(cfg->password, sizeof(cfg->password), val);
        else if (strcmp(key, "vm_start") == 0) strcpy_s(cfg->vm_start, sizeof(cfg->vm_start), val);
        else if (strcmp(key, "vm_stop")  == 0) strcpy_s(cfg->vm_stop,  sizeof(cfg->vm_stop),  val);
    }
    fclose(f);

    if (!cfg->host[0] || !cfg->port || !cfg->user[0] || !cfg->password[0]) {
        fprintf(stderr,
            "shush: config at %s is incomplete.\n"
            "  Run 'shush config' to reconfigure.\n", cfg_path);
        return 0;
    }
    return 1;
#endif /* _WIN32 */
}

/* Interactive config setup.  Prompts for all four values; password with
 * echo disabled so it never appears on screen.  Returns 0 on success. */
static int run_config(void)
{
#ifndef _WIN32
    fprintf(stderr, "shush config: interactive config is not supported on this platform.\n");
    return 1;
#else
    char vmappdefs[MAX_PATH];
    char cfg_path[MAX_PATH];
    char host[256], port_str[16], user[256], password[256];
    int  port;
    FILE *f;

    if (!_find_vmappdefs(vmappdefs)) {
        fprintf(stderr,
            "shush config: cannot locate config/ directory.\n"
            "  Run shush from the repository root or its build output.\n");
        return 1;
    }
    _snprintf_s(cfg_path, MAX_PATH, _TRUNCATE, "%s\\shush.cfg", vmappdefs);

    printf("shush config -- enter VM connection details\n");

    printf("  Host [127.0.0.1]: "); fflush(stdout);
    fgets(host, sizeof(host), stdin);
    host[strcspn(host, "\r\n")] = '\0';
    if (!host[0]) strcpy_s(host, sizeof(host), "127.0.0.1");

    printf("  Port [9998]: "); fflush(stdout);
    fgets(port_str, sizeof(port_str), stdin);
    port_str[strcspn(port_str, "\r\n")] = '\0';
    port = port_str[0] ? atoi(port_str) : 9998;
    if (port <= 0) {
        fprintf(stderr, "shush config: invalid port '%s'\n", port_str);
        return 1;
    }

    printf("  User [user]: "); fflush(stdout);
    fgets(user, sizeof(user), stdin);
    user[strcspn(user, "\r\n")] = '\0';
    if (!user[0]) strcpy_s(user, sizeof(user), "user");

    printf("  Password: "); fflush(stdout);
    _read_password(password, sizeof(password));
    if (!password[0]) {
        fprintf(stderr, "shush config: password must not be empty\n");
        return 1;
    }

    if (fopen_s(&f, cfg_path, "w") != 0) {
        fprintf(stderr, "shush config: cannot write to %s\n", cfg_path);
        SecureZeroMemory(password, sizeof(password));
        return 1;
    }
    {
        char vm_start[1024] = "";
        char vm_stop[1024]  = "";
        printf("  VM start command [VBoxManage startvm MyVM --type headless]: "); fflush(stdout);
        fgets(vm_start, sizeof(vm_start), stdin);
        vm_start[strcspn(vm_start, "\r\n")] = '\0';
        if (!vm_start[0]) strcpy_s(vm_start, sizeof(vm_start), "VBoxManage startvm MyVM --type headless");

        printf("  VM stop command  [VBoxManage controlvm MyVM poweroff]: "); fflush(stdout);
        fgets(vm_stop, sizeof(vm_stop), stdin);
        vm_stop[strcspn(vm_stop, "\r\n")] = '\0';
        if (!vm_stop[0]) strcpy_s(vm_stop, sizeof(vm_stop), "VBoxManage controlvm MyVM poweroff");

        fprintf(f, "host=%s\nport=%d\nuser=%s\npassword=%s\nvm_start=%s\nvm_stop=%s\n",
                host, port, user, password, vm_start, vm_stop);
    }
    fclose(f);
    SecureZeroMemory(password, sizeof(password));

    printf("  Saved to %s\n", cfg_path);
    return 0;
#endif /* _WIN32 */
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s config\n"
        "  %s exec  <command>\n"
        "  %s send  <local>  <remote>\n"
        "  %s recv  <remote> <local>\n"
        "  %s spawn\n"
        "  %s kill\n",
        prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    shush_config_t  cfg;
    shush_ctx_t    *ctx;
    const char     *op;
    shush_err_t     conn_err = SHUSH_OK;
    int             rc       = SHUSH_OK;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    op = argv[1];

    /* help: print usage and exit cleanly */
    if (strcmp(op, "help") == 0) {
        usage(argv[0]);
        return 0;
    }

    /* config: interactive credential setup */
    if (strcmp(op, "config") == 0) {
        return run_config();
    }

    /* spawn and kill load config for vm_start/vm_stop commands */
#ifdef _WIN32
    if (strcmp(op, "spawn") == 0 || strcmp(op, "kill") == 0) {
        if (!load_config(&cfg)) return 1;
        if (strcmp(op, "spawn") == 0) {
            int r = shush_spawn_vm(cfg.vm_start, cfg.port, 10000);
            if (r < 0) return 1;
            puts(r == 1 ? "VM ready." : "VM is already running.");
        } else {
            int r = shush_kill_vm(cfg.vm_stop, cfg.port);
            if (r < 0) return 1;
            puts(r == 1 ? "VM stopped." : "No VM was running.");
        }
        return 0;
    }
#else
    if (strcmp(op, "spawn") == 0 || strcmp(op, "kill") == 0) {
        fprintf(stderr, "shush: %s is only supported on Windows\n", op);
        return 1;
    }
#endif

    /* Validate operation-specific arg counts before loading config. */
    if (strcmp(op, "exec") == 0) {
        if (argc < 3) {
            fprintf(stderr, "shush: exec requires <command>\n");
            usage(argv[0]); return 1;
        }
    } else if (strcmp(op, "send") == 0 || strcmp(op, "recv") == 0) {
        if (argc < 4) {
            fprintf(stderr, "shush: %s requires <local> <remote>\n", op);
            usage(argv[0]); return 1;
        }
    } else {
        fprintf(stderr, "shush: unknown operation '%s'\n", op);
        usage(argv[0]); return 1;
    }

    if (!load_config(&cfg)) return 1;

    ctx = shush_connect(cfg.host, cfg.port, cfg.user, cfg.password, 10000L, &conn_err);
#ifdef _WIN32
    SecureZeroMemory(cfg.password, sizeof(cfg.password));
#else
    memset(cfg.password, 0, sizeof(cfg.password));
#endif
    if (!ctx) {
        fprintf(stderr, "shush: connection to %s:%d failed: %s\n",
                cfg.host, cfg.port, shush_strerror(conn_err));
        return 1;
    }

    if (strcmp(op, "exec") == 0) {
        char output[65536];
        int  remote_rc = -1;
        rc = shush_exec(ctx, argv[2], output, sizeof output, &remote_rc);
        if (rc == SHUSH_OK) {
            fputs(output, stdout);
            if (remote_rc != 0)
                fprintf(stderr, "shush exec: remote process exited with code %d\n", remote_rc);
            rc = remote_rc;   /* propagate remote exit code as our own */
        } else {
            fprintf(stderr, "shush exec: %s\n", shush_strerror(rc));
            rc = 1;
        }

    } else if (strcmp(op, "send") == 0) {
        rc = shush_scp_send(ctx, argv[2], argv[3]);
        if (rc != SHUSH_OK) {
            fprintf(stderr, "shush send: %s\n", shush_strerror(rc));
            rc = 1;
        }

    } else if (strcmp(op, "recv") == 0) {
        rc = shush_scp_recv(ctx, argv[2], argv[3]);
        if (rc != SHUSH_OK) {
            fprintf(stderr, "shush recv: %s\n", shush_strerror(rc));
            rc = 1;
        }
    }

    shush_disconnect(ctx);
    return rc;
}
