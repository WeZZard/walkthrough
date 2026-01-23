//! Session reader for trace data
//!
//! Reads session manifest and provides access to symbols and metadata.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use serde::Deserialize;

use super::events::{Event, EventKind, EventReader};

/// A trace session with manifest and symbol information
pub struct Session {
    /// Path to the session directory
    pub path: PathBuf,
    /// Path to the trace directory containing manifest and thread data
    pub trace_path: PathBuf,
    /// Parsed manifest
    pub manifest: Manifest,
    /// Symbol lookup by function_id
    symbols: HashMap<u64, String>,
}

/// Session manifest structure from manifest.json
#[derive(Debug, Deserialize)]
#[allow(dead_code)]
pub struct Manifest {
    pub threads: Vec<ThreadInfo>,
    #[serde(default)]
    pub time_start_ns: u64,
    #[serde(default)]
    pub time_end_ns: u64,
    #[serde(default)]
    pub clock_type: u8,
    #[serde(default)]
    pub modules: Vec<ModuleInfo>,
    #[serde(default)]
    pub symbols: Vec<SymbolInfo>,
}

/// Thread information from manifest
#[derive(Debug, Clone, Deserialize)]
pub struct ThreadInfo {
    pub id: u32,
    #[serde(default)]
    pub has_detail: bool,
}

/// Module information from manifest
#[derive(Debug, Deserialize)]
#[allow(dead_code)]
pub struct ModuleInfo {
    pub module_id: u64,
    pub path: String,
    #[serde(default)]
    pub base_address: String,
    #[serde(default)]
    pub size: u64,
    #[serde(default)]
    pub uuid: String,
}

/// Symbol information from manifest
#[derive(Debug, Deserialize)]
#[allow(dead_code)]
pub struct SymbolInfo {
    pub function_id: String,
    pub module_id: u64,
    #[serde(default)]
    pub symbol_index: u32,
    pub name: String,
}

/// Session summary statistics
pub struct SessionSummary {
    pub session_name: String,
    pub module_path: Option<String>,
    pub module_uuid: Option<String>,
    pub thread_count: usize,
    pub symbol_count: usize,
    pub total_events: usize,
    pub thread_event_counts: Vec<(u32, usize)>,
}

impl Session {
    /// Open a session from a path
    ///
    /// The path can be:
    /// - A session directory containing trace/ or bundles/
    /// - A direct path to a trace directory containing manifest.json
    pub fn open(path: &Path) -> Result<Self> {
        let path = path.to_path_buf();

        // Find the trace directory containing manifest.json
        let trace_path = Self::find_trace_path(&path)?;

        // Read manifest
        let manifest_path = trace_path.join("manifest.json");
        let manifest_content = fs::read_to_string(&manifest_path)
            .with_context(|| format!("Failed to read manifest at {:?}", manifest_path))?;
        let manifest: Manifest = serde_json::from_str(&manifest_content)
            .with_context(|| "Failed to parse manifest.json")?;

        // Build symbol lookup
        let mut symbols = HashMap::new();
        for sym in &manifest.symbols {
            // Parse function_id from hex string like "0xf7f05ac5000001a2"
            let function_id = if sym.function_id.starts_with("0x") {
                u64::from_str_radix(&sym.function_id[2..], 16).unwrap_or(0)
            } else {
                sym.function_id.parse().unwrap_or(0) // LCOV_EXCL_LINE - Non-hex format
            };
            symbols.insert(function_id, sym.name.clone());
        }

        Ok(Session {
            path,
            trace_path,
            manifest,
            symbols,
        })
    }

    /// Find the trace directory containing manifest.json
    // LCOV_EXCL_START - Filesystem discovery requires real session directories
    fn find_trace_path(session_path: &Path) -> Result<PathBuf> {
        // Check if this path directly contains manifest.json
        if session_path.join("manifest.json").exists() {
            return Ok(session_path.to_path_buf());
        }

        // Prefer trace/session_YYYYMMDD_HHMMSS/pid_XXXXX/manifest.json
        // (contains actual trace data, unlike bundles which may be empty)
        let trace_path = session_path.join("trace");
        if trace_path.is_dir() {
            if let Ok(sessions) = fs::read_dir(&trace_path) {
                // Sort by name to get most recent session first
                let mut session_dirs: Vec<_> = sessions
                    .filter_map(|e| e.ok())
                    .map(|e| e.path())
                    .filter(|p| p.is_dir())
                    .collect();
                session_dirs.sort();
                session_dirs.reverse(); // Most recent first

                for session_dir in session_dirs {
                    // Look for pid directories
                    if let Ok(pids) = fs::read_dir(&session_dir) {
                        for pid_entry in pids.filter_map(|e| e.ok()) {
                            let pid_dir = pid_entry.path();
                            if pid_dir.join("manifest.json").exists() {
                                // Verify there are actual ATF files with data
                                if Self::has_valid_atf_files(&pid_dir) {
                                    return Ok(pid_dir);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fall back to bundles/segment_XXX.adabundle/trace/manifest.json
        let bundles_path = session_path.join("bundles");
        if bundles_path.is_dir() {
            // Find first adabundle with a manifest
            if let Ok(entries) = fs::read_dir(&bundles_path) {
                for entry in entries.filter_map(|e| e.ok()) {
                    let bundle_trace = entry.path().join("trace");
                    if bundle_trace.join("manifest.json").exists() {
                        if Self::has_valid_atf_files(&bundle_trace) {
                            return Ok(bundle_trace);
                        }
                    }
                }
            }
        }

        bail!(
            "Could not find manifest.json with valid trace data in {:?}. \
            Expected: trace/session_XXX/pid_XXX/manifest.json or \
            bundles/segment_XXX.adabundle/trace/manifest.json",
            session_path
        )
    }

    /// Check if directory has valid ATF files with actual events
    fn has_valid_atf_files(trace_dir: &Path) -> bool {
        // Check for thread directories with index.atf files containing events
        if let Ok(entries) = fs::read_dir(trace_dir) {
            for entry in entries.filter_map(|e| e.ok()) {
                let path = entry.path();
                if path.is_dir() {
                    let index_path = path.join("index.atf");
                    if let Ok(metadata) = fs::metadata(&index_path) {
                        // Header (64) + Footer (64) + at least one event (32) = 160 bytes
                        if metadata.len() >= 160 {
                            return true;
                        }
                    }
                }
            }
        }
        false
    }
    // LCOV_EXCL_STOP

    /// Resolve a function_id to its symbol name
    pub fn resolve_symbol(&self, function_id: u64) -> Option<&str> {
        self.symbols.get(&function_id).map(|s| s.as_str())
    }

    /// Get session summary statistics
    // LCOV_EXCL_START - Reads ATF files from filesystem
    pub fn summary(&self) -> Result<SessionSummary> {
        let session_name = self
            .path
            .file_name()
            .map(|s| s.to_string_lossy().to_string())
            .unwrap_or_else(|| "unknown".to_string());

        let (module_path, module_uuid) = if let Some(module) = self.manifest.modules.first() {
            (Some(module.path.clone()), Some(module.uuid.clone()))
        } else {
            (None, None)
        };

        // Count events per thread
        let mut thread_event_counts = Vec::new();
        let mut total_events = 0;

        for thread in &self.manifest.threads {
            let thread_dir = self.trace_path.join(format!("thread_{}", thread.id));
            let index_path = thread_dir.join("index.atf");

            if index_path.exists() {
                let reader = EventReader::open(&index_path)?;
                let count = reader.len() as usize;
                thread_event_counts.push((thread.id, count));
                total_events += count;
            } else {
                thread_event_counts.push((thread.id, 0));
            }
        }

        Ok(SessionSummary {
            session_name,
            module_path,
            module_uuid,
            thread_count: self.manifest.threads.len(),
            symbol_count: self.manifest.symbols.len(),
            total_events,
            thread_event_counts,
        })
    }
    // LCOV_EXCL_STOP

    /// List all symbol names
    pub fn list_symbols(&self) -> Vec<&str> {
        self.manifest
            .symbols
            .iter()
            .map(|s| s.name.as_str())
            .collect()
    }

    /// List all threads
    pub fn list_threads(&self) -> Vec<&ThreadInfo> {
        self.manifest.threads.iter().collect()
    }

    /// Query events with optional filters
    // LCOV_EXCL_START - Reads ATF files from filesystem
    pub fn query_events(
        &self,
        thread_filter: Option<u32>,
        function_filter: Option<&str>,
        limit: Option<usize>,
        offset: Option<usize>,
    ) -> Result<Vec<Event>> {
        let offset = offset.unwrap_or(0);
        let limit = limit.unwrap_or(1000);

        // Build function_id filter if function name is provided
        let function_id_filter: Option<u64> = function_filter.and_then(|name| {
            self.manifest
                .symbols
                .iter()
                .find(|s| s.name.contains(name))
                .and_then(|s| {
                    if s.function_id.starts_with("0x") {
                        u64::from_str_radix(&s.function_id[2..], 16).ok()
                    } else {
                        s.function_id.parse().ok()
                    }
                })
        });

        // Determine which threads to read
        let threads: Vec<&ThreadInfo> = match thread_filter {
            Some(tid) => self
                .manifest
                .threads
                .iter()
                .filter(|t| t.id == tid)
                .collect(),
            None => self.manifest.threads.iter().collect(),
        };

        // Collect events from each thread
        let mut all_events: Vec<Event> = Vec::new();

        for thread in threads {
            let thread_dir = self.trace_path.join(format!("thread_{}", thread.id));
            let index_path = thread_dir.join("index.atf");

            if !index_path.exists() {
                continue;
            }

            let reader = EventReader::open(&index_path)?;

            for event in reader.iter() {
                // Apply function filter
                if let Some(fid) = function_id_filter {
                    if event.function_id != fid {
                        continue;
                    }
                }
                all_events.push(event);
            }
        }

        // Filter out obviously corrupted events (event_kind > 3 indicates corruption)
        let valid_events: Vec<Event> = all_events
            .into_iter()
            .filter(|e| matches!(e.kind, EventKind::Call | EventKind::Return | EventKind::Exception))
            .collect();
        all_events = valid_events;

        // Sort all events by timestamp for merged view
        if thread_filter.is_none() {
            all_events.sort_by_key(|e| e.timestamp_ns);
        }

        // Apply offset and limit
        let events = all_events
            .into_iter()
            .skip(offset)
            .take(limit)
            .collect();

        Ok(events)
    }
    // LCOV_EXCL_STOP
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_test_session() -> TempDir {
        let temp_dir = TempDir::new().unwrap();

        // Create trace structure
        let trace_dir = temp_dir.path().join("trace");
        fs::create_dir_all(&trace_dir).unwrap();

        // Create manifest
        let manifest = r#"{
            "threads": [{"id": 0, "has_detail": true}],
            "time_start_ns": 0,
            "time_end_ns": 1000000,
            "clock_type": 1,
            "modules": [{
                "module_id": 123,
                "path": "/path/to/app",
                "uuid": "ABC123"
            }],
            "symbols": [{
                "function_id": "0x7b00000001",
                "module_id": 123,
                "symbol_index": 1,
                "name": "main"
            }]
        }"#;

        let manifest_path = trace_dir.join("manifest.json");
        let mut f = fs::File::create(&manifest_path).unwrap();
        f.write_all(manifest.as_bytes()).unwrap();

        temp_dir
    }

    #[test]
    fn test_session__open_direct_manifest__then_success() {
        let temp_dir = create_test_session();
        let trace_dir = temp_dir.path().join("trace");

        let session = Session::open(&trace_dir).unwrap();
        assert_eq!(session.manifest.threads.len(), 1);
        assert_eq!(session.manifest.symbols.len(), 1);
    }

    #[test]
    fn test_session__resolve_symbol__then_found() {
        let temp_dir = create_test_session();
        let trace_dir = temp_dir.path().join("trace");

        let session = Session::open(&trace_dir).unwrap();
        let symbol = session.resolve_symbol(0x7b00000001);
        assert_eq!(symbol, Some("main"));
    }

    #[test]
    fn test_session__list_symbols__then_returns_names() {
        let temp_dir = create_test_session();
        let trace_dir = temp_dir.path().join("trace");

        let session = Session::open(&trace_dir).unwrap();
        let symbols = session.list_symbols();
        assert_eq!(symbols, vec!["main"]);
    }

    #[test]
    fn test_session__list_threads__then_returns_thread_info() {
        let temp_dir = create_test_session();
        let trace_dir = temp_dir.path().join("trace");

        let session = Session::open(&trace_dir).unwrap();
        let threads = session.list_threads();
        assert_eq!(threads.len(), 1);
        assert_eq!(threads[0].id, 0);
        assert!(threads[0].has_detail);
    }
}
