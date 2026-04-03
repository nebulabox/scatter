use anyhow::Result;
use clap::Parser;
use scatter::server::{ServerConfigArgs, run};

#[derive(Debug, Parser)]
#[command(name = "sc_server", about = "Scatter QUIC server")]
struct Args {
    #[arg(
        short = 'b',
        long,
        help = "Server bind address with port, e.g. 0.0.0.0:19111"
    )]
    bind_addr: String,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    run(ServerConfigArgs {
        listen_addr: args.bind_addr,
    })
    .await
}
