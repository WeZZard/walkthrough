use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

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
    
    // Collect Rust coverage
    collect_rust_coverage(&workspace, &coverage_dir)?;
    
    // Collect C/C++ coverage
    collect_cpp_coverage(&workspace, &coverage_dir)?;
    
    println!("Coverage data collected.");
    Ok(())
}

fn collect_rust_coverage(workspace: &Path, coverage_dir: &Path) -> Result<()> {
    println!("Collecting Rust coverage...");
    
    // Find all .profraw files
    let profraw_files: Vec<PathBuf> = WalkDir::new(coverage_dir)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| e.path().extension().map_or(false, |ext| ext == "profraw"))
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if profraw_files.is_empty() {
        println!("No Rust coverage data found.");
        return Ok(());
    }
    
    let profdata_path = coverage_dir.join("rust.profdata");
    
    // Merge profraw files into profdata
    let mut merge_cmd = Command::new("llvm-profdata");
    merge_cmd.arg("merge")
        .arg("-sparse")
        .arg("-o")
        .arg(&profdata_path);
    
    for file in &profraw_files {
        merge_cmd.arg(file);
    }
    
    merge_cmd.status()
        .context("Failed to merge Rust coverage data")?;
    
    // Generate lcov report
    let lcov_path = coverage_dir.join("rust.lcov");
    
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
        let mut export_cmd = Command::new("llvm-cov");
        export_cmd.arg("export")
            .arg("--format=lcov")
            .arg("--instr-profile")
            .arg(&profdata_path)
            .arg("--ignore-filename-regex='/.cargo/|/rustc/'");
        
        for binary in &test_binaries {
            export_cmd.arg("--object").arg(binary);
        }
        
        let output = export_cmd.output()
            .context("Failed to export Rust coverage data")?;
        
        fs::write(&lcov_path, output.stdout)
            .context("Failed to write Rust lcov file")?;
        
        println!("Rust coverage saved to: {}", lcov_path.display());
    }
    
    Ok(())
}

fn collect_cpp_coverage(workspace: &Path, coverage_dir: &Path) -> Result<()> {
    println!("Collecting C/C++ coverage...");
    
    // Find all .profraw files from C/C++ tests
    let cpp_profraw_files: Vec<PathBuf> = WalkDir::new(workspace.join("target"))
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|e| {
            let path = e.path();
            path.extension().map_or(false, |ext| ext == "profraw") &&
            path.to_str().map_or(false, |s| s.contains("tracer_backend"))
        })
        .map(|e| e.path().to_path_buf())
        .collect();
    
    if cpp_profraw_files.is_empty() {
        println!("No C/C++ coverage data found.");
        return Ok(());
    }
    
    let profdata_path = coverage_dir.join("cpp.profdata");
    
    // Merge profraw files
    let mut merge_cmd = Command::new("llvm-profdata");
    merge_cmd.arg("merge")
        .arg("-sparse")
        .arg("-o")
        .arg(&profdata_path);
    
    for file in &cpp_profraw_files {
        merge_cmd.arg(file);
    }
    
    merge_cmd.status()
        .context("Failed to merge C/C++ coverage data")?;
    
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
        let lcov_path = coverage_dir.join("cpp.lcov");
        
        let mut export_cmd = Command::new("llvm-cov");
        export_cmd.arg("export")
            .arg("--format=lcov")
            .arg("--instr-profile")
            .arg(&profdata_path);
        
        for binary in &test_binaries {
            export_cmd.arg("--object").arg(binary);
        }
        
        let output = export_cmd.output()
            .context("Failed to export C/C++ coverage data")?;
        
        fs::write(&lcov_path, output.stdout)
            .context("Failed to write C/C++ lcov file")?;
        
        println!("C/C++ coverage saved to: {}", lcov_path.display());
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
            let html_dir = coverage_dir.join("html");
            fs::create_dir_all(&html_dir)?;
            
            // Generate HTML report using llvm-cov
            let profdata = coverage_dir.join("rust.profdata");
            if profdata.exists() {
                Command::new("llvm-cov")
                    .args(&["show", "--format=html", "--output-dir"])
                    .arg(&html_dir)
                    .arg("--instr-profile")
                    .arg(&profdata)
                    .status()
                    .context("Failed to generate HTML report")?;
                
                println!("HTML report saved to: {}", html_dir.display());
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