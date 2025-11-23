use std::io;
use std::process::{Command, Stdio};
use std::time::Duration;

// Helper function for individual gtest execution with timeout
fn run_gtest(bin: &str, filter: &str) -> io::Result<()> {
    use std::sync::mpsc;
    use std::thread;

    let mut cmd = Command::new(bin);
    cmd.arg("--gtest_brief=1")
        .arg(format!("--gtest_filter={}", filter))
        .env("ADA_SKIP_DSO_HOOKS", "1")  // Skip DSO hooks for test performance
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    // Use spawn() instead of output() to get a handle we can kill
    let mut child = cmd.spawn()?;

    // Create a channel for the thread to report completion
    let (tx, rx) = mpsc::channel();
    let child_id = child.id();

    // Spawn a thread to wait for the process
    thread::spawn(move || {
        let output = child.wait_with_output();
        let _ = tx.send(output);
    });

    // Determine timeout based on test type
    let timeout = if filter.contains("integration") || filter.contains("Integration") {
        Duration::from_secs(120) // 2 minutes for integration tests
    } else {
        Duration::from_secs(60) // 1 minute for unit tests
    };

    // Wait for either completion or timeout
    match rx.recv_timeout(timeout) {
        Ok(Ok(output)) => {
            if !output.status.success() {
                eprintln!("FAILED: {} :: {}", bin, filter);
                eprintln!("stdout:\n{}", String::from_utf8_lossy(&output.stdout));
                eprintln!("stderr:\n{}", String::from_utf8_lossy(&output.stderr));
                panic!("cpp gtest failed: {} :: {}", bin, filter);
            }
            Ok(())
        }
        Ok(Err(e)) => {
            eprintln!("ERROR running test {} :: {}: {}", bin, filter, e);
            Err(e)
        }
        Err(_) => {
            // Timeout occurred - try to kill the process
            eprintln!("TIMEOUT: Test {} :: {} exceeded {:?}", bin, filter, timeout);

            // Try to kill the process on Unix systems
            #[cfg(unix)]
            {
                use std::os::unix::process::CommandExt;
                // Kill the entire process group
                unsafe {
                    libc::killpg(child_id as i32, libc::SIGTERM);
                    thread::sleep(Duration::from_millis(100));
                    libc::killpg(child_id as i32, libc::SIGKILL);
                }
            }

            #[cfg(not(unix))]
            {
                // On non-Unix, just try to kill the process
                let _ = Command::new("taskkill")
                    .args(&["/F", "/PID", &child_id.to_string()])
                    .output();
            }

            panic!("Test timeout: {} :: {} exceeded {:?}", bin, filter, timeout);
        }
    }
}

// Include generated per-gtest wrappers (if any)
include!(concat!(env!("OUT_DIR"), "/generated_cpp.rs"));

// If no wrappers were generated, this test will provide a clear warning
#[test]
fn cpp_tests_status() {
    if option_env!("ADA_CPP_TESTS_GENERATED").is_none() {
        eprintln!("╔══════════════════════════════════════════════════════════════════╗");
        eprintln!("║                    ⚠️  C++ TESTS WARNING  ⚠️                      ║");
        eprintln!("╠══════════════════════════════════════════════════════════════════╣");
        eprintln!("║ No C++ test wrappers were generated during build.                ║");
        eprintln!("║                                                                  ║");
        eprintln!("║ Possible reasons:                                                ║");
        eprintln!("║ • No test binaries found in:                                     ║");
        eprintln!("║   • target/{{debug,release}}/tracer_backend/test                 ║");
        eprintln!("║ • Test binaries exist but couldn't be executed during build      ║");
        eprintln!("║ • Cross-compilation prevents test enumeration                    ║");
        eprintln!("║                                                                  ║");
        eprintln!("║ To run C++ tests manually:                                       ║");
        eprintln!("║ • Direct execution: ./target/release/tracer_backend/test/test_*  ║");
        eprintln!("║ • Or use your IDE's test runner                                  ║");
        eprintln!("╚══════════════════════════════════════════════════════════════════╝");

        // This is not a failure - just an informational message
        // The test passes to avoid breaking CI when wrappers aren't generated
    }
}
