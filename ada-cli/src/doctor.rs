//! Doctor command for system health checks.
//!
//! Provides CLI commands for verifying ADA dependencies and system configuration.

use clap::Subcommand;
use serde::Serialize;
use std::path::PathBuf;
use std::process::Command;

#[derive(Subcommand)]
pub enum DoctorCommands {
    /// Run all health checks
    Check {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },
}

/// Result of a single health check
#[derive(Serialize, Clone)]
struct CheckResult {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    fix: Option<String>,
}

/// All check results
#[derive(Serialize)]
struct DoctorReport {
    status: String,
    checks: CheckResults,
    issues_count: usize,
}

#[derive(Serialize)]
struct CheckResults {
    frida_agent: CheckResult,
    code_signing: CheckResult,
    whisper: CheckResult,
    ffmpeg: CheckResult,
}

pub fn run(cmd: DoctorCommands) -> anyhow::Result<()> {
    match cmd {
        DoctorCommands::Check { format } => run_checks(&format),
    }
}

fn run_checks(format: &str) -> anyhow::Result<()> {
    let frida_agent = check_frida_agent();
    let code_signing = check_code_signing();
    let whisper = check_whisper();
    let ffmpeg = check_ffmpeg();

    let issues_count = [&frida_agent, &code_signing, &whisper, &ffmpeg]
        .iter()
        .filter(|c| !c.ok)
        .count();

    let status = if issues_count == 0 {
        "ok".to_string()
    } else {
        "issues_found".to_string()
    };

    if format == "json" {
        let report = DoctorReport {
            status,
            checks: CheckResults {
                frida_agent,
                code_signing,
                whisper,
                ffmpeg,
            },
            issues_count,
        };
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        print_text_report(&frida_agent, &code_signing, &whisper, &ffmpeg, issues_count);
    }

    if issues_count > 0 {
        std::process::exit(1);
    }

    Ok(())
}

fn print_text_report(
    frida_agent: &CheckResult,
    code_signing: &CheckResult,
    whisper: &CheckResult,
    ffmpeg: &CheckResult,
    issues_count: usize,
) {
    println!("ADA Doctor");
    println!("==========\n");

    println!("Core:");
    print_check("frida agent", frida_agent);
    println!();

    println!("Capture:");
    print_check("code signing", code_signing);
    println!();

    println!("Analysis:");
    print_check("whisper", whisper);
    print_check("ffmpeg", ffmpeg);
    println!();

    if issues_count == 0 {
        println!("Status: All checks passed");
    } else {
        println!(
            "Status: {} issue{} found",
            issues_count,
            if issues_count == 1 { "" } else { "s" }
        );
    }
}

fn print_check(name: &str, result: &CheckResult) {
    if result.ok {
        if let Some(path) = &result.path {
            println!("  \u{2713} {}: {}", name, path);
        } else {
            println!("  \u{2713} {}: valid", name);
        }
    } else {
        println!("  \u{2717} {}: not found", name);
        if let Some(fix) = &result.fix {
            println!("    \u{2192} {}", fix);
        }
    }
}

/// Check if Frida agent library is available
fn check_frida_agent() -> CheckResult {
    // Check ADA_AGENT_RPATH_SEARCH_PATHS environment variable first
    if let Ok(search_paths) = std::env::var("ADA_AGENT_RPATH_SEARCH_PATHS") {
        for path in search_paths.split(':') {
            let agent_path = PathBuf::from(path).join("libfrida_agent.dylib");
            if agent_path.exists() {
                return CheckResult {
                    ok: true,
                    path: Some(agent_path.display().to_string()),
                    fix: None,
                };
            }
        }
    }

    // Check known paths relative to the ada binary
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(bin_dir) = exe_path.parent() {
            // Check sibling lib directory
            let lib_dir = bin_dir.parent().map(|p| p.join("lib"));
            if let Some(lib_path) = lib_dir {
                let agent_path = lib_path.join("libfrida_agent.dylib");
                if agent_path.exists() {
                    return CheckResult {
                        ok: true,
                        path: Some(agent_path.display().to_string()),
                        fix: None,
                    };
                }
            }

            // Check same directory as binary
            let agent_path = bin_dir.join("libfrida_agent.dylib");
            if agent_path.exists() {
                return CheckResult {
                    ok: true,
                    path: Some(agent_path.display().to_string()),
                    fix: None,
                };
            }
        }
    }

    CheckResult {
        ok: false,
        path: None,
        fix: Some("Set ADA_AGENT_RPATH_SEARCH_PATHS to directory containing libfrida_agent.dylib".to_string()),
    }
}

/// Check if the ada binary has proper code signing with debugging entitlements
fn check_code_signing() -> CheckResult {
    // Get path to current executable
    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(_) => {
            return CheckResult {
                ok: false,
                path: None,
                fix: Some("Could not determine ada binary path".to_string()),
            };
        }
    };

    // Run codesign --display --entitlements - to check entitlements
    let output = Command::new("codesign")
        .args(["--display", "--entitlements", "-", "--xml"])
        .arg(&exe_path)
        .output();

    match output {
        Ok(result) => {
            if !result.status.success() {
                return CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("Run ./utils/sign_binary.sh to sign the ada binary".to_string()),
                };
            }

            // Check for debugging entitlement in the output
            let stdout = String::from_utf8_lossy(&result.stdout);
            let has_debug_entitlement = stdout.contains("com.apple.security.get-task-allow");

            if has_debug_entitlement {
                CheckResult {
                    ok: true,
                    path: None,
                    fix: None,
                }
            } else {
                CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("Run ./utils/sign_binary.sh to add debugging entitlements".to_string()),
                }
            }
        }
        Err(_) => CheckResult {
            ok: false,
            path: None,
            fix: Some("codesign command not available".to_string()),
        },
    }
}

/// Check if whisper is installed
fn check_whisper() -> CheckResult {
    match which::which("whisper") {
        Ok(path) => CheckResult {
            ok: true,
            path: Some(path.display().to_string()),
            fix: None,
        },
        Err(_) => CheckResult {
            ok: false,
            path: None,
            fix: Some("brew install openai-whisper".to_string()),
        },
    }
}

/// Check if ffmpeg is installed
fn check_ffmpeg() -> CheckResult {
    match which::which("ffmpeg") {
        Ok(path) => CheckResult {
            ok: true,
            path: Some(path.display().to_string()),
            fix: None,
        },
        Err(_) => CheckResult {
            ok: false,
            path: None,
            fix: Some("brew install ffmpeg".to_string()),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_result_json_serialization() {
        let result = CheckResult {
            ok: true,
            path: Some("/usr/bin/test".to_string()),
            fix: None,
        };
        let json = serde_json::to_string(&result).unwrap();
        assert!(json.contains("\"ok\":true"));
        assert!(json.contains("\"path\":\"/usr/bin/test\""));
        // fix should be skipped when None
        assert!(!json.contains("\"fix\""));
    }

    #[test]
    fn check_result_with_fix() {
        let result = CheckResult {
            ok: false,
            path: None,
            fix: Some("brew install test".to_string()),
        };
        let json = serde_json::to_string(&result).unwrap();
        assert!(json.contains("\"ok\":false"));
        assert!(json.contains("\"fix\":\"brew install test\""));
        // path should be skipped when None
        assert!(!json.contains("\"path\""));
    }

    #[test]
    fn doctor_report_serialization() {
        let report = DoctorReport {
            status: "ok".to_string(),
            checks: CheckResults {
                frida_agent: CheckResult {
                    ok: true,
                    path: Some("/path/to/lib".to_string()),
                    fix: None,
                },
                code_signing: CheckResult {
                    ok: true,
                    path: None,
                    fix: None,
                },
                whisper: CheckResult {
                    ok: true,
                    path: Some("/opt/homebrew/bin/whisper".to_string()),
                    fix: None,
                },
                ffmpeg: CheckResult {
                    ok: false,
                    path: None,
                    fix: Some("brew install ffmpeg".to_string()),
                },
            },
            issues_count: 1,
        };
        let json = serde_json::to_string_pretty(&report).unwrap();
        assert!(json.contains("\"status\": \"ok\""));
        assert!(json.contains("\"issues_count\": 1"));
    }
}
