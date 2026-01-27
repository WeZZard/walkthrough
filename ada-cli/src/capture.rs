//! Multimodal capture commands.
//!
//! Provides a minimal MVP that records screen, voice, and ADA trace output
//! into an .adabundle directory for handoff to an AI agent.

use anyhow::{bail, Context};
use clap::Subcommand;
use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tracer_backend::TracerController;

use crate::session_state::{self, SessionState, SessionStatus};

#[derive(Subcommand)]
pub enum CaptureCommands {
    /// Start a multimodal capture session
    ///
    /// Session data is stored in ~/.ada/sessions/<session_id>/
    /// The session directory IS the bundle (contains manifest.json, trace/, etc.)
    Start {
        /// Path to the binary to trace
        binary: String,

        /// Disable screen recording (enabled by default)
        #[arg(long = "no-screen")]
        no_screen: bool,

        /// Disable voice recording (enabled by default)
        #[arg(long = "no-voice")]
        no_voice: bool,

        /// Detail pre-roll in ms (flight recorder)
        #[arg(long, default_value_t = 0)]
        pre_roll_ms: u32,

        /// Detail post-roll in ms (flight recorder)
        #[arg(long, default_value_t = 0)]
        post_roll_ms: u32,

        /// Arguments to pass to the binary
        #[arg(trailing_var_arg = true)]
        args: Vec<String>,
    },

    /// Stop a running capture session
    Stop {
        /// Session ID to stop (defaults to latest running session)
        #[arg(long)]
        session_id: Option<String>,
    },
}

// LCOV_EXCL_START - Entry point delegates to start_capture which requires live hardware
pub fn run(cmd: CaptureCommands) -> anyhow::Result<()> {
    match cmd {
        CaptureCommands::Start {
            binary,
            no_screen,
            no_voice,
            pre_roll_ms,
            post_roll_ms,
            args,
        } => start_capture(&binary, !no_screen, !no_voice, pre_roll_ms, post_roll_ms, &args),
        CaptureCommands::Stop { session_id } => stop_capture(session_id),
    }
}
// LCOV_EXCL_STOP

#[derive(Serialize)]
struct BundleManifest {
    version: u32,
    created_at_ms: u64,
    finished_at_ms: u64,
    session_name: String,
    trace_root: String,
    trace_session: Option<String>,
    screen_path: Option<String>,
    voice_path: Option<String>,
    voice_lossless_path: Option<String>,
    detail_when_voice: bool,
}

// LCOV_EXCL_START - macOS app bundle resolution and agent path setup

/// Resolve a user-provided path to an executable.
///
/// Handles:
/// - `/path/to/App.app` -> `/path/to/App.app/Contents/MacOS/<CFBundleExecutable>`
/// - `/path/to/binary` -> `/path/to/binary` (unchanged)
fn resolve_executable_path(path: &str) -> anyhow::Result<String> {
    let p = Path::new(path);

    // Check if path ends with .app
    if p.extension().map(|e| e == "app").unwrap_or(false) {
        // It's an app bundle - resolve to internal executable
        let info_plist = p.join("Contents/Info.plist");

        if !info_plist.exists() {
            bail!(
                "App bundle missing Info.plist: {}\n\
                 Expected: {}/Contents/Info.plist",
                path,
                path
            );
        }

        // Read CFBundleExecutable from Info.plist
        let executable_name = read_plist_key(&info_plist, "CFBundleExecutable")
            .with_context(|| format!("Failed to read executable name from {}", info_plist.display()))?;

        let executable_path = p.join("Contents/MacOS").join(&executable_name);

        if !executable_path.exists() {
            bail!(
                "Executable not found in app bundle: {}\n\
                 Expected: {}",
                path,
                executable_path.display()
            );
        }

        Ok(executable_path.to_string_lossy().to_string())
    } else {
        // Not an app bundle - use as-is
        Ok(path.to_string())
    }
}

/// Read a key from Info.plist using PlistBuddy
fn read_plist_key(plist_path: &Path, key: &str) -> anyhow::Result<String> {
    let output = Command::new("/usr/libexec/PlistBuddy")
        .arg("-c")
        .arg(format!("Print :{}", key))
        .arg(plist_path)
        .output()
        .context("Failed to execute PlistBuddy")?;

    if !output.status.success() {
        bail!("PlistBuddy failed: {}", String::from_utf8_lossy(&output.stderr));
    }

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

/// Ensure ADA_AGENT_RPATH_SEARCH_PATHS is set so the tracer can find libfrida_agent.dylib
fn ensure_agent_rpath() -> anyhow::Result<()> {
    if let Ok(existing) = std::env::var("ADA_AGENT_RPATH_SEARCH_PATHS") {
        if !existing.trim().is_empty() {
            return Ok(());
        }
    }

    let mut candidates = Vec::new();

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            candidates.push(dir.to_path_buf());
            if let Some(target_root) = dir.parent() {
                candidates.push(target_root.join("tracer_backend/lib"));
                candidates.push(target_root.join("build"));
            }
        }
    }

    let target_dir = std::env::var("CARGO_TARGET_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("target"));
    let profile = if cfg!(debug_assertions) { "debug" } else { "release" };
    candidates.push(target_dir.join(profile).join("tracer_backend/lib"));

    let mut search_paths = Vec::new();
    #[cfg(target_os = "macos")]
    let lib_name = "libfrida_agent.dylib";
    #[cfg(not(target_os = "macos"))]
    let lib_name = "libfrida_agent.so";

    for candidate in candidates {
        let lib_path = candidate.join(lib_name);
        if lib_path.exists() {
            search_paths.push(candidate);
        }
    }

    if search_paths.is_empty() {
        bail!("libfrida_agent.dylib not found; set ADA_AGENT_RPATH_SEARCH_PATHS");
    }

    let joined = search_paths
        .iter()
        .map(|path| path.to_string_lossy())
        .collect::<Vec<_>>()
        .join(":");
    std::env::set_var("ADA_AGENT_RPATH_SEARCH_PATHS", joined);
    Ok(())
}

/// Find the ada-recorder binary
fn find_ada_recorder() -> anyhow::Result<PathBuf> {
    // 1. Same directory as ada binary
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let recorder = dir.join("ada-recorder");
            if recorder.exists() {
                return Ok(recorder);
            }
        }
    }

    // 2. PATH lookup
    if let Ok(path) = which::which("ada-recorder") {
        return Ok(path);
    }

    bail!(
        "ada-recorder not found.\n\
         Install it by running:\n\
         cd ada-recorder/macos && swift build -c release\n\
         cp .build/release/ada-recorder <ada-binary-directory>/"
    )
}

// LCOV_EXCL_STOP

// LCOV_EXCL_START - Integration code requires live tracer and capture hardware

fn start_capture(
    binary: &str,
    screen: bool,
    voice: bool,
    pre_roll_ms: u32,
    post_roll_ms: u32,
    args: &[String],
) -> anyhow::Result<()> {
    // Clean up any orphaned sessions first
    if let Err(e) = session_state::cleanup_orphaned() {
        tracing::warn!("Failed to cleanup orphaned sessions: {}", e);
    }

    // Ensure agent library can be found
    ensure_agent_rpath()?;

    // Resolve .app bundle to executable path
    let binary = resolve_executable_path(binary)?;
    let binary = binary.as_str();

    let now_ms = current_time_ms();
    let app_info = session_state::extract_app_info(binary);
    let session_id = session_state::generate_session_id(&app_info.name);

    // Session directory IS the bundle directory
    let bundle_dir = session_state::session_dir(&session_id)?;
    let trace_root = bundle_dir.join("trace");
    let session_name = session_id.clone();

    fs::create_dir_all(&trace_root)
        .with_context(|| format!("Failed to create trace directory at {}", trace_root.display()))?;

    // Register session state
    let session = SessionState {
        session_id: session_id.clone(),
        session_path: bundle_dir.clone(),
        start_time: chrono::Utc::now().to_rfc3339(),
        end_time: None,
        app_info: app_info.clone(),
        status: SessionStatus::Running,
        pid: None, // Will be set after spawn
        capture_pid: Some(std::process::id()),
    };

    if let Err(e) = session_state::register(&session) {
        tracing::warn!("Failed to register session state: {}", e);
    }

    // Output session info for Claude context
    println!("ADA Session Started:");
    println!("  ID: {}", session_id);
    println!(
        "  App: {} ({})",
        app_info.name,
        app_info.bundle_id.as_deref().unwrap_or("no bundle id")
    );
    println!("  Binary: {}", binary);
    println!("  Bundle: {}", bundle_dir.display());
    println!("  Time: {}", session.start_time);

    let mut controller = map_tracer_result(TracerController::new(&trace_root))?;

    let mut spawn_args = vec![binary.to_string()];
    spawn_args.extend_from_slice(args);
    let target_pid = map_tracer_result(controller.spawn_suspended(binary, &spawn_args))?;

    // Update session with target PID
    if let Ok(Some(mut session)) = session_state::get(&session_id) {
        session.pid = Some(target_pid);
        let _ = session_state::update(&session_id, &session);
    }

    map_tracer_result(controller.attach(target_pid))?;
    map_tracer_result(controller.install_hooks())?;

    if voice {
        map_tracer_result(controller.arm_trigger(pre_roll_ms, post_roll_ms))?;
        map_tracer_result(controller.fire_trigger())?;
    }

    map_tracer_result(controller.set_detail_enabled(voice))?;
    map_tracer_result(controller.resume())?;

    // Start ada-recorder for screen/voice recording
    let mut recorder_child = None;
    if screen || voice {
        recorder_child = Some(start_ada_recorder(&bundle_dir, screen, voice)?);
    }

    println!("Capture running. Press Ctrl+C to stop.");

    let running = Arc::new(AtomicBool::new(true));
    let running_flag = running.clone();
    ctrlc::set_handler(move || {
        running_flag.store(false, Ordering::SeqCst);
    })?;

    // Main loop: monitor both Ctrl+C and target process
    let exit_reason = wait_for_termination(&running, target_pid);

    println!("\n{}", exit_reason);

    // Stop recorder first (sends SIGTERM)
    if let Some(mut child) = recorder_child {
        stop_ada_recorder(&mut child)?;
    }

    // Cleanup tracer
    if voice {
        let _ = map_tracer_result(controller.disarm_trigger());
        let _ = map_tracer_result(controller.set_detail_enabled(false));
    }

    if let Err(err) = controller.detach() {
        eprintln!("Warning: failed to detach tracer ({err})");
    }
    drop(controller);

    // Encode voice to AAC if we have a WAV file
    let voice_wav = bundle_dir.join("voice.wav");
    if voice_wav.exists() {
        if let Err(e) = encode_voice_to_aac(&bundle_dir) {
            eprintln!("Warning: Failed to encode voice to AAC: {}", e);
        }
    }

    let finished_at_ms = current_time_ms();
    let trace_session = find_latest_trace_session(&trace_root);

    // Write manifest
    let manifest = BundleManifest {
        version: 1,
        created_at_ms: now_ms,
        finished_at_ms,
        session_name,
        trace_root: path_as_string(&bundle_dir, &trace_root),
        trace_session: trace_session
            .as_ref()
            .map(|path| path_as_string(&bundle_dir, path)),
        screen_path: if screen && bundle_dir.join("screen.mp4").exists() {
            Some("screen.mp4".to_string())
        } else {
            None
        },
        voice_path: if voice && bundle_dir.join("voice.m4a").exists() {
            Some("voice.m4a".to_string())
        } else {
            None
        },
        voice_lossless_path: if voice && voice_wav.exists() {
            Some("voice.wav".to_string())
        } else {
            None
        },
        detail_when_voice: voice,
    };

    let manifest_path = bundle_dir.join("manifest.json");
    let manifest_json = serde_json::to_string_pretty(&manifest)?;
    fs::write(&manifest_path, manifest_json)
        .with_context(|| format!("Failed to write manifest at {}", manifest_path.display()))?;

    notify_ready(&bundle_dir);

    // Mark session as complete
    if let Ok(Some(mut session)) = session_state::get(&session_id) {
        session.status = SessionStatus::Complete;
        session.end_time = Some(chrono::Utc::now().to_rfc3339());
        let _ = session_state::update(&session_id, &session);
    }

    println!("ADA Session Complete:");
    println!("  ID: {}", session_id);
    println!("  Bundle: {}", bundle_dir.display());
    println!("  Manifest: {}", manifest_path.display());
    Ok(())
}

/// Wait for either Ctrl+C or target process termination
fn wait_for_termination(running: &Arc<AtomicBool>, target_pid: u32) -> String {
    loop {
        // Check Ctrl+C
        if !running.load(Ordering::SeqCst) {
            return "User interrupted (Ctrl+C)".to_string();
        }

        // Check if target process is still alive using waitpid with WNOHANG
        let mut status: i32 = 0;
        let result = unsafe { libc::waitpid(target_pid as i32, &mut status, libc::WNOHANG) };

        if result > 0 {
            // Process state changed
            if libc::WIFEXITED(status) {
                let exit_code = libc::WEXITSTATUS(status);
                return format!("Target process exited with code {}", exit_code);
            } else if libc::WIFSIGNALED(status) {
                let signal = libc::WTERMSIG(status);
                let signal_name = match signal {
                    libc::SIGTERM => "SIGTERM",
                    libc::SIGKILL => "SIGKILL",
                    libc::SIGSEGV => "SIGSEGV (crash)",
                    libc::SIGABRT => "SIGABRT (abort)",
                    libc::SIGBUS => "SIGBUS",
                    libc::SIGFPE => "SIGFPE",
                    libc::SIGILL => "SIGILL",
                    _ => "unknown signal",
                };
                return format!("Target process killed by {} ({})", signal_name, signal);
            }
        } else if result == -1 {
            // Error - process might not be our child, use kill(0) to check
            let alive = unsafe { libc::kill(target_pid as i32, 0) };
            if alive != 0 {
                return "Target process terminated".to_string();
            }
        }

        thread::sleep(Duration::from_millis(100));
    }
}

/// Start ada-recorder subprocess for screen and voice recording
fn start_ada_recorder(bundle_dir: &Path, screen: bool, voice: bool) -> anyhow::Result<Child> {
    let recorder_path = find_ada_recorder()?;

    let mut cmd = Command::new(&recorder_path);
    cmd.arg("start")
        .arg("--output-dir")
        .arg(bundle_dir);

    if !screen {
        cmd.arg("--no-screen");
    }
    if !voice {
        cmd.arg("--no-voice");
    }

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .spawn()
        .with_context(|| format!("Failed to start ada-recorder at {}", recorder_path.display()))?;

    // Give recorder time to initialize
    thread::sleep(Duration::from_millis(500));

    Ok(child)
}

/// Stop ada-recorder gracefully
fn stop_ada_recorder(child: &mut Child) -> anyhow::Result<()> {
    // Check if already exited
    if child.try_wait()?.is_some() {
        return Ok(());
    }

    // Send SIGTERM for graceful shutdown
    let pid = child.id();
    let result = unsafe { libc::kill(pid as i32, libc::SIGTERM) };
    if result != 0 {
        // Process might already be gone
        return Ok(());
    }

    // Wait up to 5 seconds for graceful shutdown
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    while std::time::Instant::now() < deadline {
        if child.try_wait()?.is_some() {
            return Ok(());
        }
        thread::sleep(Duration::from_millis(100));
    }

    // Force kill if still running
    eprintln!("Warning: ada-recorder did not stop gracefully, forcing termination");
    let _ = child.kill();
    let _ = child.wait();
    Ok(())
}

/// Stop a running capture session
fn stop_capture(session_id: Option<String>) -> anyhow::Result<()> {
    // Find the session to stop
    let session = if let Some(id) = session_id {
        session_state::get(&id)?.ok_or_else(|| anyhow::anyhow!("Session {} not found", id))?
    } else {
        session_state::latest_running()?.ok_or_else(|| anyhow::anyhow!("No running sessions found"))?
    };

    if session.status != SessionStatus::Running {
        bail!("Session {} is not running (status: {:?})", session.session_id, session.status);
    }

    // Send SIGINT to the capture process
    if let Some(capture_pid) = session.capture_pid {
        println!("Stopping session {}...", session.session_id);
        let result = unsafe { libc::kill(capture_pid as i32, libc::SIGINT) };
        if result == 0 {
            println!("Stop signal sent to capture process (PID {})", capture_pid);
            println!("Session will complete shortly.");
        } else {
            let err = std::io::Error::last_os_error();
            if err.raw_os_error() == Some(libc::ESRCH) {
                // Process doesn't exist - mark as failed
                eprintln!("Capture process not found. Marking session as failed.");
                if let Ok(Some(mut s)) = session_state::get(&session.session_id) {
                    s.status = SessionStatus::Failed;
                    s.end_time = Some(chrono::Utc::now().to_rfc3339());
                    let _ = session_state::update(&session.session_id, &s);
                }
            } else {
                bail!("Failed to send stop signal: {}", err);
            }
        }
    } else {
        bail!("Session {} has no capture process ID", session.session_id);
    }

    Ok(())
}

// LCOV_EXCL_STOP

fn map_tracer_result<T, E>(result: Result<T, E>) -> anyhow::Result<T>
where
    E: std::fmt::Display,
{
    result.map_err(|err| anyhow::anyhow!(err.to_string()))
}

#[cfg(test)]
mod tests {
    use super::{map_tracer_result, resolve_executable_path};

    #[test]
    fn map_tracer_result_ok() {
        let value = map_tracer_result::<_, &str>(Ok(42)).expect("ok result");
        assert_eq!(value, 42);
    }

    #[test]
    fn map_tracer_result_err() {
        let err = map_tracer_result::<(), &str>(Err("boom")).expect_err("err result");
        assert!(err.to_string().contains("boom"));
    }

    #[test]
    fn resolve_executable_path__direct_binary__then_unchanged() {
        let result = resolve_executable_path("/usr/bin/ls").unwrap();
        assert_eq!(result, "/usr/bin/ls");
    }

    #[test]
    fn resolve_executable_path__app_bundle__then_resolved() {
        // Use Calculator.app as it exists on all macOS systems
        let result = resolve_executable_path("/System/Applications/Calculator.app").unwrap();
        assert_eq!(
            result,
            "/System/Applications/Calculator.app/Contents/MacOS/Calculator"
        );
    }

    #[test]
    fn resolve_executable_path__nonexistent_app__then_error() {
        let result = resolve_executable_path("/nonexistent/Fake.app");
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Info.plist"));
    }
}

fn encode_voice_to_aac(bundle_dir: &Path) -> anyhow::Result<PathBuf> {
    let ffmpeg = which::which("ffmpeg").context("ffmpeg not found in PATH")?;
    let input = bundle_dir.join("voice.wav");
    let output = bundle_dir.join("voice.m4a");

    if !input.exists() {
        anyhow::bail!("voice.wav not found at {}", input.display());
    }

    let status = Command::new(ffmpeg)
        .args([
            "-y",
            "-i",
            input.to_str().unwrap_or_default(),
            "-c:a",
            "aac",
            "-b:a",
            "128k",
        ])
        .arg(&output)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .context("Failed to encode voice.m4a")?;

    if !status.success() {
        anyhow::bail!("ffmpeg failed while encoding voice.m4a");
    }

    Ok(output)
}

fn find_latest_trace_session(trace_root: &Path) -> Option<PathBuf> {
    let mut sessions: Vec<PathBuf> = fs::read_dir(trace_root)
        .ok()?
        .filter_map(|entry| entry.ok())
        .map(|entry| entry.path())
        .filter(|path| {
            path.file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.starts_with("session_"))
                .unwrap_or(false)
        })
        .collect();

    sessions.sort();
    let session_dir = sessions.pop()?;

    let mut pid_dirs: Vec<PathBuf> = fs::read_dir(&session_dir)
        .ok()?
        .filter_map(|entry| entry.ok())
        .map(|entry| entry.path())
        .filter(|path| path.is_dir())
        .collect();

    pid_dirs.sort();
    pid_dirs.pop().or(Some(session_dir))
}

fn notify_ready(bundle_dir: &Path) {
    let message = format!("Bundle ready: {}", bundle_dir.display());
    let script = format!(
        "display notification \"{}\" with title \"ADA Capture\"",
        message.replace('"', "'")
    );

    let _ = Command::new("osascript")
        .arg("-e")
        .arg(script)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn path_as_string(bundle_dir: &Path, path: &Path) -> String {
    path.strip_prefix(bundle_dir)
        .unwrap_or(path)
        .to_string_lossy()
        .to_string()
}

fn current_time_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}
