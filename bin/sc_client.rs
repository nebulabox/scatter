use anyhow::Result;
use scatter::client::{ClientConfigArgs, run};

#[tokio::main]
async fn main() -> Result<()> {
    run(ClientConfigArgs::default()).await
}
