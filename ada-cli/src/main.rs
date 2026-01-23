//! ADA Command Line Interface
//!
//! Provides commands for tracing, symbol resolution, and trace analysis.
//!
//! # Commands
//!
//! - `ada trace` - Manage tracing sessions
//! - `ada symbols` - Symbol resolution and dSYM management
//! - `ada query` - Query trace data

mod capture;
mod ffi;
mod query;
mod symbols;
mod trace;

use clap::{Parser, Subcommand};
use tracing_subscriber::{fmt, EnvFilter};

/// ADA - Application Dynamic Analysis
///
/// A performance tracing and analysis toolkit for macOS applications.
#[derive(Parser)]
#[command(name = "ada")]
#[command(author, version, about, long_about = None)]
#[command(propagate_version = true)]
struct Cli {
    /// Enable verbose output
    #[arg(short, long, global = true)]
    verbose: bool,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Manage tracing sessions
    #[command(subcommand)]
    Trace(trace::TraceCommands),

    /// Symbol resolution and dSYM management
    #[command(subcommand)]
    Symbols(symbols::SymbolsCommands),

    /// Capture multimodal debugging sessions
    #[command(subcommand)]
    Capture(capture::CaptureCommands),

    // LCOV_EXCL_START - Struct field definitions
    /// Query trace data
    ///
    /// Examples:
    ///   ada query /path/to/session summary
    ///   ada query /path/to/session list functions
    ///   ada query /path/to/session events limit:100
    ///   ada query /path/to/session events thread:0 limit:50
    ///   ada query /path/to/session calls to main
    ///   ada query /path/to/session --format json summary
    Query {
        /// Path to session directory
        session: String,

        /// Query (summary, list functions, list threads, events, calls to <name>)
        #[arg(trailing_var_arg = true, required = true)]
        query: Vec<String>,

        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },
    // LCOV_EXCL_STOP
}

fn main() -> anyhow::Result<()> {
    // Initialize logging
    let filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));
    fmt::Subscriber::builder()
        .with_env_filter(filter)
        .with_target(false)
        .init();

    let cli = Cli::parse();

    if cli.verbose {
        tracing::info!("Verbose mode enabled");
    }

    match cli.command {
        Commands::Trace(cmd) => trace::run(cmd),
        Commands::Symbols(cmd) => symbols::run(cmd),
        Commands::Capture(cmd) => capture::run(cmd),
        // LCOV_EXCL_START - CLI entry point requires real session files
        Commands::Query {
            session,
            query: query_words,
            format,
        } => {
            use std::path::PathBuf;

            let query_str = query_words.join(" ");
            let output_format: query::OutputFormat = format
                .parse()
                .map_err(|e: String| anyhow::anyhow!("{}", e))?;

            query::run(&PathBuf::from(session), &query_str, output_format)
        }
        // LCOV_EXCL_STOP
    }
}
