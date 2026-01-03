//! Tracer CLI - Rust implementation of the tracer POC
//!
//! This demonstrates Cargo orchestrating the build and providing
//! the main entry point for the tracer system.

use anyhow::Result;
use std::env;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use tracer_backend::{ffi::ProcessState, TracerController};

static RUNNING: AtomicBool = AtomicBool::new(true);

fn print_usage(program: &str) {
    println!("Usage: {} <mode> <target> [options]", program);
    println!("\nModes:");
    println!("  spawn    - Spawn and trace a new process");
    println!("  attach   - Attach to an existing process");
    println!("\nExamples:");
    println!("  {} spawn ./test_cli --wait", program);
    println!("  {} spawn ./test_runloop", program);
    println!("  {} attach 1234", program);
    println!("\nOptions:");
    println!("  --output <dir>   - Output directory for traces (default: ./traces)");
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        print_usage(&args[0]);
        std::process::exit(1);
    }

    let mode = &args[1];
    let target = &args[2];
    let mut output_dir = PathBuf::from("./traces");

    // Parse options
    let mut i = 3;
    let mut target_args = Vec::new();
    while i < args.len() {
        if args[i] == "--output" && i + 1 < args.len() {
            output_dir = PathBuf::from(&args[i + 1]);
            i += 2;
        } else {
            target_args.push(args[i].clone());
            i += 1;
        }
    }

    // Setup Ctrl+C handler
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();
    ctrlc::set_handler(move || {
        println!("\nReceived Ctrl+C, shutting down...");
        r.store(false, Ordering::SeqCst);
        RUNNING.store(false, Ordering::SeqCst);
    })?;

    println!("=== ADA Tracer (Rust) ===");
    println!("Output directory: {}", output_dir.display());

    // Create output directory
    std::fs::create_dir_all(&output_dir)?;

    // Create controller
    let mut controller = TracerController::new(&output_dir)?;
    println!("Controller created successfully");

    let pid = match mode.as_str() {
        "spawn" => {
            println!("Spawning process: {}", target);

            // Build argv - first arg should be the program name
            let mut spawn_args = vec![target.clone()];
            spawn_args.extend(target_args);

            let pid = controller.spawn_suspended(target, &spawn_args)?;
            println!("Process spawned with PID: {} (suspended)", pid);

            // Attach to spawned process
            println!("Attaching to PID {}...", pid);
            controller.attach(pid)?;

            pid
        }
        "attach" => {
            let pid: u32 = target
                .parse()
                .map_err(|_| anyhow::anyhow!("Invalid PID: {}", target))?;

            println!("Attaching to PID {}...", pid);
            controller.attach(pid)?;

            pid
        }
        _ => {
            eprintln!("Unknown mode: {}", mode);
            print_usage(&args[0]);
            std::process::exit(1);
        }
    };

    println!("Successfully attached to PID {}", pid);

    // Install hooks
    println!("Installing hooks...");
    controller.install_hooks()?;
    println!("Hooks installed successfully");

    // Resume process if spawned
    if mode == "spawn" {
        println!("Resuming process...");
        controller.resume()?;
        println!("Process resumed");
    }

    // Monitor loop
    println!("\n=== Tracing Active ===");
    println!("Press Ctrl+C to stop\n");

    let mut tick = 0;
    while running.load(Ordering::SeqCst) {
        thread::sleep(Duration::from_secs(1));
        tick += 1;

        // Print statistics every 5 seconds
        if tick % 5 == 0 {
            let stats = controller.get_stats();
            println!(
                "[Stats] Events: {}, Dropped: {}, Bytes: {}, Cycles: {}",
                stats.events_captured,
                stats.events_dropped,
                stats.bytes_written,
                stats.drain_cycles
            );
        }

        // Check if process is still running
        let state = controller.get_state();
        if state != ProcessState::Running && state != ProcessState::Attached {
            println!("Process has terminated");
            break;
        }
    }

    // Detach and cleanup
    println!("\nDetaching from process...");
    controller.detach()?;

    // Print final statistics
    let final_stats = controller.get_stats();
    println!("\n=== Final Statistics ===");
    println!("Events captured: {}", final_stats.events_captured);
    println!("Events dropped:  {}", final_stats.events_dropped);
    println!("Bytes written:   {}", final_stats.bytes_written);
    println!("Drain cycles:    {}", final_stats.drain_cycles);

    // Find and display session directory
    if let Ok(entries) = std::fs::read_dir(&output_dir) {
        let mut sessions: Vec<_> = entries
            .filter_map(|e| e.ok())
            .filter(|e| {
                e.file_name()
                    .to_str()
                    .map(|n| n.starts_with("session_"))
                    .unwrap_or(false)
            })
            .collect();

        // Sort by name (timestamp is in name, so alphabetical = chronological)
        sessions.sort_by_key(|e| e.file_name());

        // Get the most recent session
        if let Some(session) = sessions.last() {
            let session_path = session.path();
            // Find the pid_XXXXX subdirectory
            if let Ok(pid_entries) = std::fs::read_dir(&session_path) {
                for pid_entry in pid_entries.filter_map(|e| e.ok()) {
                    let pid_dir = pid_entry.path();
                    if pid_dir.is_dir() {
                        println!("\n=== Session Output ===");
                        println!("Session directory: {}", pid_dir.display());

                        // Count ATF files
                        if let Ok(thread_entries) = std::fs::read_dir(&pid_dir) {
                            let atf_count = thread_entries
                                .filter_map(|e| e.ok())
                                .filter(|e| e.path().is_dir())
                                .count();
                            println!("Thread directories: {}", atf_count);
                        }
                        break;
                    }
                }
            }
        }
    }

    println!("\nTracer completed successfully");
    Ok(())
}

// Add ctrlc dependency
#[cfg(not(test))]
use ctrlc;
