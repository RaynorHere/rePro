//! shushCopy CLI demo (Rust)
//!
//! Usage:
//!   shush_rs config
//!   shush_rs exec  <command>
//!   shush_rs send  <local>  <remote>
//!   shush_rs recv  <remote> <local>

use shush_copy::ShushConn;
use std::io::{self, Write};
use std::path::Path;
use std::process;

// ------------------------------------------------------------------ //
// Config file support  (config/shush.cfg)                            //
// ------------------------------------------------------------------ //

struct Config {
    host:     String,
    port:     u16,
    user:     String,
    password: String,
    vm_start: String,
    vm_stop:  String,
}

fn find_vmappdefs() -> Option<std::path::PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let mut dir = exe.parent()?.to_path_buf();
    for _ in 0..6 {
        let candidate = dir.join("config");
        if candidate.is_dir() {
            return Some(candidate);
        }
        let parent = dir.parent()?.to_path_buf();
        if parent == dir { break; }
        dir = parent;
    }
    None
}

fn load_config() -> Result<Config, String> {
    let vmappdefs = find_vmappdefs().ok_or_else(|| {
        "cannot locate config/ directory.\n  Run shush from the repository root or its build output."
            .to_string()
    })?;
    let cfg_path = vmappdefs.join("shush.cfg");

    if !cfg_path.exists() {
        return Err(format!(
            "no config found at {}.\n  Run 'shush config' to set up connection details.",
            cfg_path.display()
        ));
    }

    let content = std::fs::read_to_string(&cfg_path)
        .map_err(|e| format!("cannot read config: {}", e))?;

    let mut host     = String::new();
    let mut port_str = String::new();
    let mut user     = String::new();
    let mut password = String::new();
    let mut vm_start = String::new();
    let mut vm_stop  = String::new();

    for line in content.lines() {
        if line.trim_start().starts_with('#') { continue; }
        if let Some((key, val)) = line.split_once('=') {
            match key.trim() {
                "host"     => host     = val.to_string(),
                "port"     => port_str = val.to_string(),
                "user"     => user     = val.to_string(),
                "password" => password = val.to_string(),
                "vm_start" => vm_start = val.to_string(),
                "vm_stop"  => vm_stop  = val.to_string(),
                _          => {}
            }
        }
    }

    if host.is_empty() || port_str.is_empty() || user.is_empty() || password.is_empty() {
        return Err(format!(
            "config at {} is incomplete.\n  Run 'shush config' to reconfigure.",
            cfg_path.display()
        ));
    }

    let port: u16 = port_str.trim().parse().map_err(|_| format!(
        "config has invalid port '{}'.\n  Run 'shush config' to reconfigure.",
        port_str
    ))?;

    Ok(Config { host, port, user, password, vm_start, vm_stop })
}

fn prompt_default(label: &str, default: &str) -> String {
    print!("  {} [{}]: ", label, default);
    io::stdout().flush().ok();
    let mut input = String::new();
    io::stdin().read_line(&mut input).ok();
    let input = input.trim().to_string();
    if input.is_empty() { default.to_string() } else { input }
}

fn run_config() -> i32 {
    let vmappdefs = match find_vmappdefs() {
        Some(p) => p,
        None => {
            eprintln!("shush config: cannot locate config/ directory.\n  Run shush from the repository root or its build output.");
            return 1;
        }
    };
    let cfg_path = vmappdefs.join("shush.cfg");

    println!("shush config \u{2014} enter VM connection details (saved to config/shush.cfg)");

    let host     = prompt_default("Host", "127.0.0.1");
    let port_str = prompt_default("Port", "9998");
    let port: u16 = match port_str.trim().parse::<u16>() {
        Ok(p) if p > 0 => p,
        _ => {
            eprintln!("shush config: invalid port '{}'", port_str);
            return 1;
        }
    };
    let user = prompt_default("User", "user");

    let password = match rpassword::prompt_password("  Password: ") {
        Ok(p)  => p,
        Err(e) => { eprintln!("shush config: failed to read password: {}", e); return 1; }
    };
    if password.is_empty() {
        eprintln!("shush config: password must not be empty");
        return 1;
    }

    let vm_start = prompt_default("VM start command", "VBoxManage startvm MyVM --type headless");
    let vm_stop  = prompt_default("VM stop command",  "VBoxManage controlvm MyVM poweroff");

    let content = format!("host={}\nport={}\nuser={}\npassword={}\nvm_start={}\nvm_stop={}\n",
                          host, port, user, password, vm_start, vm_stop);
    drop(password);   // clear from memory before writing to disk

    match std::fs::write(&cfg_path, content) {
        Ok(()) => { println!("  Saved to {}", cfg_path.display()); 0 }
        Err(e) => { eprintln!("shush config: cannot write to {}: {}", cfg_path.display(), e); 1 }
    }
}

fn usage(prog: &str) {
    eprintln!(
        "Usage:\n  \
         {prog} config\n  \
         {prog} exec  <command>\n  \
         {prog} send  <local>  <remote>\n  \
         {prog} recv  <remote> <local>\n  \
         {prog} spawn\n  \
         {prog} kill"
    );
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let prog = &args[0];

    if args.len() < 2 {
        usage(prog);
        process::exit(1);
    }

    let op = &args[1];

    if op == "help" {
        usage(prog);
        process::exit(0);
    }

    if op == "config" {
        process::exit(run_config());
    }

    // spawn and kill load config for vm_start/vm_stop commands.
    if op == "spawn" || op == "kill" {
        #[cfg(target_os = "windows")]
        {
            let cfg = load_config().unwrap_or_else(|e| {
                eprintln!("shush: {}", e);
                process::exit(1);
            });
            match op.as_str() {
                "spawn" => {
                    match shush_copy::spawn_vm(&cfg.vm_start, cfg.port, 10_000) {
                        Ok(true)  => { println!("VM ready.");              process::exit(0); }
                        Ok(false) => { println!("VM is already running."); process::exit(0); }
                        Err(e)    => { eprintln!("shush: {}", e);         process::exit(1); }
                    }
                }
                _ => {
                    match shush_copy::kill_vm(&cfg.vm_stop, cfg.port) {
                        Ok(true)  => { println!("VM stopped.");        process::exit(0); }
                        Ok(false) => { println!("No VM was running."); process::exit(0); }
                        Err(e)    => { eprintln!("shush: {}", e);     process::exit(1); }
                    }
                }
            }
        }
        #[cfg(not(target_os = "windows"))]
        {
            eprintln!("shush: {} is only supported on Windows", op);
            process::exit(1);
        }
    }

    let rest = &args[2..];

    // Validate operation-specific arg counts before loading config.
    match op.as_str() {
        "exec" if rest.is_empty() => {
            eprintln!("shush: exec requires <command>");
            usage(prog); process::exit(1);
        }
        "send" | "recv" if rest.len() < 2 => {
            eprintln!("shush: {} requires <local> <remote>", op);
            usage(prog); process::exit(1);
        }
        "exec" | "send" | "recv" => {}
        _ => {
            eprintln!("shush: unknown operation '{}'", op);
            usage(prog); process::exit(1);
        }
    }

    let cfg = load_config().unwrap_or_else(|e| {
        eprintln!("shush: {}", e);
        process::exit(1);
    });

    let mut conn = ShushConn::connect(&cfg.host, cfg.port, &cfg.user, &cfg.password, 10_000)
        .unwrap_or_else(|e| {
            eprintln!("shush: connection to {}:{} failed: {}", cfg.host, cfg.port, e);
            process::exit(1);
        });

    // Drop the config (including the password String) once the connection is open.
    drop(cfg);

    let rc: i32 = match op.as_str() {
        "exec" => {
            match conn.exec(&rest[0]) {
                Ok((output, code)) => {
                    print!("{}", output);
                    if code != 0 {
                        eprintln!("shush exec: remote process exited with code {}", code);
                    }
                    code
                }
                Err(e) => { eprintln!("shush exec: {}", e); 1 }
            }
        }
        "send" => {
            match conn.scp_send(Path::new(&rest[0]), Path::new(&rest[1])) {
                Ok(()) => 0,
                Err(e) => { eprintln!("shush send: {}", e); 1 }
            }
        }
        "recv" => {
            match conn.scp_recv(Path::new(&rest[0]), Path::new(&rest[1])) {
                Ok(()) => 0,
                Err(e) => { eprintln!("shush recv: {}", e); 1 }
            }
        }
        _ => {
            eprintln!("shush: unknown operation '{}'", op);
            usage(prog);
            1
        }
    };

    process::exit(rc);
}
