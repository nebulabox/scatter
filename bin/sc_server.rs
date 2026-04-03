use anyhow::Result;
use clap::Parser;
use scatter::server::{ServerConfigArgs, run};

#[derive(Debug, Parser)]
#[command(name = "sc_server", about = "Scatter QUIC server")]
struct Args {
    #[arg(long, help = "Server bind address, e.g. 0.0.0.0")]
    bind_addr: String,
    #[arg(long, help = "Server bind port, e.g. 19911")]
    bind_port: u16,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    run(ServerConfigArgs {
        listen_addr: format!("{}:{}", args.bind_addr, args.bind_port),
    })
    .await
}
