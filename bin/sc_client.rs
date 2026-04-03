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
    let mut config = ClientConfigArgs::default();
    config.listen_addr = args.bind_addr;
    config.server_addr = args.server_addr;
    let server_sock_addr: SocketAddr = config.server_addr.parse()?;
    config.server_name = server_sock_addr.ip().to_string();
    run(config).await
}
