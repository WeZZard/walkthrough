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

use std::path::PathBuf;

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
    /// Query trace data from a bundle
    ///
    /// Examples:
    ///   ada query /path/to/bundle.adabundle summary
    ///   ada query /path/to/bundle.adabundle events --limit 100
    ///   ada query /path/to/bundle.adabundle events --thread 0 --limit 50
    ///   ada query /path/to/bundle.adabundle functions
    ///   ada query /path/to/bundle.adabundle threads
    ///   ada query /path/to/bundle.adabundle calls main --format json
    Query {
        /// Path to .adabundle directory
        bundle: PathBuf,

        #[command(subcommand)]
        command: QueryCommands,
    },
    // LCOV_EXCL_STOP
} // LCOV_EXCL_LINE - Enum closing brace

// LCOV_EXCL_START - Enum field definitions
/// Query subcommands for trace data
#[derive(Subcommand)]
pub enum QueryCommands {
    /// Show session summary statistics
    Summary {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// List trace events with optional filters
    Events {
        /// Filter by thread ID
        #[arg(short, long)]
        thread: Option<u32>,

        /// Filter by function name (substring match)
        #[arg(long)]
        function: Option<String>,

        /// Maximum number of events to return
        #[arg(short, long, default_value = "1000")]
        limit: usize,

        /// Number of events to skip
        #[arg(short, long, default_value = "0")]
        offset: usize,

        /// Output format (text or json)
        #[arg(short = 'f', long, default_value = "text")]
        format: String,
    },

    /// List all traced functions
    Functions {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// List all traced threads
    Threads {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Find calls to a specific function
    Calls {
        /// Function name to search for (substring match)
        function: String,

        /// Maximum number of results
        #[arg(short, long, default_value = "1000")]
        limit: usize,

        /// Output format (text or json)
        #[arg(short = 'f', long, default_value = "text")]
        format: String,
    },
}
// LCOV_EXCL_STOP

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
        Commands::Query { bundle, command } => query::run(&bundle, command),
        // LCOV_EXCL_STOP
    }
}
