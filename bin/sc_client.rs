use anyhow::Result;
use clap::Parser;
use scatter::client::{ClientConfigArgs, run};

#[derive(Debug, Parser)]
#[command(name = "sc_client", about = "Scatter SOCKS5 client")]
struct Args {
    #[arg(long, help = "SOCKS bind address, e.g. 127.0.0.1")]
    bind_addr: String,
    #[arg(long, help = "SOCKS bind port, e.g. 19080")]
    bind_port: u16,
    #[arg(long, help = "Remote server address, e.g. 127.0.0.1")]
    server_addr: String,
    #[arg(long, help = "Remote server port, e.g. 19911")]
    server_port: u16,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();
    let mut config = ClientConfigArgs::default();
    config.listen_addr = format!("{}:{}", args.bind_addr, args.bind_port);
    config.server_addr = format!("{}:{}", args.server_addr, args.server_port);
    config.server_name = args.server_addr;
    run(config).await
}
