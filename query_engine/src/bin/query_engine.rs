use anyhow::Result;
use clap::Parser;
use query_engine::app::{self, AppConfig, Args};

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    app::init_tracing();
    let config = AppConfig::from(args);
    app::run(config).await
}
