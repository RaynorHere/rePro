/*
 * main.cpp  --  shushCopy CLI demo (C++)
 *
 * Usage:
 *   shushcpp config
 *   shushcpp exec  <command>
 *   shushcpp send  <local>  <remote>
 *   shushcpp recv  <remote> <local>
 */

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "shush_copy.hpp"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <windows.h>
#endif

// ------------------------------------------------------------------ //
// Config file support  (config/shush.cfg)                            //
// ------------------------------------------------------------------ //

struct Config {
    std::string host;
    int         port = 0;
    std::string user;
    std::string password;
    std::string vm_start;
    std::string vm_stop;
};

static std::optional<std::filesystem::path> find_vmappdefs()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, buf, MAX_PATH)) return std::nullopt;
    auto dir = std::filesystem::path(buf).parent_path();
#else
    auto dir = std::filesystem::current_path();
#endif
    for (int depth = 0; depth < 6; ++depth) {
        auto candidate = dir / "config";
        if (std::filesystem::is_directory(candidate))
            return candidate;
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

static std::optional<Config> load_config()
{
    auto vmappdefs = find_vmappdefs();
    if (!vmappdefs) {
        std::cerr << "shush: cannot locate config/ directory.\n"
                  << "  Run shush from the repository root or its build output.\n";
        return std::nullopt;
    }
    auto cfg_path = *vmappdefs / "shush.cfg";

    std::ifstream f(cfg_path);
    if (!f) {
        std::cerr << "shush: no config found at " << cfg_path << '\n'
                  << "  Run 'shush config' to set up connection details.\n";
        return std::nullopt;
    }

    Config cfg;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        if      (key == "host")     cfg.host     = val;
        else if (key == "port")     cfg.port     = std::atoi(val.c_str());
        else if (key == "user")     cfg.user     = val;
        else if (key == "password") cfg.password = val;
        else if (key == "vm_start") cfg.vm_start = val;
        else if (key == "vm_stop")  cfg.vm_stop  = val;
    }

    if (cfg.host.empty() || !cfg.port || cfg.user.empty() || cfg.password.empty()) {
        std::cerr << "shush: config at " << cfg_path << " is incomplete.\n"
                  << "  Run 'shush config' to reconfigure.\n";
        return std::nullopt;
    }
    return cfg;
}

#ifdef _WIN32
static std::string read_password()
{
    HANDLE  h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD   mode_old, nread;
    wchar_t wbuf[512] = {};

    GetConsoleMode(h, &mode_old);
    SetConsoleMode(h, mode_old & ~ENABLE_ECHO_INPUT);

    ReadConsoleW(h, wbuf, static_cast<DWORD>(sizeof(wbuf) / sizeof(wchar_t) - 1), &nread, nullptr);

    SetConsoleMode(h, mode_old);
    std::cout << '\n';

    while (nread > 0 && (wbuf[nread - 1] == L'\r' || wbuf[nread - 1] == L'\n'))
        wbuf[--nread] = L'\0';
    wbuf[nread] = L'\0';

    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
    std::string result(needed > 0 ? needed - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, result.data(), needed, nullptr, nullptr);
    SecureZeroMemory(wbuf, sizeof(wbuf));
    return result;
}
#endif

static std::string prompt_default(const std::string &label, const std::string &def)
{
    std::cout << "  " << label << " [" << def << "]: " << std::flush;
    std::string input;
    std::getline(std::cin, input);
    return input.empty() ? def : input;
}

static int run_config()
{
    auto vmappdefs = find_vmappdefs();
    if (!vmappdefs) {
        std::cerr << "shush config: cannot locate config/ directory.\n"
                  << "  Run shush from the repository root or its build output.\n";
        return 1;
    }
    auto cfg_path = *vmappdefs / "shush.cfg";

    std::cout << "shush config -- enter VM connection details\n";

    auto host     = prompt_default("Host", "127.0.0.1");
    auto port_str = prompt_default("Port", "9998");
    int  port     = std::atoi(port_str.c_str());
    if (port <= 0) {
        std::cerr << "shush config: invalid port '" << port_str << "'\n";
        return 1;
    }
    auto user = prompt_default("User", "user");

    std::cout << "  Password: " << std::flush;
#ifdef _WIN32
    auto password = read_password();
#else
    std::string password;
    std::getline(std::cin, password);
#endif
    if (password.empty()) {
        std::cerr << "shush config: password must not be empty\n";
        return 1;
    }

    std::ofstream f(cfg_path);
    if (!f) {
        std::cerr << "shush config: cannot write to " << cfg_path << '\n';
        return 1;
    }
    f << "host="     << host << '\n'
      << "port="     << port << '\n'
      << "user="     << user << '\n'
      << "password=" << password << '\n';

    std::fill(password.begin(), password.end(), '\0');

    std::cout << "  Saved to " << cfg_path << '\n';
    return 0;
}

static void usage(const char *prog)
{
    std::cerr
        << "Usage:\n"
        << "  " << prog << " config\n"
        << "  " << prog << " exec  <command>\n"
        << "  " << prog << " send  <local>  <remote>\n"
        << "  " << prog << " recv  <remote> <local>\n"
        << "  " << prog << " spawn\n"
        << "  " << prog << " kill\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const std::string op{argv[1]};

    if (op == "help") {
        usage(argv[0]);
        return 0;
    }

    if (op == "config") {
        return run_config();
    }

    /* spawn and kill load config for vm_start/vm_stop commands */
    if (op == "spawn" || op == "kill") {
#ifdef _WIN32
        auto cfg_opt = load_config();
        if (!cfg_opt) return 1;
        try {
            if (op == "spawn") {
                bool launched = shush::spawn_vm((*cfg_opt).vm_start, (*cfg_opt).port);
                std::cout << (launched ? "VM ready." : "VM is already running.") << '\n';
            } else {
                bool was_running = shush::kill_vm((*cfg_opt).vm_stop, (*cfg_opt).port);
                std::cout << (was_running ? "VM stopped." : "No VM was running.") << '\n';
            }
        } catch (const shush::ShushError &e) {
            std::cerr << "shush: " << e.what() << '\n';
            return 1;
        }
        return 0;
#else
        std::cerr << "shush: " << op << " is only supported on Windows\n";
        return 1;
#endif
    }

    /* Validate operation-specific arg counts before loading config. */
    if (op == "exec") {
        if (argc < 3) { std::cerr << "shush: exec requires <command>\n"; usage(argv[0]); return 1; }
    } else if (op == "send" || op == "recv") {
        if (argc < 4) { std::cerr << "shush: " << op << " requires <local> <remote>\n"; usage(argv[0]); return 1; }
    } else {
        std::cerr << "shush: unknown operation '" << op << "'\n"; usage(argv[0]); return 1;
    }

    auto cfg_opt = load_config();
    if (!cfg_opt) return 1;
    Config cfg = std::move(*cfg_opt);

    int exit_status = 0;

    try {
        shush::Connection conn{cfg.host, cfg.port, cfg.user, cfg.password};

        /* Zero the password once the connection is open. */
        std::fill(cfg.password.begin(), cfg.password.end(), '\0');

        if (op == "exec") {
            int remote_rc = -1;
            std::cout << conn.exec(argv[2], &remote_rc);
            if (remote_rc != 0)
                std::cerr << "shush exec: remote process exited with code "
                          << remote_rc << '\n';
            exit_status = remote_rc;

        } else if (op == "send") {
            conn.scp_send(argv[2], argv[3]);

        } else if (op == "recv") {
            conn.scp_recv(argv[2], argv[3]);
        }

    } catch (const shush::ShushError &e) {
        std::cerr << "shush: " << e.what() << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "shush: unexpected error: " << e.what() << '\n';
        return 1;
    }

    return exit_status;
}
