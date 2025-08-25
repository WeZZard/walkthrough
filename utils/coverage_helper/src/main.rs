//! # Coverage Helper for ADA Project
//! 
//! This tool provides unified coverage collection for Rust, C/C++, and Python code
//! using LLVM coverage tools that come with Rust or cargo-llvm-cov.
//! 
//! ## How It Works
//! 
//! Both Rust and C/C++ use the same LLVM coverage infrastructure:
//! - **Compile flags**: `-fprofile-instr-generate -fcoverage-mapping`
//! - **Output format**: `.profraw` files (raw profile data)
//! - **Processing**: `llvm-profdata` merges `.profraw` → `.profdata`
//! - **Reporting**: `llvm-cov` generates reports from `.profdata`
//! 
//! ## Usage Examples
//! 
//! ```bash
//! # Clean old coverage data
//! coverage_helper clean
//! 
//! # Run full coverage workflow
//! coverage_helper full --format lcov
//! 
//! # Or step by step:
//! coverage_helper clean
//! # ... run tests with LLVM_PROFILE_FILE set ...
//! coverage_helper collect
//! coverage_helper report --format html
//! ```
//! 
//! ## C/C++ Coverage Setup
//! 
//! In CMakeLists.txt:
//! ```cmake
//! if(ENABLE_COVERAGE)
//!     add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
//!     add_link_options(-fprofile-instr-generate -fcoverage-mapping)
//! endif()
//! ```
//! 
//! Then build with: `cmake -B build -DENABLE_COVERAGE=ON`
//! 
//! ## Environment Variables
//! 
//! - `LLVM_PROFILE_FILE`: Where to write .profraw files (e.g., "target/coverage/%p-%m.profraw")
//! - `CARGO_FEATURE_COVERAGE`: Set to "1" to enable coverage in Cargo builds
//! - `RUSTFLAGS`: Should include "-C instrument-coverage" for Rust coverage
//! 
//! ## Tool Discovery
//! 
//! The helper automatically finds LLVM tools in these locations:
//! 1. System PATH
//! 2. Homebrew LLVM: `/opt/homebrew/opt/llvm/bin/` (macOS ARM64)
//! 3. cargo-llvm-cov bundled tools (via `cargo llvm-cov show-env`)
//! 
//! ## Troubleshooting
//! 
//! If "llvm-profdata not found":
//! - Install LLVM: `brew install llvm` (macOS) or `apt install llvm` (Linux)
//! - Or install cargo-llvm-cov: `cargo install cargo-llvm-cov`
//! 
//! If no coverage data generated:
//! - Ensure code was compiled with coverage flags
//! - Check LLVM_PROFILE_FILE is set correctly
//! - Verify tests actually executed the instrumented code

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

mod toolchains;
mod dashboard;

#[derive(Parser)]
#[command(name = "coverage_helper")]
#[command(about = "Coverage collection helper for ADA project")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Clean coverage data
    Clean,
    /// Collect coverage data from all components
    Collect,
    /// Generate coverage report
    Report {
        /// Output format (lcov, html, text)
        #[arg(short, long, default_value = "lcov")]
        format: String,
    },
    /// Run full coverage workflow (clean, test with coverage, collect, report)
    Full {
        /// Output format for final report
        #[arg(short, long, default_value = "lcov")]
        format: String,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    
    match cli.command {
        Commands::Clean => clean_coverage(),
        Commands::Collect => collect_coverage(),
        Commands::Report { format } => generate_report(&format),
        Commands::Full { format } => {
            clean_coverage()?;
            run_tests_with_coverage()?;
            collect_coverage()?;
            generate_report(&format)
        }
    }
}

fn get_workspace_root() -> Result<PathBuf> {
    let output = Command::new("cargo")
        .args(&["locate-project", "--workspace", "--message-format=plain"])
        .output()
        .context("Failed to locate workspace root")?;
    
    let cargo_toml = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
    Ok(cargo_toml.parent().unwrap().to_path_buf())
}

fn clean_coverage() -> Result<()> {
    println!("Cleaning coverage data...");
    
    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    
    if coverage_dir.exists() {
        fs::remove_dir_all(&coverage_dir)
            .context("Failed to remove coverage directory")?;
    }
    
    fs::create_dir_all(&coverage_dir)
        .context("Failed to create coverage directory")?;
    
    // Clean C/C++ coverage files
    for entry in WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
    {
        let path = entry.path();
        if path.extension().map_or(false, |ext| ext == "profraw" || ext == "profdata" || ext == "gcda" || ext == "gcno") {
            fs::remove_file(path).ok();
        }
    }
    
    println!("Coverage data cleaned.");
    Ok(())
}

fn run_tests_with_coverage() -> Result<()> {
    println!("Running tests with coverage enabled...");
    
    let workspace = get_workspace_root()?;
    
    // Set environment variables for coverage
    env::set_var("CARGO_FEATURE_COVERAGE", "1");
    env::set_var("RUSTFLAGS", "-C instrument-coverage");
    env::set_var("LLVM_PROFILE_FILE", workspace.join("target/coverage/prof-%p-%m.profraw").to_str().unwrap());
    
    // Run Rust and C/C++ tests with coverage
    let status = Command::new("cargo")
        .args(&["test", "--all", "--features", "tracer_backend/coverage,query_engine/coverage"])
        .env("CARGO_FEATURE_COVERAGE", "1")
        .status()
        .context("Failed to run tests with coverage")?;
    
    if !status.success() {
        anyhow::bail!("Tests failed");
    }
    
    // Run Python tests with coverage if query_engine has Python tests
    let query_engine_dir = workspace.join("query_engine");
    if query_engine_dir.join("tests").exists() {
        println!("Running Python tests with coverage...");
        
        let status = Command::new("python")
            .args(&["-m", "pytest", "--cov=query_engine", "--cov-report=lcov:target/coverage/python.lcov"])
            .current_dir(&query_engine_dir)
            .status();
        
        if let Err(e) = status {
            eprintln!("Warning: Failed to run Python tests: {}", e);
        }
    }
    
    println!("Tests completed.");
    Ok(())
}

fn collect_coverage() -> Result<()> {
    println!("Collecting coverage data...");
    
    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    let report_dir = workspace.join("target").join("coverage_report");
    
    // Ensure directories exist
    std::fs::create_dir_all(&coverage_dir)?;
    std::fs::create_dir_all(&report_dir)?;
    
    let mut lcov_files = Vec::new();
    
    // Collect Rust coverage
    let rust_lcov = report_dir.join("rust.lcov");
    if let Ok(_) = collect_rust_coverage(&workspace, &coverage_dir, &rust_lcov) {
        lcov_files.push(rust_lcov);
    }
    
    // Collect C/C++ coverage
    let cpp_lcov = report_dir.join("cpp.lcov");
    if let Ok(_) = collect_cpp_coverage(&workspace, &coverage_dir, &cpp_lcov) {
        lcov_files.push(cpp_lcov);
    }
    
    // Collect Python coverage
    let python_lcov = report_dir.join("python.lcov");
    if let Ok(_) = collect_python_coverage(&workspace, &python_lcov) {
        lcov_files.push(python_lcov);
    }
    
    // Merge all LCOV files
    if !lcov_files.is_empty() {
        let merged_lcov = report_dir.join("merged.lcov");
        toolchains::merge_lcov_files(&lcov_files, &merged_lcov)?;
        println!("\nMerged coverage data to: {}", merged_lcov.display());
    } else {
        println!("\nNo coverage data collected.");
    }
    
    Ok(())
}

/// Collect Python coverage using pytest-cov
fn collect_python_coverage(workspace: &Path, output_lcov: &Path) -> Result<()> {
    println!("\nCollecting Python coverage...");
    
    // Check which Python components exist
    let mut components_to_test = vec![];
    
    let query_engine_dir = workspace.join("query_engine");
    if query_engine_dir.exists() && query_engine_dir.join("pyproject.toml").exists() {
        components_to_test.push(("query_engine", query_engine_dir));
    }
    
    let mcp_server_dir = workspace.join("mcp_server");
    if mcp_server_dir.exists() && mcp_server_dir.join("pyproject.toml").exists() {
        components_to_test.push(("mcp_server", mcp_server_dir));
    }
    
    if components_to_test.is_empty() {
        println!("  No Python components found");
        return Ok(());
    }
    
    let mut all_lcov_data = String::new();
    
    for (name, dir) in components_to_test {
        println!("  Testing Python component: {}", name);
        
        // Create a temporary directory for this component's coverage
        let temp_coverage_file = workspace.join("target").join("coverage").join(format!("{}_coverage.xml", name));
        
        // Run pytest with coverage for this component
        let output = Command::new("python3")
            .args(&[
                "-m", "pytest",
                "--cov", name,
                "--cov-report", &format!("xml:{}", temp_coverage_file.display()),
                "--cov-report", "term",
            ])
            .current_dir(&dir)
            .output();
        
        match output {
            Ok(result) if result.status.success() => {
                println!("    Tests passed for {}", name);
                
                // Convert XML to LCOV if the coverage file was created
                if temp_coverage_file.exists() {
                    if let Ok(lcov_data) = convert_xml_to_lcov(&temp_coverage_file, &dir) {
                        all_lcov_data.push_str(&lcov_data);
                    }
                }
            }
            Ok(result) => {
                let stderr = String::from_utf8_lossy(&result.stderr);
                let stdout = String::from_utf8_lossy(&result.stdout);
                println!("    No tests found or tests failed for {}", name);
                if !stderr.is_empty() {
                    println!("    stderr: {}", stderr);
                }
                if !stdout.is_empty() {
                    println!("    stdout: {}", stdout);
                }
            }
            Err(e) => {
                println!("    pytest not available or failed for {}: {}", name, e);
            }
        }
    }
    
    // Write combined LCOV data
    if !all_lcov_data.is_empty() {
        fs::write(output_lcov, all_lcov_data)?;
        println!("  Python coverage saved to: {}", output_lcov.display());
    } else {
        println!("  No Python coverage data collected");
    }
    
    Ok(())
}

/// Convert Python coverage XML to LCOV format
fn convert_xml_to_lcov(xml_file: &Path, base_dir: &Path) -> Result<String> {
    // For now, use a simple Python script to convert
    let python_script = r#"
import xml.etree.ElementTree as ET
import sys
import os

def convert_xml_to_lcov(xml_path, base_dir):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    
    lcov_output = []
    
    for package in root.findall('.//package'):
        for class_elem in package.findall('classes/class'):
            filename = class_elem.get('filename')
            if filename:
                # Make path relative to workspace root
                full_path = os.path.join(base_dir, filename)
                lcov_output.append(f'SF:{full_path}')
                
                # Process lines
                for line in class_elem.findall('lines/line'):
                    line_number = line.get('number')
                    hits = line.get('hits', '0')
                    lcov_output.append(f'DA:{line_number},{hits}')
                
                lcov_output.append('end_of_record')
    
    return '\n'.join(lcov_output)

if __name__ == '__main__':
    xml_path = sys.argv[1]
    base_dir = sys.argv[2]
    print(convert_xml_to_lcov(xml_path, base_dir))
"#;
    
    let output = Command::new("python3")
        .arg("-c")
        .arg(python_script)
        .arg(xml_file)
        .arg(base_dir)
        .output()?;
    
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    } else {
        // Fallback: return empty string if conversion fails
        Ok(String::new())
    }
}

/// Find LLVM tools (llvm-profdata, llvm-cov) in various locations.
/// 
/// This function searches for LLVM tools in the following order:
/// 1. System PATH - if already available
/// 2. Homebrew LLVM installation - common on macOS
/// 3. cargo-llvm-cov's bundled version - if installed
/// 
/// # Why this is needed
/// 
/// On macOS, even though LLVM tools may be installed via Homebrew,
/// they're often not in PATH because macOS ships its own (different)
/// versions with Xcode. This function ensures we can find the right
/// LLVM tools that are compatible with our coverage instrumentation.
/// 
/// # Example paths searched:
/// - `/opt/homebrew/opt/llvm/bin/llvm-profdata` (macOS ARM64)
/// - `/usr/local/opt/llvm/bin/llvm-profdata` (macOS x86_64)
/// - `/opt/homebrew/Cellar/llvm/*/bin/llvm-profdata` (versioned installs)
fn find_llvm_tool(tool_name: &str) -> Result<PathBuf> {
    // Try to find the tool in various locations
    
    // 1. Check if it's already in PATH
    if let Ok(output) = Command::new("which").arg(tool_name).output() {
        if output.status.success() {
            let path = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
            if path.exists() {
                return Ok(path);
            }
        }
    }
    
    // 2. Try Homebrew LLVM installation
    let homebrew_paths = vec![
        format!("/opt/homebrew/opt/llvm/bin/{}", tool_name),
        format!("/usr/local/opt/llvm/bin/{}", tool_name),
        format!("/opt/homebrew/Cellar/llvm/*/bin/{}", tool_name),
    ];
    
    for path_pattern in homebrew_paths {
        if let Ok(entries) = glob::glob(&path_pattern) {
            for entry in entries.flatten() {
                if entry.exists() {
                    return Ok(entry);
                }
            }
        }
    }
    
    // 3. Try cargo-llvm-cov's bundled version
    if let Ok(output) = Command::new("cargo")
        .args(&["llvm-cov", "show-env"])
        .output() 
    {
        let output_str = String::from_utf8_lossy(&output.stdout);
        for line in output_str.lines() {
            if line.starts_with("LLVM_COV=") || line.starts_with("LLVM_PROFDATA=") {
                let path = line.split('=').nth(1).unwrap_or("");
                let tool_dir = PathBuf::from(path).parent().unwrap().to_path_buf();
                let tool_path = tool_dir.join(tool_name);
                if tool_path.exists() {
                    return Ok(tool_path);
                }
            }
        }
    }
    
    anyhow::bail!("Could not find {}. Please install LLVM tools via 'brew install llvm' or 'cargo install cargo-llvm-cov'", tool_name)
}

/// Collect Rust coverage using LLVM tools.
/// 
/// This function:
/// 1. Finds all .profraw files generated by Rust tests
/// 2. Merges them into a single .profdata file using llvm-profdata
/// 3. Exports to LCOV format using llvm-cov
/// 
/// The same LLVM tools are used for both Rust and C/C++ coverage,
/// providing a unified coverage solution.
fn collect_rust_coverage(workspace: &Path, coverage_dir: &Path, output_lcov: &Path) -> Result<()> {
    println!("\nCollecting Rust coverage...");
    
    // Find all Rust .profraw files
    let profraw_files: Vec<PathBuf> = WalkDir::new(coverage_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.extension().map_or(false, |ext| ext == "profraw") &&
            path.file_name().and_then(|n| n.to_str()).map_or(false, |n| n.contains("rust") || !n.contains("cpp"))
        })
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if profraw_files.is_empty() {
        println!("No Rust coverage data found.");
        return Ok(());
    }
    
    // Use Rust toolchain for Rust code
    let toolchain = toolchains::detect_rust_toolchain()?;
    
    // Merge profraw files into profdata
    let profdata_path = coverage_dir.join("rust.profdata");
    toolchains::merge_profdata(&toolchain, &profraw_files, &profdata_path)?;
    
    // Find test binaries
    let test_binaries: Vec<PathBuf> = WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.is_file() && 
            path.to_str().map_or(false, |s| s.contains("/deps/") && !s.contains("."))
        })
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if !test_binaries.is_empty() {
        // Export to LCOV format
        toolchains::export_lcov(&toolchain, &test_binaries, &profdata_path, output_lcov)?;
        println!("  Rust coverage saved to: {}", output_lcov.display());
    } else {
        println!("  No Rust test binaries found");
    }
    
    Ok(())
}

/// Collect C/C++ coverage using the same LLVM tools as Rust.
/// 
/// This demonstrates the key benefit: Rust's LLVM tools work perfectly
/// for C/C++ code compiled with clang/gcc using:
/// - `-fprofile-instr-generate -fcoverage-mapping`
/// 
/// Process:
/// 1. Find .profraw files from C/C++ test executables
/// 2. Merge them using llvm-profdata (same tool as Rust)
/// 3. Export to LCOV using llvm-cov (same tool as Rust)
/// 
/// This unified approach means:
/// - No need for gcov/lcov for C/C++
/// - Same toolchain for all languages
/// - Consistent output format (LCOV) for all coverage
fn collect_cpp_coverage(workspace: &Path, coverage_dir: &Path, output_lcov: &Path) -> Result<()> {
    println!("Collecting C/C++ coverage...");
    
    // First, run all C++ test binaries to generate coverage data
    println!("  Running C++ tests with coverage instrumentation...");
    
    // Find all C++ test binaries
    let test_binary_patterns = vec![
        workspace.join("target/release/tracer_backend/test"),
        workspace.join("target/debug/tracer_backend/test"),
        workspace.join("target/release/build/tracer_backend-*/out/build"),
        workspace.join("target/debug/build/tracer_backend-*/out/build"),
    ];
    
    let mut test_binaries = Vec::new();
    for pattern in test_binary_patterns {
        if let Ok(entries) = glob::glob(&format!("{}/**/test_*", pattern.display())) {
            for entry in entries.flatten() {
                if entry.is_file() && entry.to_str().map_or(false, |s| {
                    s.contains("test_") && 
                    !s.ends_with(".o") && 
                    !s.ends_with(".d") && 
                    !s.ends_with(".cmake") &&
                    !s.contains("[")  // Exclude CMake generated files with brackets
                }) {
                    // Check if it's executable
                    if let Ok(metadata) = entry.metadata() {
                        #[cfg(unix)]
                        {
                            use std::os::unix::fs::PermissionsExt;
                            if metadata.permissions().mode() & 0o111 != 0 {
                                test_binaries.push(entry);
                            }
                        }
                        #[cfg(not(unix))]
                        {
                            test_binaries.push(entry);
                        }
                    }
                }
            }
        }
    }
    
    if test_binaries.is_empty() {
        println!("  No C++ test binaries found. Make sure to build with --features coverage");
        return Ok(());
    }
    
    println!("  Found {} C++ test binaries", test_binaries.len());
    
    // Run each test binary with unique profraw output
    for (idx, test_binary) in test_binaries.iter().enumerate() {
        let binary_name = test_binary.file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("unknown");
        
        let profraw_path = coverage_dir.join(format!("cpp_{}_{}.profraw", binary_name, idx));
        
        println!("    Running {}", binary_name);
        
        let output = Command::new(&test_binary)
            .env("LLVM_PROFILE_FILE", &profraw_path)
            .output();
        
        match output {
            Ok(result) => {
                if !result.status.success() {
                    println!("      Warning: {} failed with status {}", binary_name, result.status);
                } else {
                    println!("      ✓ {} completed", binary_name);
                }
            }
            Err(e) => {
                println!("      Error running {}: {}", binary_name, e);
            }
        }
    }
    
    // Now find all generated .profraw files
    let cpp_profraw_files: Vec<PathBuf> = WalkDir::new(coverage_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.extension().map_or(false, |ext| ext == "profraw") &&
            path.file_name().and_then(|n| n.to_str()).map_or(false, |n| n.starts_with("cpp_"))
        })
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if cpp_profraw_files.is_empty() {
        println!("  No C/C++ coverage data generated.");
        return Ok(());
    }
    
    println!("  Generated {} profraw files", cpp_profraw_files.len());
    
    // Use C++ toolchain (prefers Xcode on macOS)
    let toolchain = toolchains::detect_cpp_toolchain()?;
    
    // Merge profraw files
    let profdata_path = coverage_dir.join("cpp.profdata");
    toolchains::merge_profdata(&toolchain, &cpp_profraw_files, &profdata_path)?;
    
    // Find C/C++ test binaries
    let test_binaries: Vec<PathBuf> = WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.is_file() && 
            path.file_name().map_or(false, |name| {
                let name_str = name.to_string_lossy();
                name_str.starts_with("test_") && !name_str.contains(".")
            })
        })
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if !test_binaries.is_empty() {
        // Export to LCOV format
        toolchains::export_lcov(&toolchain, &test_binaries, &profdata_path, output_lcov)?;
        println!("  C/C++ coverage saved to: {}", output_lcov.display());
    } else {
        println!("  No C/C++ test binaries found");
    }
    
    Ok(())
}

fn generate_report(format: &str) -> Result<()> {
    println!("Generating {} coverage report...", format);
    
    let workspace = get_workspace_root()?;
    let coverage_dir = workspace.join("target").join("coverage");
    
    match format {
        "lcov" => {
            // Merge all lcov files
            let mut lcov_files = vec![];
            
            for entry in ["rust.lcov", "cpp.lcov", "python.lcov"] {
                let path = coverage_dir.join(entry);
                if path.exists() {
                    lcov_files.push(path);
                }
            }
            
            if lcov_files.is_empty() {
                println!("No coverage data to report.");
                return Ok(());
            }
            
            let merged_lcov = coverage_dir.join("coverage.lcov");
            
            // Merge lcov files
            let mut merge_cmd = Command::new("lcov");
            for file in &lcov_files {
                merge_cmd.arg("-a").arg(file);
            }
            merge_cmd.arg("-o").arg(&merged_lcov);
            
            let status = merge_cmd.status();
            if status.is_err() {
                // Fallback: just concatenate files
                let mut merged_content = String::new();
                for file in &lcov_files {
                    merged_content.push_str(&fs::read_to_string(file)?);
                }
                fs::write(&merged_lcov, merged_content)?;
            }
            
            println!("Coverage report saved to: {}", merged_lcov.display());
            
            // Calculate total coverage
            calculate_coverage_percentage(&merged_lcov)?;
        }
        "html" => {
            let report_dir = workspace.join("target").join("coverage_report");
            let merged_lcov = report_dir.join("merged.lcov");
            
            // First ensure we have merged LCOV
            if !merged_lcov.exists() {
                println!("No merged LCOV found. Running collection first...");
                collect_coverage()?;
            }
            
            // Generate dashboard
            if merged_lcov.exists() {
                dashboard::generate_dashboard(&workspace, &report_dir, &merged_lcov)?;
                println!("HTML dashboard saved to: {}/index.html", report_dir.display());
            } else {
                println!("No coverage data available for HTML report");
            }
        }
        "text" => {
            // Generate text summary
            let profdata = coverage_dir.join("rust.profdata");
            if profdata.exists() {
                Command::new("llvm-cov")
                    .args(&["report", "--instr-profile"])
                    .arg(&profdata)
                    .status()
                    .context("Failed to generate text report")?;
            }
        }
        _ => {
            anyhow::bail!("Unsupported format: {}", format);
        }
    }
    
    Ok(())
}

fn calculate_coverage_percentage(lcov_file: &Path) -> Result<()> {
    let content = fs::read_to_string(lcov_file)?;
    
    let mut lines_hit = 0;
    let mut lines_total = 0;
    
    for line in content.lines() {
        if line.starts_with("DA:") {
            let parts: Vec<&str> = line[3..].split(',').collect();
            if parts.len() == 2 {
                lines_total += 1;
                if let Ok(hits) = parts[1].parse::<i32>() {
                    if hits > 0 {
                        lines_hit += 1;
                    }
                }
            }
        }
    }
    
    if lines_total > 0 {
        let percentage = (lines_hit as f64 / lines_total as f64) * 100.0;
        println!("Total coverage: {:.2}% ({}/{} lines)", percentage, lines_hit, lines_total);
    }
    
    Ok(())
}