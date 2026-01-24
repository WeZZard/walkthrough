//! Query module for ada CLI
//!
//! Provides functionality to query captured trace sessions from the command line.
//! Uses bundle-first architecture: parse bundle manifest, then route to data.

mod bundle;
mod events;
mod output;
mod session;

use std::path::Path;

use anyhow::Result;

use crate::QueryCommands;
use bundle::Bundle;
use output::OutputFormat;

/// Run a query against a bundle
///
/// Layer 1: Open and validate the bundle manifest
/// Layer 2: Dispatch to appropriate data source based on query type
// LCOV_EXCL_START - Integration function requires real session files
pub fn run(bundle_path: &Path, cmd: QueryCommands) -> Result<()> {
    // Layer 1: Open and validate bundle
    let bundle = Bundle::open(bundle_path)?;

    // Layer 2: Dispatch based on query type
    // All current queries are trace queries - need ATF data
    let session = session::Session::open(&bundle.trace_path())?;

    execute_trace_query(&session, cmd)
}

/// Execute a trace query against an opened session
fn execute_trace_query(session: &session::Session, cmd: QueryCommands) -> Result<()> {
    match cmd {
        QueryCommands::Summary { format } => {
            let fmt = parse_format(&format)?;
            let summary = session.summary()?;
            println!("{}", output::format_summary(&summary, fmt));
        }
        QueryCommands::Events {
            thread,
            function,
            limit,
            offset,
            format,
        } => {
            let fmt = parse_format(&format)?;
            let events =
                session.query_events(thread, function.as_deref(), Some(limit), Some(offset))?;
            println!("{}", output::format_events(&events, session, fmt));
        }
        QueryCommands::Functions { format } => {
            let fmt = parse_format(&format)?;
            let symbols = session.list_symbols();
            println!("{}", output::format_functions(&symbols, fmt));
        }
        QueryCommands::Threads { format } => {
            let fmt = parse_format(&format)?;
            let threads = session.list_threads();
            println!("{}", output::format_threads(&threads, fmt));
        }
        QueryCommands::Calls {
            function,
            limit,
            format,
        } => {
            let fmt = parse_format(&format)?;
            let events =
                session.query_events(None, Some(&function), Some(limit), Some(0))?;
            println!("{}", output::format_events(&events, session, fmt));
        }
    }

    Ok(())
}

/// Parse format string to OutputFormat
fn parse_format(format: &str) -> Result<OutputFormat> {
    format
        .parse()
        .map_err(|e: String| anyhow::anyhow!("{}", e))
}
// LCOV_EXCL_STOP
