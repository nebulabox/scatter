use anyhow::Result;
use scatter::server::{ServerConfigArgs, run};

#[tokio::main]
async fn main() -> Result<()> {
    run(ServerConfigArgs::default()).await
}
