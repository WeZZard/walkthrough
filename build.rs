use std::env;
use std::path::PathBuf;

fn main() {
    // Initialize third-party dependencies if needed
    initialize_third_parties();
    
    // Set up environment variables for sub-crates
    setup_build_environment();
    
    // Print helpful message about compile_commands.json location
    if let Ok(target_dir) = env::var("CARGO_TARGET_DIR") {
        println!("cargo:info=compile_commands.json will be generated in: {}/compile_commands.json", target_dir);
    } else {
        println!("cargo:info=compile_commands.json will be generated in: target/compile_commands.json");
    }
    
    println!("cargo:info=For IDE integration, point your C/C++ extension to this file");
    
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=third_parties/");
}

fn initialize_third_parties() {
    let project_root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let third_parties_dir = project_root.join("third_parties");
    
    // Check if Frida SDKs are initialized
    let frida_core = third_parties_dir.join("frida-core");
    let frida_gum = third_parties_dir.join("frida-gum");
    
    if !frida_core.exists() || !frida_gum.exists() {
        println!("cargo:warning=Frida SDKs not initialized. Run ./utils/init_third_parties.sh first");
        println!("cargo:warning=Building without Frida support");
    } else {
        // Export Frida paths for sub-crates to use
        println!("cargo:rustc-env=FRIDA_CORE_DIR={}", frida_core.display());
        println!("cargo:rustc-env=FRIDA_GUM_DIR={}", frida_gum.display());
    }
}

fn setup_build_environment() {
    // Set up any global build environment variables
    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    println!("cargo:rustc-env=BUILD_PROFILE={}", profile);
    
    // Ensure target directories exist for predictable build outputs
    let target_dir = PathBuf::from(env::var("CARGO_TARGET_DIR").unwrap_or_else(|_| "target".to_string()));
    let profile_dir = target_dir.join(&profile);
    
    // Create predictable output directories for all components
    let tracer_backend_dir = profile_dir.join("tracer_backend");
    if !tracer_backend_dir.exists() {
        std::fs::create_dir_all(&tracer_backend_dir).ok();
    }
    
    // Export paths for sub-crates
    println!("cargo:rustc-env=PREDICTABLE_BUILD_DIR={}", profile_dir.display());
}