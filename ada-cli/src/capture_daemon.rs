//! ADA capture daemon for GUI control.
//!
//! Line-delimited JSON commands over stdin/stdout:
//! {"cmd":"start_session", "binary":"/path", "args":[...], "output":"/path"}
//! {"cmd":"stop_session"}
//! {"cmd":"start_voice", "audio_device":":0"}
//! {"cmd":"stop_voice"}
//! {"cmd":"status"}

use anyhow::Context;
use serde::{Deserialize, Serialize};
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};
use tracer_backend::TracerController;

const JSON_PREFIX: &str = "ADA_JSON:";

#[derive(Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
enum DaemonCommand {
    StartSession {
        binary: Option<String>,
        #[serde(default)]
        args: Vec<String>,
        output: Option<String>,
        pid: Option<u32>,
    },
    StopSession,
    StartVoice {
        audio_device: Option<String>,
    },
    StopVoice,
    Status,
}

#[derive(Serialize)]
struct DaemonResponse<T: Serialize> {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    data: Option<T>,
}

#[derive(Serialize)]
struct SessionInfo {
    session_root: String,
    trace_root: String,
    trace_session: Option<String>,
    is_voice_active: bool,
}

#[derive(Serialize)]
struct VoiceStartInfo {
    is_session_active: bool,
    is_voice_active: bool,
    session_root: String,
    trace_root: String,
    trace_session: Option<String>,
    segment_dir: String,
    voice_path: String,
}

#[derive(Serialize)]
struct BundleInfo {
    bundle_path: String,
    trace_session: Option<String>,
}

#[derive(Serialize)]
struct StatusInfo {
    is_session_active: bool,
    is_voice_active: bool,
    session_root: Option<String>,
    trace_root: Option<String>,
    trace_session: Option<String>,
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
    segment_start_ms: u64,
    segment_end_ms: u64,
    segment_index: u32,
}

struct RecorderChild {
    child: Child,
}

struct CaptureSession {
    controller: TracerController,
    session_root: PathBuf,
    trace_root: PathBuf,
    trace_session: Option<PathBuf>,
    screen_recorder: Option<RecorderChild>,
    segment_index: u32,
    segment_start_ms: Option<u64>,
    active_segment_dir: Option<PathBuf>,
    is_voice_active: bool,
}

impl CaptureSession {
    fn start(
        binary: Option<&str>,
        args: &[String],
        output_base: Option<&str>,
        pid: Option<u32>,
    ) -> anyhow::Result<Self> {
        ensure_agent_rpath()?;

        let output_base = resolve_output_base(output_base)?;
        let session_name = format!(
            "session_{}_{}",
            current_time_ms() / 1000,
            pid.map(|value| format!("pid_{}", value))
                .or_else(|| binary.map(sanitize_name))
                .as_deref()
                .unwrap_or("session")
        );
        let session_root = output_base.join(&session_name);
        let trace_root = session_root.join("trace");

        fs::create_dir_all(&trace_root).with_context(|| {
            format!("Failed to create trace root at {}", trace_root.display())
        })?;

        // LCOV_EXCL_START - Integration path uses live tracer controller.
        let mut controller = map_tracer_result(TracerController::new(&trace_root))?;

        match (binary, pid) {
            (Some(binary), None) => {
                let mut spawn_args = vec![binary.to_string()];
                spawn_args.extend_from_slice(args);

                let pid = map_tracer_result(controller.spawn_suspended(binary, &spawn_args))?;
                map_tracer_result(controller.attach(pid))?;
                map_tracer_result(controller.install_hooks())?;
                map_tracer_result(controller.set_detail_enabled(false))?;
                map_tracer_result(controller.resume())?;
            }
            (None, Some(pid)) => {
                map_tracer_result(controller.attach(pid))?;
                map_tracer_result(controller.install_hooks())?;
                map_tracer_result(controller.set_detail_enabled(false))?;
                map_tracer_result(controller.start_session())?;
            }
            _ => {
                anyhow::bail!("start_session requires either binary or pid");
            }
        }
        // LCOV_EXCL_STOP

        let trace_session = find_latest_trace_session(&trace_root);

        Ok(Self {
            controller,
            session_root,
            trace_root,
            trace_session,
            screen_recorder: None,
            segment_index: 0,
            segment_start_ms: None,
            active_segment_dir: None,
            is_voice_active: false,
        })
    }

    fn stop(&mut self) -> anyhow::Result<()> {
        if self.is_voice_active || self.screen_recorder.is_some() {
            let _ = self.stop_voice();
        }
        // LCOV_EXCL_START - Integration cleanup uses live tracer controller.
        let _ = map_tracer_result(self.controller.set_detail_enabled(false));
        let _ = map_tracer_result(self.controller.disarm_trigger());
        let _ = map_tracer_result(self.controller.detach());
        // LCOV_EXCL_STOP
        Ok(())
    }

    fn start_voice(&mut self, _audio_device: Option<&str>) -> anyhow::Result<PathBuf> {
        if self.is_voice_active || self.screen_recorder.is_some() {
            anyhow::bail!("voice recording already active");
        }

        self.segment_index += 1;
        let segment_id = format!("segment_{:03}", self.segment_index);
        let segment_dir = self.session_root.join("recordings").join(&segment_id);
        fs::create_dir_all(&segment_dir).with_context(|| {
            format!("Failed to create segment dir at {}", segment_dir.display())
        })?;

        // LCOV_EXCL_START - Integration path uses live tracer controller.
        map_tracer_result(self.controller.arm_trigger(0, 0))?;
        map_tracer_result(self.controller.fire_trigger())?;
        map_tracer_result(self.controller.set_detail_enabled(true))?;
        // LCOV_EXCL_STOP

        let screen_recorder = start_screen_recording(&segment_dir)?;
        self.screen_recorder = Some(screen_recorder);
        self.segment_start_ms = Some(current_time_ms());
        self.active_segment_dir = Some(segment_dir.clone());
        self.is_voice_active = true;

        Ok(segment_dir)
    }

    fn stop_voice(&mut self) -> anyhow::Result<BundleInfo> {
        if !self.is_voice_active {
            anyhow::bail!("voice recording not active");
        }

        let segment_start_ms = self.segment_start_ms.take().unwrap_or_else(current_time_ms);
        let segment_end_ms = current_time_ms();
        let segment_index = self.segment_index;

        if let Some(recorder) = self.screen_recorder.as_mut() {
            stop_recorder(recorder)?;
        }

        self.screen_recorder = None;
        self.is_voice_active = false;

        // LCOV_EXCL_START - Integration cleanup uses live tracer controller.
        let _ = map_tracer_result(self.controller.set_detail_enabled(false));
        let _ = map_tracer_result(self.controller.disarm_trigger());
        // LCOV_EXCL_STOP

        if self.trace_session.is_none() {
            self.trace_session = find_latest_trace_session(&self.trace_root);
        }

        let trace_session = self
            .trace_session
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("trace session not found"))?;

        let bundle_name = format!("segment_{:03}.adabundle", segment_index);
        let bundle_dir = self.session_root.join("bundles").join(&bundle_name);
        fs::create_dir_all(&bundle_dir).with_context(|| {
            format!("Failed to create bundle directory at {}", bundle_dir.display())
        })?;

        let recordings_dir = self.session_root.join("recordings");
        let segment_dir = self
            .active_segment_dir
            .take()
            .unwrap_or_else(|| recordings_dir.join(format!("segment_{:03}", segment_index)));
        let screen_path = move_if_exists(&segment_dir.join("screen.mp4"), &bundle_dir)?;
        let voice_wav_path = segment_dir.join("voice.wav");
        if voice_wav_path.exists() {
            let _ = encode_voice_to_aac(&segment_dir)?;
        }
        let voice_path = move_if_exists(&segment_dir.join("voice.m4a"), &bundle_dir)?;
        let voice_lossless_path = move_if_exists(&voice_wav_path, &bundle_dir)?;
        let voice_log_path = move_if_exists(&segment_dir.join("voice_ffmpeg.log"), &bundle_dir)?;
        let screen_log_path = move_if_exists(&segment_dir.join("screen_ffmpeg.log"), &bundle_dir)?;

        let trace_bundle_path = bundle_dir.join("trace");
        copy_dir_recursive(trace_session, &trace_bundle_path)?;

        let manifest = BundleManifest {
            version: 1,
            created_at_ms: segment_start_ms,
            finished_at_ms: segment_end_ms,
            session_name: self
                .session_root
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("session")
                .to_string(),
            trace_root: "trace".to_string(),
            trace_session: Some("trace".to_string()),
            screen_path: screen_path
                .as_ref()
                .map(|path| path.to_string_lossy().to_string()),
            voice_path: voice_path
                .as_ref()
                .map(|path| path.to_string_lossy().to_string()),
            voice_lossless_path: voice_lossless_path
                .as_ref()
                .map(|path| path.to_string_lossy().to_string()),
            voice_log_path: voice_log_path
                .as_ref()
                .map(|path| path.to_string_lossy().to_string()),
            screen_log_path: screen_log_path
                .as_ref()
                .map(|path| path.to_string_lossy().to_string()),
            detail_when_voice: true,
            segment_start_ms,
            segment_end_ms,
            segment_index,
        };

        let manifest_path = bundle_dir.join("manifest.json");
        let manifest_json = serde_json::to_string_pretty(&manifest)?;
        fs::write(&manifest_path, manifest_json)
            .with_context(|| format!("Failed to write manifest at {}", manifest_path.display()))?;

        Ok(BundleInfo {
            bundle_path: bundle_dir.to_string_lossy().to_string(),
            trace_session: Some(trace_bundle_path.to_string_lossy().to_string()),
        })
    }
}

fn main() -> anyhow::Result<()> {
    let stdin = io::stdin();
    let mut stdout = io::stdout();

    let mut session: Option<CaptureSession> = None;

    for line in stdin.lock().lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }

        let cmd: Result<DaemonCommand, _> = serde_json::from_str(&line);
        match cmd {
            Ok(command) => {
                let response = handle_command(command, &mut session);
                let json = serde_json::to_string(&response)?;
                writeln!(stdout, "{}{}", JSON_PREFIX, json)?;
                stdout.flush()?;
            }
            Err(err) => {
                let response = DaemonResponse::<serde_json::Value> {
                    ok: false,
                    error: Some(format!("invalid command: {err}")),
                    data: None,
                };
                let json = serde_json::to_string(&response)?;
                writeln!(stdout, "{}{}", JSON_PREFIX, json)?;
                stdout.flush()?;
            }
        }
    }

    if let Some(mut active) = session.take() {
        let _ = active.stop();
    }

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
        let value = map_tracer_result::<_, &str>(Ok(7)).expect("ok result");
        assert_eq!(value, 7);
    }

    #[test]
    fn map_tracer_result_err() {
        let err = map_tracer_result::<(), &str>(Err("boom")).expect_err("err result");
        assert!(err.to_string().contains("boom"));
    }
}

fn handle_command(
    command: DaemonCommand,
    session: &mut Option<CaptureSession>,
) -> DaemonResponse<serde_json::Value> {
    match command {
        DaemonCommand::StartSession { binary, args, output, pid } => {
            if session.is_some() {
                return DaemonResponse {
                    ok: false,
                    error: Some("session already active".to_string()),
                    data: None,
                };
            }

            match CaptureSession::start(binary.as_deref(), &args, output.as_deref(), pid) {
                Ok(active) => {
                    let info = SessionInfo {
                        session_root: active.session_root.to_string_lossy().to_string(),
                        trace_root: active.trace_root.to_string_lossy().to_string(),
                        trace_session: active
                            .trace_session
                            .as_ref()
                            .map(|path| path.to_string_lossy().to_string()),
                        is_voice_active: false,
                    };
                    *session = Some(active);
                    DaemonResponse {
                        ok: true,
                        error: None,
                        data: Some(serde_json::to_value(info).unwrap_or(serde_json::Value::Null)),
                    }
                }
                Err(err) => DaemonResponse {
                    ok: false,
                    error: Some(err.to_string()),
                    data: None,
                },
            }
        }
        DaemonCommand::StopSession => {
            if let Some(mut active) = session.take() {
                let _ = active.stop();
            }
            DaemonResponse {
                ok: true,
                error: None,
                data: Some(serde_json::to_value(StatusInfo {
                    is_session_active: false,
                    is_voice_active: false,
                    session_root: None,
                    trace_root: None,
                    trace_session: None,
                })
                .unwrap_or(serde_json::Value::Null)),
            }
        }
        DaemonCommand::StartVoice { audio_device } => {
            if let Some(active) = session.as_mut() {
                match active.start_voice(audio_device.as_deref()) {
                    Ok(segment_dir) => DaemonResponse {
                        ok: true,
                        error: None,
                        data: Some(serde_json::to_value(VoiceStartInfo {
                            is_session_active: true,
                            is_voice_active: true,
                            session_root: active.session_root.to_string_lossy().to_string(),
                            trace_root: active.trace_root.to_string_lossy().to_string(),
                            trace_session: active
                                .trace_session
                                .as_ref()
                                .map(|path| path.to_string_lossy().to_string()),
                            segment_dir: segment_dir.to_string_lossy().to_string(),
                            voice_path: segment_dir
                                .join("voice.wav")
                                .to_string_lossy()
                                .to_string(),
                        })
                        .unwrap_or(serde_json::Value::Null)),
                    },
                    Err(err) => DaemonResponse {
                        ok: false,
                        error: Some(err.to_string()),
                        data: None,
                    },
                }
            } else {
                DaemonResponse {
                    ok: false,
                    error: Some("no active session".to_string()),
                    data: None,
                }
            }
        }
        DaemonCommand::StopVoice => {
            if let Some(active) = session.as_mut() {
                match active.stop_voice() {
                    Ok(info) => DaemonResponse {
                        ok: true,
                        error: None,
                        data: Some(serde_json::to_value(info).unwrap_or(serde_json::Value::Null)),
                    },
                    Err(err) => DaemonResponse {
                        ok: false,
                        error: Some(err.to_string()),
                        data: None,
                    },
                }
            } else {
                DaemonResponse {
                    ok: false,
                    error: Some("no active session".to_string()),
                    data: None,
                }
            }
        }
        DaemonCommand::Status => {
            if let Some(active) = session.as_ref() {
                DaemonResponse {
                    ok: true,
                    error: None,
                    data: Some(serde_json::to_value(StatusInfo {
                        is_session_active: true,
                        is_voice_active: active.is_voice_active,
                        session_root: Some(active.session_root.to_string_lossy().to_string()),
                        trace_root: Some(active.trace_root.to_string_lossy().to_string()),
                        trace_session: active
                            .trace_session
                            .as_ref()
                            .map(|path| path.to_string_lossy().to_string()),
                    })
                    .unwrap_or(serde_json::Value::Null)),
                }
            } else {
                DaemonResponse {
                    ok: true,
                    error: None,
                    data: Some(serde_json::to_value(StatusInfo {
                        is_session_active: false,
                        is_voice_active: false,
                        session_root: None,
                        trace_root: None,
                        trace_session: None,
                    })
                    .unwrap_or(serde_json::Value::Null)),
                }
            }
        }
    }
}

fn start_screen_recording(segment_dir: &Path) -> anyhow::Result<RecorderChild> {
    let output = segment_dir.join("screen.mp4");
    let log_path = segment_dir.join("screen_ffmpeg.log");

    let mut cmd = Command::new("screencapture");
    cmd.arg("-v").arg("-D").arg("1");
    cmd.arg(&output);

    let child = cmd
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(open_log_file(&log_path)?)
        .spawn()
        .context("Failed to start screencapture")?;

    Ok(RecorderChild {
        child,
    })
}

fn stop_recorder(recorder: &mut RecorderChild) -> anyhow::Result<()> {
    if recorder.child.try_wait()?.is_some() {
        return Ok(());
    }

    send_signal(recorder.child.id(), libc::SIGINT)?;

    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
    while std::time::Instant::now() < deadline {
        if recorder.child.try_wait()?.is_some() {
            return Ok(());
        }
        std::thread::sleep(std::time::Duration::from_millis(100));
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

fn encode_voice_to_aac(segment_dir: &Path) -> anyhow::Result<PathBuf> {
    let ffmpeg = which::which("ffmpeg").context("ffmpeg not found in PATH")?;
    let input = segment_dir.join("voice.wav");
    let output = segment_dir.join("voice.m4a");

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

fn move_if_exists(src: &Path, bundle_dir: &Path) -> anyhow::Result<Option<PathBuf>> {
    if !src.exists() {
        return Ok(None);
    }

    let dest = bundle_dir.join(
        src.file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("recording.bin"),
    );

    if let Err(err) = fs::rename(src, &dest) {
        fs::copy(src, &dest).with_context(|| {
            format!("Failed to copy recording from {}: {err}", src.display())
        })?;
    }

    Ok(Some(dest))
}

fn copy_dir_recursive(src: &Path, dest: &Path) -> anyhow::Result<()> {
    if dest.exists() {
        fs::remove_dir_all(dest).with_context(|| {
            format!("Failed to remove existing trace directory {}", dest.display())
        })?;
    }

    fs::create_dir_all(dest).with_context(|| {
        format!("Failed to create trace directory {}", dest.display())
    })?;

    for entry in fs::read_dir(src)
        .with_context(|| format!("Failed to read trace dir {}", src.display()))?
    {
        let entry = entry?;
        let path = entry.path();
        let target = dest.join(entry.file_name());
        let metadata = entry.metadata()?;

        if metadata.is_dir() {
            copy_dir_recursive(&path, &target)?;
        } else if metadata.is_file() {
            fs::copy(&path, &target).with_context(|| {
                format!("Failed to copy {} to {}", path.display(), target.display())
            })?;
        }
    }

    Ok(())
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
    let profile = std::env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
    candidates.push(target_dir.join(&profile).join("tracer_backend/lib"));

    let mut search_paths = Vec::new();
    for candidate in candidates {
        let lib_path = candidate.join("libfrida_agent.dylib");
        if lib_path.exists() {
            search_paths.push(candidate);
        }
    }

    if search_paths.is_empty() {
        anyhow::bail!("libfrida_agent.dylib not found; set ADA_AGENT_RPATH_SEARCH_PATHS");
    }

    let joined = search_paths
        .iter()
        .map(|path| path.to_string_lossy())
        .collect::<Vec<_>>()
        .join(":");
    std::env::set_var("ADA_AGENT_RPATH_SEARCH_PATHS", joined);
    Ok(())
}

fn resolve_output_base(output_base: Option<&str>) -> anyhow::Result<PathBuf> {
    let base = output_base.unwrap_or("ADA-Captures");
    let path = PathBuf::from(base);
    if path.is_absolute() {
        return Ok(path);
    }

    let home = std::env::var("HOME")
        .map(PathBuf::from)
        .context("HOME not set; please supply an absolute output path")?;

    Ok(home.join(base))
}
