//! Multimodal capture commands.
//!
//! Provides a minimal MVP that records screen, voice, and ADA trace output
//! into an .adabundle directory for handoff to an AI agent.

use anyhow::Context;
use clap::Subcommand;
use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tracer_backend::TracerController;

#[derive(Subcommand)]
pub enum CaptureCommands {
    /// Start a multimodal capture session
    Start {
        /// Path to the binary to trace
        binary: String,

        /// Output directory for .adabundle
        #[arg(short, long, default_value = "./captures")]
        output: PathBuf,

        /// Enable screen recording
        #[arg(long)]
        screen: bool,

        /// Enable voice recording (enables detail lane while active)
        #[arg(long)]
        voice: bool,

        /// Include microphone audio in screen recording
        #[arg(long, default_value_t = false)]
        screen_audio: bool,

        /// Audio device spec for ffmpeg (avfoundation), e.g. ":0"
        #[arg(long)]
        audio_device: Option<String>,

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
}

pub fn run(cmd: CaptureCommands) -> anyhow::Result<()> {
    match cmd {
        CaptureCommands::Start {
            binary,
            output,
            screen,
            voice,
            screen_audio,
            audio_device,
            pre_roll_ms,
            post_roll_ms,
            args,
        } => start_capture(
            &binary,
            &output,
            screen,
            voice,
            screen_audio,
            audio_device.as_deref(),
            pre_roll_ms,
            post_roll_ms,
            &args,
        ),
    }
}

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
    voice_log_path: Option<String>,
    screen_log_path: Option<String>,
    detail_when_voice: bool,
}

struct RecorderChild {
    name: &'static str,
    child: Child,
    output: PathBuf,
}

fn start_capture(
    binary: &str,
    output: &PathBuf,
    screen: bool,
    voice: bool,
    screen_audio: bool,
    audio_device: Option<&str>,
    pre_roll_ms: u32,
    post_roll_ms: u32,
    args: &[String],
) -> anyhow::Result<()> {
    let now_ms = current_time_ms();
    let session_name = format!("session_{}_{}", now_ms / 1000, sanitize_name(binary));
    let bundle_dir = output.join(format!("{}.adabundle", session_name));
    let trace_root = bundle_dir.join("trace");

    fs::create_dir_all(&trace_root)
        .with_context(|| format!("Failed to create trace directory at {}", trace_root.display()))?;

    println!("Starting capture session: {}", bundle_dir.display());
    println!("Tracing binary: {}", binary);

    // LCOV_EXCL_START - Integration path uses live tracer controller.
    let mut controller = map_tracer_result(TracerController::new(&trace_root))?;

    let mut spawn_args = vec![binary.to_string()];
    spawn_args.extend_from_slice(args);
    let pid = map_tracer_result(controller.spawn_suspended(binary, &spawn_args))?;
    map_tracer_result(controller.attach(pid))?;
    map_tracer_result(controller.install_hooks())?;

    if voice {
        map_tracer_result(controller.arm_trigger(pre_roll_ms, post_roll_ms))?;
        map_tracer_result(controller.fire_trigger())?;
    }

    map_tracer_result(controller.set_detail_enabled(voice))?;
    map_tracer_result(controller.resume())?;
    // LCOV_EXCL_STOP

    let mut screen_recorder = None;
    if screen {
        screen_recorder = Some(start_screen_recording(&bundle_dir, screen_audio)?);
    }

    let mut voice_recorder = None;
    if voice {
        voice_recorder = Some(start_voice_recording(&bundle_dir, audio_device)?);
    }

    println!("Capture running. Press Ctrl+C to stop.");

    let running = Arc::new(AtomicBool::new(true));
    let running_flag = running.clone();
    ctrlc::set_handler(move || {
        running_flag.store(false, Ordering::SeqCst);
    })?;

    while running.load(Ordering::SeqCst) {
        thread::sleep(Duration::from_millis(200));
    }

    if let Some(recorder) = voice_recorder.as_mut() {
        stop_recorder(recorder)?;
        // LCOV_EXCL_START - Integration cleanup uses live tracer controller.
        let _ = map_tracer_result(controller.disarm_trigger());
        let _ = map_tracer_result(controller.set_detail_enabled(false));
        // LCOV_EXCL_STOP
        let _ = encode_voice_to_aac(&bundle_dir)?;
    }

    if let Some(recorder) = screen_recorder.as_mut() {
        stop_recorder(recorder)?;
    }

    if let Err(err) = controller.detach() {
        eprintln!("Warning: failed to detach tracer ({err})");
    }
    drop(controller);

    let finished_at_ms = current_time_ms();
    let trace_session = find_latest_trace_session(&trace_root);

    fs::create_dir_all(&bundle_dir)
        .with_context(|| format!("Failed to create bundle directory at {}", bundle_dir.display()))?;

    let manifest = BundleManifest {
        version: 1,
        created_at_ms: now_ms,
        finished_at_ms,
        session_name,
        trace_root: path_as_string(&bundle_dir, &trace_root),
        trace_session: trace_session
            .as_ref()
            .map(|path| path_as_string(&bundle_dir, path)),
        screen_path: screen_recorder
            .as_ref()
            .map(|recorder| path_as_string(&bundle_dir, &recorder.output)),
        voice_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice.m4a")))
        } else {
            None
        },
        voice_lossless_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice.wav")))
        } else {
            None
        },
        voice_log_path: if voice_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("voice_ffmpeg.log")))
        } else {
            None
        },
        screen_log_path: if screen_recorder.is_some() {
            Some(path_as_string(&bundle_dir, &bundle_dir.join("screen_ffmpeg.log")))
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

    println!("\nBundle ready: {}", bundle_dir.display());
    println!("Manifest: {}", manifest_path.display());
    Ok(())
}

fn map_tracer_result<T, E>(result: Result<T, E>) -> anyhow::Result<T>
where
    E: std::fmt::Display,
{
    result.map_err(|err| anyhow::anyhow!(err.to_string()))
}

#[cfg(test)]
mod tests {
    use super::map_tracer_result;

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
}

fn start_screen_recording(bundle_dir: &Path, screen_audio: bool) -> anyhow::Result<RecorderChild> {
    let output = bundle_dir.join("screen.mp4");
    let log_path = bundle_dir.join("screen_ffmpeg.log");

    let mut cmd = Command::new("screencapture");
    cmd.arg("-v").arg("-J").arg("video").arg("-U");
    if screen_audio {
        cmd.arg("-g");
    }
    cmd.arg(&output);

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(open_log_file(&log_path)?)
        .spawn()
        .context("Failed to start screencapture")?;

    Ok(RecorderChild {
        name: "screen",
        child,
        output,
    })
}

fn start_voice_recording(
    bundle_dir: &Path,
    audio_device: Option<&str>,
) -> anyhow::Result<RecorderChild> {
    let ffmpeg = which::which("ffmpeg").context("ffmpeg not found in PATH")?;
    let output = bundle_dir.join("voice.wav");
    let log_path = bundle_dir.join("voice_ffmpeg.log");
    let device = audio_device.unwrap_or(":0");

    let mut cmd = Command::new(ffmpeg);
    cmd.args([
        "-f",
        "avfoundation",
        "-loglevel",
        "info",
        "-thread_queue_size",
        "1024",
        "-rtbufsize",
        "256M",
        "-i",
        device,
        "-ac",
        "1",
        "-c:a",
        "pcm_s16le",
        "-y",
    ]);
    cmd.arg(&output);

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(open_log_file(&log_path)?)
        .spawn()
        .context("Failed to start voice recorder (ffmpeg)")?;

    Ok(RecorderChild {
        name: "voice",
        child,
        output,
    })
}

fn stop_recorder(recorder: &mut RecorderChild) -> anyhow::Result<()> {
    if recorder.child.try_wait()?.is_some() {
        return Ok(());
    }

    send_signal(recorder.child.id(), libc::SIGINT)?;

    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if recorder.child.try_wait()?.is_some() {
            return Ok(());
        }
        thread::sleep(Duration::from_millis(100));
    }

    recorder.child.kill()?;
    let _ = recorder.child.wait();
    Ok(())
}

fn send_signal(pid: u32, signal: i32) -> std::io::Result<()> {
    let result = unsafe { libc::kill(pid as i32, signal) };
    if result == 0 {
        Ok(())
    } else {
        Err(std::io::Error::last_os_error())
    }
}

fn open_log_file(path: &Path) -> anyhow::Result<std::fs::File> {
    std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .with_context(|| format!("Failed to open log file {}", path.display()))
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

fn sanitize_name(binary: &str) -> String {
    let name = Path::new(binary)
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("app");

    name.chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() || ch == '_' || ch == '-' {
                ch
            } else {
                '_'
            }
        })
        .collect()
}

fn current_time_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}
