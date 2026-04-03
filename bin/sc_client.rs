use anyhow::Result;
use clap::Parser;
use scatter::client::{ClientConfigArgs, run};
use std::net::SocketAddr;

#[derive(Debug, Parser)]
#[command(name = "sc_client", about = "Scatter SOCKS5 client")]
struct Args {
    #[arg(
        short = 'b',
        long,
        help = "SOCKS bind address with port, e.g. 0.0.0.0:19080"
    )]
    bind_addr: String,
    #[arg(
        short = 's',
        long,
        help = "Remote server address with port, e.g. 127.0.0.1:19111"
    )]
    server_addr: String,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    let _: SocketAddr = args.server_addr.parse()?;
    let config = ClientConfigArgs {
        listen_addr: args.bind_addr,
        server_addr: args.server_addr,
    };
    run(config).await
}
