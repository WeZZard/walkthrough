//! Output formatters for query results
//!
//! Supports text, JSON, and line output formats.

use std::collections::HashMap;

use serde::Serialize;

use super::events::{Event, EventKind};
use super::session::{Session, SessionSummary, ThreadInfo};

/// Output format
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum OutputFormat {
    #[default]
    Text,
    Json,
    /// Line format with computed path indices, human-readable timestamps, and raw nanoseconds
    Line,
}

/// Event enriched with computed path index
#[derive(Debug, Clone)]
pub struct EnrichedEvent {
    pub event: Event,
    /// Hierarchical path in call tree (e.g., "0.0.1")
    pub path_index: String,
    /// Seconds from first event (human-readable)
    pub relative_time_secs: f64,
}

/// Per-thread call stack tracker for computing path indices
struct PathTracker {
    /// thread_id -> stack of sibling indices at each depth
    stacks: HashMap<u32, Vec<usize>>,
    /// thread_id -> (depth -> next sibling index)
    sibling_counters: HashMap<u32, HashMap<u32, usize>>,
}

impl PathTracker {
    fn new() -> Self {
        PathTracker {
            stacks: HashMap::new(),
            sibling_counters: HashMap::new(),
        }
    }

    /// Process an event and return its path index
    fn process_event(&mut self, event: &Event) -> String {
        let thread_id = event.thread_id;
        let depth = event.depth;

        match event.kind {
            EventKind::Call => {
                // Get or create structures for this thread
                let stack = self.stacks.entry(thread_id).or_insert_with(Vec::new);
                let counters = self.sibling_counters.entry(thread_id).or_default();

                // Get sibling index at current depth, increment counter
                let sibling_idx = *counters.get(&depth).unwrap_or(&0);
                counters.insert(depth, sibling_idx + 1);

                // Push sibling index onto stack
                stack.push(sibling_idx);

                // Reset child sibling counter (depth + 1)
                counters.insert(depth + 1, 0);

                // Build path: thread.parent_siblings...sibling
                let mut path_parts: Vec<String> = vec![thread_id.to_string()];
                for &idx in stack.iter() {
                    path_parts.push(idx.to_string());
                }
                path_parts.join(".")
            }
            EventKind::Return | EventKind::Exception => {
                // Get stack for this thread
                let stack = self.stacks.entry(thread_id).or_insert_with(Vec::new);

                if stack.is_empty() {
                    // Orphan return - just use thread_id
                    return thread_id.to_string();
                }

                // Build path from current stack
                let mut path_parts: Vec<String> = vec![thread_id.to_string()];
                for &idx in stack.iter() {
                    path_parts.push(idx.to_string());
                }
                let path = path_parts.join(".");

                // Pop from stack
                stack.pop();

                path
            }
            EventKind::Unknown(_) => {
                // Unknown event kind - just use thread_id
                thread_id.to_string()
            }
        }
    }
}

/// Compute enriched events with path indices and relative timestamps
pub fn compute_enriched_events(events: &[Event], start_time_ns: u64) -> Vec<EnrichedEvent> {
    let mut tracker = PathTracker::new();
    events
        .iter()
        .map(|event| EnrichedEvent {
            event: event.clone(),
            path_index: tracker.process_event(event),
            relative_time_secs: (event.timestamp_ns.saturating_sub(start_time_ns)) as f64 / 1e9,
        })
        .collect()
}

impl std::str::FromStr for OutputFormat {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "text" | "txt" => Ok(OutputFormat::Text),
            "json" => Ok(OutputFormat::Json),
            "line" => Ok(OutputFormat::Line),
            _ => Err(format!(
                "Unknown format '{}'. Use 'text', 'json', or 'line'",
                s
            )),
        }
    }
}

/// Format session summary
pub fn format_summary(summary: &SessionSummary, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_summary_text(summary),
        OutputFormat::Json => format_summary_json(summary),
    }
}

fn format_summary_text(summary: &SessionSummary) -> String {
    let mut output = String::new();

    output.push_str(&format!("Session: {}\n", summary.session_name));

    if let Some(ref path) = summary.module_path {
        // Extract just the app name from the full path
        let app_name = std::path::Path::new(path)
            .file_name()
            .map(|s| s.to_string_lossy().to_string())
            .unwrap_or_else(|| path.clone());
        output.push_str(&format!("Module:  {}", app_name));
        if let Some(ref uuid) = summary.module_uuid {
            output.push_str(&format!(" ({})", uuid));
        }
        output.push('\n');
    } // LCOV_EXCL_LINE - None case tested via integration

    output.push_str(&format!("Threads: {}\n", summary.thread_count));
    output.push_str(&format!("Symbols: {}\n", summary.symbol_count));
    output.push_str(&format!("Events:  {:>6}\n", format_number(summary.total_events)));

    if !summary.thread_event_counts.is_empty() {
        output.push('\n');
        for (thread_id, count) in &summary.thread_event_counts {
            if *count > 0 {
                output.push_str(&format!(
                    "Thread {}: {:>6} events\n",
                    thread_id,
                    format_number(*count)
                ));
            }
        }
    } // LCOV_EXCL_LINE - Empty case tested via integration

    output
}

fn format_summary_json(summary: &SessionSummary) -> String {
    #[derive(Serialize)]
    struct JsonSummary {
        session_name: String,
        module_path: Option<String>,
        module_uuid: Option<String>,
        thread_count: usize,
        symbol_count: usize,
        total_events: usize,
        threads: Vec<JsonThreadCount>,
    }

    #[derive(Serialize)]
    struct JsonThreadCount {
        id: u32,
        event_count: usize,
    }

    let json_summary = JsonSummary {
        session_name: summary.session_name.clone(),
        module_path: summary.module_path.clone(),
        module_uuid: summary.module_uuid.clone(),
        thread_count: summary.thread_count,
        symbol_count: summary.symbol_count,
        total_events: summary.total_events,
        threads: summary
            .thread_event_counts
            .iter()
            .map(|(id, count)| JsonThreadCount {
                id: *id,
                event_count: *count,
            })
            .collect(),
    };

    serde_json::to_string_pretty(&json_summary).unwrap_or_else(|_| "{}".to_string())
}

/// Format function list
// LCOV_EXCL_START - Integration tested via CLI
pub fn format_functions(symbols: &[&str], format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_functions_text(symbols),
        OutputFormat::Json => format_functions_json(symbols),
    }
}

fn format_functions_text(symbols: &[&str]) -> String {
    let mut output = String::new();
    output.push_str(&format!("Functions ({}):\n\n", symbols.len()));
    for symbol in symbols {
        output.push_str(symbol);
        output.push('\n');
    }
    output
}

fn format_functions_json(symbols: &[&str]) -> String {
    #[derive(Serialize)]
    struct JsonFunctions<'a> {
        count: usize,
        functions: Vec<&'a str>,
    }

    let json_funcs = JsonFunctions {
        count: symbols.len(),
        functions: symbols.to_vec(),
    };

    serde_json::to_string_pretty(&json_funcs).unwrap_or_else(|_| "{}".to_string())
}

/// Format thread list
pub fn format_threads(threads: &[&ThreadInfo], format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_threads_text(threads),
        OutputFormat::Json => format_threads_json(threads),
    }
}

fn format_threads_text(threads: &[&ThreadInfo]) -> String {
    let mut output = String::new();
    output.push_str(&format!("Threads ({}):\n\n", threads.len()));
    for thread in threads {
        output.push_str(&format!(
            "Thread {}: has_detail={}\n",
            thread.id, thread.has_detail
        ));
    }
    output
}

fn format_threads_json(threads: &[&ThreadInfo]) -> String {
    #[derive(Serialize)]
    struct JsonThreads {
        count: usize,
        threads: Vec<JsonThread>,
    }

    #[derive(Serialize)]
    struct JsonThread {
        id: u32,
        has_detail: bool,
    }

    let json_threads = JsonThreads {
        count: threads.len(),
        threads: threads
            .iter()
            .map(|t| JsonThread {
                id: t.id,
                has_detail: t.has_detail,
            })
            .collect(),
    };

    serde_json::to_string_pretty(&json_threads).unwrap_or_else(|_| "{}".to_string())
}

/// Format events list
pub fn format_events(events: &[Event], session: &Session, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text => format_events_text(events, session),
        OutputFormat::Json => format_events_json(events, session),
        OutputFormat::Line => format_events_line(events, session),
    }
}

/// Format events in line format with path indices and timestamps
///
/// Output format:
/// ```text
/// ns=949066051830500 | T=0.000000s | thread:0 | path:0.0 | depth:1 | CALL main()
/// ```
fn format_events_line(events: &[Event], session: &Session) -> String {
    if events.is_empty() {
        return "(no events)\n".to_string();
    }

    let start = events.first().map(|e| e.timestamp_ns).unwrap_or(0);
    let enriched = compute_enriched_events(events, start);

    let mut output = String::new();
    for e in &enriched {
        let function_name = session
            .resolve_symbol(e.event.function_id)
            .unwrap_or("<unknown>");

        output.push_str(&format!(
            "ns={} | T={:.6}s | thread:{} | path:{} | depth:{} | {} {}()\n",
            e.event.timestamp_ns,
            e.relative_time_secs,
            e.event.thread_id,
            e.path_index,
            e.event.depth,
            e.event.kind,
            function_name
        ));
    }

    output
}

fn format_events_text(events: &[Event], session: &Session) -> String {
    let mut output = String::new();

    // Header
    output.push_str(&format!(
        "{:<16} {:>6} {:>5} {:>7} {}\n",
        "TIME(ns)", "THREAD", "DEPTH", "TYPE", "FUNCTION"
    ));
    output.push_str(&format!("{}\n", "-".repeat(80)));

    for event in events {
        let function_name = session
            .resolve_symbol(event.function_id)
            .unwrap_or("<unknown>");

        // Truncate long function names
        let display_name = if function_name.len() > 50 {
            format!("{}...", &function_name[..47])
        } else {
            function_name.to_string()
        };

        output.push_str(&format!(
            "{:<16} {:>6} {:>5} {:>7} {}\n",
            event.timestamp_ns, event.thread_id, event.depth, event.kind, display_name
        ));
    }

    if events.is_empty() {
        output.push_str("(no events)\n");
    } else {
        output.push_str(&format!("\n{} events\n", events.len()));
    }

    output
}

fn format_events_json(events: &[Event], session: &Session) -> String {
    #[derive(Serialize)]
    struct JsonEvents {
        count: usize,
        events: Vec<JsonEvent>,
    }

    #[derive(Serialize)]
    struct JsonEvent {
        timestamp_ns: u64,
        thread_id: u32,
        depth: u32,
        kind: String,
        function_id: String,
        function_name: Option<String>,
    }

    let json_events = JsonEvents {
        count: events.len(),
        events: events
            .iter()
            .map(|e| JsonEvent {
                timestamp_ns: e.timestamp_ns,
                thread_id: e.thread_id,
                depth: e.depth,
                kind: e.kind.to_string(),
                function_id: format!("0x{:x}", e.function_id),
                function_name: session.resolve_symbol(e.function_id).map(String::from),
            })
            .collect(),
    };

    serde_json::to_string_pretty(&json_events).unwrap_or_else(|_| "{}".to_string())
}
// LCOV_EXCL_STOP

/// Format number with thousands separators
fn format_number(n: usize) -> String {
    let s = n.to_string();
    let mut result = String::new();
    let chars: Vec<_> = s.chars().collect();

    for (i, c) in chars.iter().enumerate() {
        if i > 0 && (chars.len() - i) % 3 == 0 {
            result.push(',');
        }
        result.push(*c);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_number__small__then_no_comma() {
        assert_eq!(format_number(123), "123");
    }

    #[test]
    fn test_format_number__thousands__then_comma() {
        assert_eq!(format_number(1234), "1,234");
    }

    #[test]
    fn test_format_number__millions__then_two_commas() {
        assert_eq!(format_number(1234567), "1,234,567");
    }

    #[test]
    fn test_output_format__parse_text__then_text() {
        let format: OutputFormat = "text".parse().unwrap();
        assert_eq!(format, OutputFormat::Text);
    }

    #[test]
    fn test_output_format__parse_json__then_json() {
        let format: OutputFormat = "json".parse().unwrap();
        assert_eq!(format, OutputFormat::Json);
    }

    #[test]
    fn test_output_format__parse_unknown__then_error() {
        let result: Result<OutputFormat, _> = "xml".parse();
        assert!(result.is_err());
    }

    #[test]
    fn test_format_summary_text__basic__then_formatted() {
        let summary = SessionSummary {
            session_name: "test_session".to_string(),
            module_path: Some("/path/to/app".to_string()),
            module_uuid: Some("ABC123".to_string()),
            thread_count: 2,
            symbol_count: 10,
            total_events: 100,
            thread_event_counts: vec![(0, 60), (1, 40)],
        };

        let output = format_summary(&summary, OutputFormat::Text);
        assert!(output.contains("Session: test_session"));
        assert!(output.contains("Threads: 2"));
        assert!(output.contains("Thread 0:") && output.contains("60 events"));
    }

    #[test]
    fn test_format_summary_json__basic__then_valid_json() {
        let summary = SessionSummary {
            session_name: "test_session".to_string(),
            module_path: None,
            module_uuid: None,
            thread_count: 1,
            symbol_count: 5,
            total_events: 50,
            thread_event_counts: vec![(0, 50)],
        };

        let output = format_summary(&summary, OutputFormat::Json);
        // Verify it's valid JSON
        let parsed: serde_json::Value = serde_json::from_str(&output).unwrap();
        assert_eq!(parsed["session_name"], "test_session");
        assert_eq!(parsed["total_events"], 50);
    }

    #[test]
    fn test_output_format__parse_line__then_line() {
        let format: OutputFormat = "line".parse().unwrap();
        assert_eq!(format, OutputFormat::Line);
    }

    #[test]
    fn test_path_tracker__single_call__then_thread_sibling_path() {
        let mut tracker = PathTracker::new();
        let event = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 1,
        };

        let path = tracker.process_event(&event);
        assert_eq!(path, "0.0"); // thread 0, first call at depth 1
    }

    #[test]
    fn test_path_tracker__nested_calls__then_hierarchical_path() {
        let mut tracker = PathTracker::new();

        // First call: main()
        let call1 = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&call1), "0.0");

        // Nested call: login()
        let call2 = Event {
            timestamp_ns: 2000,
            function_id: 0x200,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&call2), "0.0.0");

        // Return from login()
        let ret = Event {
            timestamp_ns: 3000,
            function_id: 0x200,
            thread_id: 0,
            kind: EventKind::Return,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&ret), "0.0.0");
    }

    #[test]
    fn test_path_tracker__sibling_calls__then_incremented_index() {
        let mut tracker = PathTracker::new();

        // First call: main()
        let call1 = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&call1), "0.0");

        // First nested call: login()
        let call2 = Event {
            timestamp_ns: 2000,
            function_id: 0x200,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&call2), "0.0.0");

        // Return from login()
        let ret2 = Event {
            timestamp_ns: 3000,
            function_id: 0x200,
            thread_id: 0,
            kind: EventKind::Return,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&ret2), "0.0.0");

        // Second sibling call: logout()
        let call3 = Event {
            timestamp_ns: 4000,
            function_id: 0x300,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&call3), "0.0.1"); // sibling incremented
    }

    #[test]
    fn test_path_tracker__multiple_threads__then_independent_paths() {
        let mut tracker = PathTracker::new();

        // Thread 0: first call
        let call0 = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&call0), "0.0");

        // Thread 1: first call (independent)
        let call1 = Event {
            timestamp_ns: 1500,
            function_id: 0x200,
            thread_id: 1,
            kind: EventKind::Call,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&call1), "1.0");

        // Thread 0: second call
        let call0b = Event {
            timestamp_ns: 2000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 2,
        };
        assert_eq!(tracker.process_event(&call0b), "0.0.0");
    }

    #[test]
    fn test_path_tracker__orphan_return__then_fallback_path() {
        let mut tracker = PathTracker::new();

        // Return without matching call (orphan)
        let orphan_ret = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 5,
            kind: EventKind::Return,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&orphan_ret), "5"); // just thread_id
    }

    #[test]
    fn test_compute_enriched_events__timestamps__then_relative_seconds() {
        let events = vec![
            Event {
                timestamp_ns: 1_000_000_000, // 1 second
                function_id: 0x100,
                thread_id: 0,
                kind: EventKind::Call,
                depth: 1,
            },
            Event {
                timestamp_ns: 1_000_021_000, // 1.000021 seconds
                function_id: 0x200,
                thread_id: 0,
                kind: EventKind::Call,
                depth: 2,
            },
        ];

        let enriched = compute_enriched_events(&events, 1_000_000_000);

        assert_eq!(enriched.len(), 2);
        assert!((enriched[0].relative_time_secs - 0.0).abs() < 1e-9);
        assert!((enriched[1].relative_time_secs - 0.000021).abs() < 1e-9);
        assert_eq!(enriched[0].path_index, "0.0");
        assert_eq!(enriched[1].path_index, "0.0.0");
    }

    #[test]
    fn test_path_tracker__exception__then_same_as_return() {
        let mut tracker = PathTracker::new();

        // Call
        let call = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Call,
            depth: 1,
        };
        tracker.process_event(&call);

        // Exception (should behave like return)
        let exc = Event {
            timestamp_ns: 2000,
            function_id: 0x100,
            thread_id: 0,
            kind: EventKind::Exception,
            depth: 1,
        };
        assert_eq!(tracker.process_event(&exc), "0.0");
    }

    #[test]
    fn test_path_tracker__unknown_event_kind__then_thread_id_only() {
        let mut tracker = PathTracker::new();

        // Unknown event kind (rare edge case)
        let unknown = Event {
            timestamp_ns: 1000,
            function_id: 0x100,
            thread_id: 7,
            kind: EventKind::Unknown(99),
            depth: 1,
        };
        assert_eq!(tracker.process_event(&unknown), "7"); // just thread_id
    }
}
