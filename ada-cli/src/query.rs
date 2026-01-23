//! Query module for ada CLI
//!
//! Provides functionality to query captured trace sessions from the command line.

mod events;
mod output;
mod parser;
mod session;

use std::path::Path;

use anyhow::Result;

pub use output::OutputFormat;
pub use parser::Query;

/// Run a query against a session
// LCOV_EXCL_START - Integration function requires real session files
pub fn run(session_path: &Path, query_str: &str, format: OutputFormat) -> Result<()> {
    // Open the session
    let session = session::Session::open(session_path)?;

    // Parse the query
    let query = Query::parse(query_str)?;

    // Execute the query and format output
    match query {
        Query::Summary => {
            let summary = session.summary()?;
            println!("{}", output::format_summary(&summary, format));
        }
        Query::ListFunctions => {
            let symbols = session.list_symbols();
            println!("{}", output::format_functions(&symbols, format));
        }
        Query::ListThreads => {
            let threads = session.list_threads();
            println!("{}", output::format_threads(&threads, format));
        }
        Query::Events {
            thread,
            function,
            limit,
            offset,
        } => {
            let events = session.query_events(thread, function.as_deref(), limit, offset)?;
            println!("{}", output::format_events(&events, &session, format));
        }
        Query::Calls { function } => {
            let events =
                session.query_events(None, Some(&function), Some(1000), Some(0))?;
            println!("{}", output::format_events(&events, &session, format));
        }
    }

    Ok(())
}
// LCOV_EXCL_STOP
