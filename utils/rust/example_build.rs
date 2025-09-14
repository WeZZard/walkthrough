// Example build.rs for Rust components that need to link with tracer-backend
// This demonstrates how to build and link C/C++ components from Rust

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=../tracer-backend/");
    
    // Determine the output directory
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let profile = env::var("PROFILE").unwrap();
    
    // Build configuration
    let build_type = match profile.as_str() {
        "debug" => "Debug",
        "release" => "Release",
        _ => "Debug",
    };
    
    // Use cmake crate to build the C/C++ components
    let dst = cmake::Config::new("../tracer-backend")
        .define("CMAKE_BUILD_TYPE", build_type)
        .define("BUILD_TESTING", "OFF")  // Don't build tests during Rust build
        .build();
    
    // Link the native controller library
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=native_controller");
    
    // Platform-specific linking for Frida Core dependencies
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=framework=Foundation");
        println!("cargo:rustc-link-lib=framework=AppKit");
        println!("cargo:rustc-link-lib=bsm");
        println!("cargo:rustc-link-lib=resolv");
    } else if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
    }
    
    // Generate Rust bindings for C headers using bindgen
    let bindings = bindgen::Builder::default()
        .header("../tracer-backend/include/frida_controller.h")
        .header("../tracer-backend/include/shared_memory.h")
        .header("../tracer-backend/include/ring_buffer.h")
        // Tell bindgen where to find Frida headers
        .clang_arg("-I../third_parties/frida-core")
        .clang_arg("-I../third_parties/frida-gum")
        // Generate bindings for these types/functions
        .allowlist_type("FridaContext")
        .allowlist_type("SharedMemory")
        .allowlist_type("RingBuffer")
        .allowlist_function("frida_.*")
        .allowlist_function("shared_memory_.*")
        .allowlist_function("ring_buffer_.*")
        // Rust-specific configurations
        .derive_default(true)
        .derive_debug(true)
        .generate()
        .expect("Unable to generate bindings");
    
    // Write the bindings to the $OUT_DIR/bindings.rs file
    let bindings_path = out_dir.join("bindings.rs");
    bindings
        .write_to_file(bindings_path)
        .expect("Couldn't write bindings!");
    
    // Copy the agent library to a known location for runtime loading
    let agent_lib = if cfg!(target_os = "macos") {
        "libfrida_agent.dylib"
    } else if cfg!(target_os = "windows") {
        "frida_agent.dll"
    } else {
        "libfrida_agent.so"
    };
    
    let agent_src = dst.join("lib").join(agent_lib);
    let agent_dst = out_dir.join("../../..").join(agent_lib);
    
    if agent_src.exists() {
        std::fs::copy(&agent_src, &agent_dst)
            .expect("Failed to copy agent library");
        
        println!("cargo:info=Agent library copied to: {}", agent_dst.display());
    }
}

// Example Cargo.toml dependencies for this build.rs:
/*
[build-dependencies]
cmake = "0.1"
bindgen = "0.69"

[dependencies]
libc = "0.2"
once_cell = "1.19"
anyhow = "1.0"
*/