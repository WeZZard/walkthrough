//! Toolchain detection module for coverage tools
//! 
//! This module provides platform-aware detection of LLVM coverage tools:
//! - Rust: Uses rustup's bundled LLVM tools
//! - C/C++ on macOS: Uses Xcode's LLVM tools via xcrun
//! - C/C++ on Linux: Uses system LLVM tools
//! - Python: Delegates to pytest-cov

use anyhow::{Context, Result};
use std::path::{Path, PathBuf};
use std::process::Command;
use glob;

/// Represents the detected LLVM toolchain
#[derive(Debug, Clone)]
pub struct LlvmToolchain {
    pub profdata: PathBuf,
    pub cov: PathBuf,
    pub source: ToolchainSource,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ToolchainSource {
    Rustup,      // Rust's bundled LLVM tools
    Xcode,       // macOS Xcode Command Line Tools
    System,      // System-installed LLVM
    Homebrew,    // Homebrew-installed LLVM
}

impl std::fmt::Display for ToolchainSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Rustup => write!(f, "Rust toolchain (rustup)"),
            Self::Xcode => write!(f, "Xcode Command Line Tools"),
            Self::System => write!(f, "System LLVM"),
            Self::Homebrew => write!(f, "Homebrew LLVM"),
        }
    }
}

/// Detect the appropriate LLVM toolchain for Rust code
pub fn detect_rust_toolchain() -> Result<LlvmToolchain> {
    println!("Detecting Rust LLVM toolchain...");
    
    // Find rustup's LLVM tools
    let rustup_home = std::env::var("RUSTUP_HOME")
        .unwrap_or_else(|_| format!("{}/.rustup", std::env::var("HOME").unwrap()));
    
    // Find the active toolchain
    let output = Command::new("rustup")
        .args(&["show", "active-toolchain"])
        .output()
        .context("Failed to run rustup")?;
    
    let toolchain = String::from_utf8_lossy(&output.stdout);
    let toolchain_name = toolchain.split_whitespace().next()
        .context("Failed to parse rustup toolchain")?;
    
    // Look for LLVM tools in the toolchain
    let patterns = vec![
        format!("{}/toolchains/{}/lib/rustlib/*/bin", rustup_home, toolchain_name),
        format!("{}/toolchains/{}/lib/rustlib/*/llvm-tools-preview/bin", rustup_home, toolchain_name),
    ];
    
    for pattern in patterns {
        if let Ok(entries) = glob::glob(&pattern) {
            for entry in entries.flatten() {
                let profdata = entry.join("llvm-profdata");
                let cov = entry.join("llvm-cov");
                
                if profdata.exists() && cov.exists() {
                    println!("  Found Rust LLVM tools at: {}", entry.display());
                    return Ok(LlvmToolchain {
                        profdata,
                        cov,
                        source: ToolchainSource::Rustup,
                    });
                }
            }
        }
    }
    
    anyhow::bail!(
        "Could not find Rust LLVM tools. Please run: rustup component add llvm-tools-preview"
    )
}

/// Detect the appropriate LLVM toolchain for C/C++ code
pub fn detect_cpp_toolchain() -> Result<LlvmToolchain> {
    println!("Detecting C/C++ LLVM toolchain...");
    
    // On macOS, prefer Xcode's LLVM tools
    #[cfg(target_os = "macos")]
    {
        if let Ok(toolchain) = detect_xcode_toolchain() {
            return Ok(toolchain);
        }
    }
    
    // Try system LLVM tools
    if let Ok(toolchain) = detect_system_toolchain() {
        return Ok(toolchain);
    }
    
    // Try Homebrew LLVM
    if let Ok(toolchain) = detect_homebrew_toolchain() {
        return Ok(toolchain);
    }
    
    anyhow::bail!(
        "Could not find C/C++ LLVM tools. Please install:\n\
         - macOS: Xcode Command Line Tools (xcode-select --install)\n\
         - Linux: apt install llvm or yum install llvm"
    )
}

/// Detect Xcode's LLVM toolchain on macOS
#[cfg(target_os = "macos")]
fn detect_xcode_toolchain() -> Result<LlvmToolchain> {
    // Check if Xcode Command Line Tools are installed
    let output = Command::new("xcode-select")
        .arg("-p")
        .output()
        .context("Failed to run xcode-select")?;
    
    if !output.status.success() {
        anyhow::bail!("Xcode Command Line Tools not installed");
    }
    
    // Find tools via xcrun
    let profdata_output = Command::new("xcrun")
        .args(&["--find", "llvm-profdata"])
        .output()
        .context("Failed to find llvm-profdata via xcrun")?;
    
    let cov_output = Command::new("xcrun")
        .args(&["--find", "llvm-cov"])
        .output()
        .context("Failed to find llvm-cov via xcrun")?;
    
    if profdata_output.status.success() && cov_output.status.success() {
        let profdata = PathBuf::from(String::from_utf8_lossy(&profdata_output.stdout).trim());
        let cov = PathBuf::from(String::from_utf8_lossy(&cov_output.stdout).trim());
        
        println!("  Found Xcode LLVM tools:");
        println!("    llvm-profdata: {}", profdata.display());
        println!("    llvm-cov: {}", cov.display());
        
        return Ok(LlvmToolchain {
            profdata,
            cov,
            source: ToolchainSource::Xcode,
        });
    }
    
    anyhow::bail!("Xcode LLVM tools not found")
}

/// Detect Xcode's LLVM toolchain on macOS (stub for non-macOS)
#[cfg(not(target_os = "macos"))]
fn detect_xcode_toolchain() -> Result<LlvmToolchain> {
    anyhow::bail!("Xcode tools are only available on macOS")
}

/// Detect system-installed LLVM toolchain
fn detect_system_toolchain() -> Result<LlvmToolchain> {
    // Try to find in PATH
    let profdata = which::which("llvm-profdata")
        .context("llvm-profdata not found in PATH")?;
    let cov = which::which("llvm-cov")
        .context("llvm-cov not found in PATH")?;
    
    println!("  Found system LLVM tools:");
    println!("    llvm-profdata: {}", profdata.display());
    println!("    llvm-cov: {}", cov.display());
    
    Ok(LlvmToolchain {
        profdata,
        cov,
        source: ToolchainSource::System,
    })
}

/// Detect Homebrew-installed LLVM toolchain
fn detect_homebrew_toolchain() -> Result<LlvmToolchain> {
    let homebrew_paths = vec![
        "/opt/homebrew/opt/llvm/bin",     // ARM64 Macs
        "/usr/local/opt/llvm/bin",        // Intel Macs
    ];
    
    for base_path in homebrew_paths {
        let profdata = PathBuf::from(base_path).join("llvm-profdata");
        let cov = PathBuf::from(base_path).join("llvm-cov");
        
        if profdata.exists() && cov.exists() {
            println!("  Found Homebrew LLVM tools:");
            println!("    llvm-profdata: {}", profdata.display());
            println!("    llvm-cov: {}", cov.display());
            
            return Ok(LlvmToolchain {
                profdata,
                cov,
                source: ToolchainSource::Homebrew,
            });
        }
    }
    
    anyhow::bail!("Homebrew LLVM tools not found")
}

/// Run llvm-profdata merge command
pub fn merge_profdata(
    toolchain: &LlvmToolchain,
    profraw_files: &[PathBuf],
    output: &Path,
) -> Result<()> {
    if profraw_files.is_empty() {
        anyhow::bail!("No .profraw files to merge");
    }
    
    println!("  Merging {} .profraw files using {}", 
             profraw_files.len(), toolchain.source);
    
    let mut cmd = Command::new(&toolchain.profdata);
    cmd.arg("merge")
       .arg("-sparse");
    
    for file in profraw_files {
        cmd.arg(file);
    }
    
    cmd.arg("-o").arg(output);
    
    let output = cmd.output()
        .context("Failed to run llvm-profdata merge")?;
    
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        anyhow::bail!("llvm-profdata merge failed: {}", stderr);
    }
    
    Ok(())
}

/// Export coverage to LCOV format
pub fn export_lcov(
    toolchain: &LlvmToolchain,
    binaries: &[PathBuf],
    profdata: &Path,
    output: &Path,
) -> Result<()> {
    if binaries.is_empty() {
        anyhow::bail!("No binaries to export coverage for");
    }
    
    println!("  Exporting LCOV using {}", toolchain.source);
    
    let mut cmd = Command::new(&toolchain.cov);
    cmd.arg("export")
       .arg("-format=lcov")
       .arg(format!("-instr-profile={}", profdata.display()));
    
    // Add all binaries as objects
    for binary in binaries {
        cmd.arg(binary);
    }
    
    let output_data = cmd.output()
        .context("Failed to run llvm-cov export")?;
    
    if !output_data.status.success() {
        let stderr = String::from_utf8_lossy(&output_data.stderr);
        anyhow::bail!("llvm-cov export failed: {}", stderr);
    }
    
    // Write LCOV data to file
    std::fs::write(output, &output_data.stdout)
        .context("Failed to write LCOV file")?;
    
    Ok(())
}

/// Merge multiple LCOV files into one
pub fn merge_lcov_files(lcov_files: &[PathBuf], output: &Path) -> Result<()> {
    // Filter to only existing files
    let existing_files: Vec<&PathBuf> = lcov_files.iter()
        .filter(|f| f.exists())
        .collect();
    
    if existing_files.is_empty() {
        anyhow::bail!("No LCOV files to merge");
    }
    
    if existing_files.len() == 1 {
        // Just copy the single file
        std::fs::copy(existing_files[0], output)
            .context("Failed to copy LCOV file")?;
        println!("  Single LCOV file copied to: {}", output.display());
        // Sanitize counts to ensure integers (avoid scientific notation edge cases)
        sanitize_lcov_counts(output)?;
        return Ok(());
    }
    
    println!("  Merging {} LCOV files...", existing_files.len());
    
    // Use lcov tool to merge with error ignoring for compatibility
    let mut cmd = Command::new("lcov");
    
    // Ignore common errors that don't affect coverage data
    cmd.arg("--ignore-errors")
       .arg("inconsistent")
       .arg("--ignore-errors")
       .arg("corrupt")
       .arg("--ignore-errors")
       .arg("unsupported");
    
    for file in existing_files {
        cmd.arg("--add-tracefile").arg(file);
    }
    
    cmd.arg("--output-file").arg(output);
    
    let output_data = cmd.output()
        .context("Failed to run lcov merge")?;
    
    if !output_data.status.success() {
        let stderr = String::from_utf8_lossy(&output_data.stderr);
        anyhow::bail!("lcov merge failed: {}", stderr);
    }
    
    // Sanitize counts to ensure integers (avoid scientific notation from lcov merge)
    sanitize_lcov_counts(output)?;
    println!("  Merged LCOV written to: {}", output.display());
    Ok(())
}

/// Ensure DA line hit counts are integers (diff-cover expects ints, not scientific notation)
fn sanitize_lcov_counts(path: &Path) -> Result<()> {
    let content = std::fs::read_to_string(path)
        .with_context(|| format!("Failed to read LCOV file {}", path.display()))?;
    let mut out = String::with_capacity(content.len());
    for line in content.lines() {
        if let Some(rest) = line.strip_prefix("DA:") {
            // Format: DA:<line>,<hits>
            if let Some((ln_str, hits_str)) = rest.split_once(',') {
                let hits_sanitized = if hits_str.contains('e') || hits_str.contains('E') {
                    // Convert scientific notation to integer by truncation
                    match hits_str.parse::<f64>() {
                        Ok(v) if v.is_finite() && v >= 0.0 => format!("{}", v.trunc() as u64),
                        _ => String::from("0"),
                    }
                } else {
                    hits_str.trim().to_string()
                };
                out.push_str(&format!("DA:{},{}\n", ln_str.trim(), hits_sanitized));
                continue;
            }
        }
        out.push_str(line);
        out.push('\n');
    }
    std::fs::write(path, out)
        .with_context(|| format!("Failed to write sanitized LCOV file {}", path.display()))?;
    Ok(())
}
