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
mod doctor;
mod ffi;
mod query;
mod session_state;
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

    /// Manage ADA capture sessions
    #[command(subcommand)]
    Session(session_state::SessionCommands),

    /// Check system health and dependencies
    #[command(subcommand)]
    Doctor(doctor::DoctorCommands),

    // LCOV_EXCL_START - Struct field definitions
    /// Query trace data from a bundle
    ///
    /// Bundle can be specified as:
    ///   - @latest: Most recent session
    ///   - Session ID: e.g., session_2026_01_24_14_56_19_a1b2c3
    ///   - Path: Direct path to session directory or .adabundle
    ///
    /// Examples:
    ///   ada query @latest summary
    ///   ada query session_2026_01_24_14_56_19_a1b2c3 events --limit 100
    ///   ada query ~/.ada/sessions/session_xxx/ events --thread 0 --limit 50
    ///   ada query /path/to/bundle.adabundle functions
    Query {
        /// Bundle path: @latest, session ID, or directory path
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

        /// Filter events with timestamp >= this value (nanoseconds)
        #[arg(long)]
        since_ns: Option<u64>,

        /// Filter events with timestamp <= this value (nanoseconds)
        #[arg(long)]
        until_ns: Option<u64>,

        /// Output format (text, json, or line)
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

        /// Output format (text, json, or line)
        #[arg(short = 'f', long, default_value = "text")]
        format: String,
    },

    /// Show session time bounds and duration
    TimeInfo {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Show available query capabilities and tool requirements
    Capabilities {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Transcribe voice recording from session
    #[command(subcommand)]
    Transcribe(TranscribeCommands),

    /// Extract screenshot from screen recording
    Screenshot {
        /// Time in seconds to extract frame from
        #[arg(short, long)]
        time: f64,

        /// Output file path (if not specified, outputs to session directory)
        #[arg(short, long)]
        output: Option<std::path::PathBuf>,

        /// Output format (text or json)
        #[arg(short = 'f', long, default_value = "text")]
        format: String,
    },
}

/// Transcribe subcommands
#[derive(Subcommand)]
pub enum TranscribeCommands {
    /// Get transcript metadata without loading full content
    Info {
        /// Output format (text or json)
        #[arg(short, long, default_value = "text")]
        format: String,
    },

    /// Get transcript segments with pagination
    Segments {
        /// Number of segments to skip
        #[arg(short, long, default_value = "0")]
        offset: usize,

        /// Maximum number of segments to return
        #[arg(short, long, default_value = "20")]
        limit: usize,

        /// Filter segments starting at or after this time (seconds)
        #[arg(long)]
        since: Option<f64>,

        /// Filter segments ending at or before this time (seconds)
        #[arg(long)]
        until: Option<f64>,

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

    // LCOV_EXCL_START - CLI entry point, tested via integration
    match cli.command {
        Commands::Trace(cmd) => trace::run(cmd),
        Commands::Symbols(cmd) => symbols::run(cmd),
        Commands::Capture(cmd) => capture::run(cmd),
        Commands::Session(cmd) => session_state::run(cmd),
        Commands::Doctor(cmd) => doctor::run(cmd),
        Commands::Query { bundle, command } => query::run(&bundle, command),
    }
    // LCOV_EXCL_STOP
}
