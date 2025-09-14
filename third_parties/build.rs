use std::env;
use std::path::{Path, PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=frida-core/frida-core-example.c");
    println!("cargo:rerun-if-changed=frida-gum/frida-gum-example.c");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
    
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let profile = env::var("PROFILE").unwrap();
    
    // Determine build type based on profile
    let build_type = match profile.as_str() {
        "debug" => "Debug",
        "release" => "Release",
        _ => "Debug",
    };
    
    // Build the Frida examples using cmake
    let dst = cmake::Config::new(".")
        .define("CMAKE_BUILD_TYPE", build_type)
        .build();
    
    println!("cargo:info=Built Frida examples at: {}", dst.display());
    
    // Copy to predictable location
    let target_base = Path::new(&out_dir)
        .ancestors()
        .find(|p| p.file_name() == Some(std::ffi::OsStr::new("target")))
        .unwrap_or_else(|| Path::new("target"));
    
    let predictable_dir = target_base.join(&profile).join("frida_examples");
    std::fs::create_dir_all(&predictable_dir).ok();
    
    // Copy the built examples
    let examples = vec![
        ("bin/frida_gum_example", "frida_gum_example"),
        ("bin/frida_core_example", "frida_core_example"),
    ];
    
    for (src_path, dst_name) in examples {
        let src = dst.join(src_path);
        let dst_file = predictable_dir.join(dst_name);
        if src.exists() {
            if let Err(e) = std::fs::copy(&src, &dst_file) {
                println!("cargo:warning=Failed to copy {}: {}", src_path, e);
            } else {
                println!("cargo:info=Copied {} to {}", dst_name, dst_file.display());
            }
        }
    }
    
    println!("cargo:info=Frida examples available at: {}", predictable_dir.display());
}