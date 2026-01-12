use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let project_root = manifest_dir
        .parent()
        .and_then(|parent| parent.parent())
        .unwrap();
    let xcodeproj = project_root.join("ada-capture/macos/AdaCapture/AdaCapture.xcodeproj");

    if !xcodeproj.exists() {
        println!("cargo:warning=SwiftUI project not found at {}", xcodeproj.display());
        return;
    }

    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    let configuration = if profile == "release" { "Release" } else { "Debug" };

    let derived_data = project_root.join("target/ada_gui");
    let status = Command::new("xcodebuild")
        .args([
            "-project",
            xcodeproj.to_str().unwrap(),
            "-scheme",
            "AdaCapture",
            "-configuration",
            configuration,
            "-derivedDataPath",
            derived_data.to_str().unwrap(),
        ])
        .status();

    match status {
        Ok(result) if result.success() => {
            println!("cargo:info=SwiftUI app built via xcodebuild");
            copy_daemon(project_root, &derived_data, configuration);
            copy_agent(project_root, &derived_data, configuration);
        }
        Ok(result) => {
            println!("cargo:warning=xcodebuild failed with status {}", result);
        }
        Err(err) => {
            println!("cargo:warning=Failed to run xcodebuild: {}", err);
        }
    }

    let rerun_path = project_root.join("ada-capture/macos/AdaCapture");
    println!("cargo:rerun-if-changed={}", rerun_path.display());
}

fn copy_daemon(project_root: &Path, derived_data: &Path, configuration: &str) {
    let target_dir = env::var("CARGO_TARGET_DIR").unwrap_or_else(|_| "target".to_string());
    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    let target_root = PathBuf::from(target_dir);
    let target_root = if target_root.is_absolute() {
        target_root
    } else {
        project_root.join(target_root)
    };
    let daemon = target_root.join(&profile).join("ada-capture-daemon");

    if !daemon.exists() {
        println!(
            "cargo:warning=ada-capture-daemon not found at {}; build it via cargo build -p ada-cli --bin ada-capture-daemon",
            daemon.display()
        );
        return;
    }

    let app_path = derived_data
        .join("Build/Products")
        .join(configuration)
        .join("AdaCapture.app");
    let dest = app_path.join("Contents/MacOS/ada-capture-daemon");

    if let Err(err) = std::fs::create_dir_all(dest.parent().unwrap()) {
        println!("cargo:warning=Failed to create app bundle dir: {}", err);
        return;
    }

    if let Err(err) = std::fs::copy(&daemon, &dest) {
        println!("cargo:warning=Failed to copy daemon into app: {}", err);
        return;
    }

    println!("cargo:info=Copied ada-capture-daemon into app bundle");
}

fn copy_agent(project_root: &Path, derived_data: &Path, configuration: &str) {
    let target_dir = env::var("CARGO_TARGET_DIR").unwrap_or_else(|_| "target".to_string());
    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    let target_root = PathBuf::from(target_dir);
    let target_root = if target_root.is_absolute() {
        target_root
    } else {
        project_root.join(target_root)
    };

    let agent = target_root
        .join(&profile)
        .join("tracer_backend/lib/libfrida_agent.dylib");

    if !agent.exists() {
        println!(
            "cargo:warning=libfrida_agent.dylib not found at {}; build tracer_backend first",
            agent.display()
        );
        return;
    }

    let app_path = derived_data
        .join("Build/Products")
        .join(configuration)
        .join("AdaCapture.app");
    let dest = app_path.join("Contents/MacOS/libfrida_agent.dylib");

    if let Err(err) = std::fs::create_dir_all(dest.parent().unwrap()) {
        println!("cargo:warning=Failed to create app bundle dir: {}", err);
        return;
    }

    if let Err(err) = std::fs::copy(&agent, &dest) {
        println!("cargo:warning=Failed to copy libfrida_agent.dylib: {}", err);
        return;
    }

    println!("cargo:info=Copied libfrida_agent.dylib into app bundle");
}
