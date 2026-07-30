// build.rs -- shushCopy Rust port
//
// When linking against pre-built libssh2 + OpenSSL from vcpkg, the Rust
// linker needs to be told about the Windows system libraries that OpenSSL's
// CAPI engine requires.  libssh2-sys emits the core libs but misses these
// when it picks up a pre-built (non-source) vcpkg install.
//
// These are the same libs listed in build/deps.props for the C/C++ builds.

fn main() {
    if cfg!(target_os = "windows") {
        println!("cargo:rustc-link-lib=crypt32");
        println!("cargo:rustc-link-lib=advapi32");
        println!("cargo:rustc-link-lib=user32");
        println!("cargo:rustc-link-lib=bcrypt");
    }
}
