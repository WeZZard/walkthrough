//! Output formatters for query results
//!
//! Supports text and JSON output formats.

use serde::Serialize;

use super::events::Event;
use super::session::{Session, SessionSummary, ThreadInfo};

/// Output format
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum OutputFormat {
    #[default]
    Text,
    Json,
}

impl std::str::FromStr for OutputFormat {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "text" | "txt" => Ok(OutputFormat::Text),
            "json" => Ok(OutputFormat::Json),
            _ => Err(format!("Unknown format '{}'. Use 'text' or 'json'", s)),
        }
    }
}

/// Format session summary
pub fn format_summary(summary: &SessionSummary, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text => format_summary_text(summary),
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
        OutputFormat::Text => format_functions_text(symbols),
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
        OutputFormat::Text => format_threads_text(threads),
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
    }
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
}
