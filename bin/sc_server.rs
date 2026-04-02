use anyhow::{Result, bail};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind("0.0.0.0:19911").await?;
    println!("Server 监听在 19911 端口...");

    loop {
        let (socket, _) = listener.accept().await?;

        tokio::spawn(async move {
            if let Err(e) = handle_connection(socket).await {
                eprintln!("Server 处理任务出错: {}", e);
            }
        });
    }
}

async fn handle_connection(mut client_stream: TcpStream) -> Result<()> {
    // 1. 读取目标地址 (带长度检查)
    let len = client_stream.read_u16().await? as usize;
    if len > 512 {
        bail!("Invalid address length");
    }

    let mut buf = vec![0u8; len];
    client_stream.read_exact(&mut buf).await?;
    let target_addr = String::from_utf8(buf)?;

    println!("代为连接目标: {}", target_addr);

    // 2. 连接互联网目标并反馈给 Client
    match TcpStream::connect(&target_addr).await {
        Ok(mut internet_stream) => {
            client_stream.write_all(&[0x00]).await?; // 成功标记

            let (mut c_read, mut c_write) = client_stream.split();
            let (mut i_read, mut i_write) = internet_stream.split();

            // 拆分为独立数据包处理
            const PACKET_SIZE: usize = 4096;
            let mut client_buf = vec![0u8; PACKET_SIZE];
            let mut internet_buf = vec![0u8; PACKET_SIZE];

            loop {
                tokio::select! {
                    res = c_read.read(&mut client_buf) => {
                        match res? {
                            0 => break,
                            n => {
                                let packet = &client_buf[..n];
                                // 【可在此处理客户端→互联网的数据包】
                                println!("客户端→互联网 数据包大小: {} 字节", n);
                                i_write.write_all(packet).await?;
                            }
                        }
                    }
                    res = i_read.read(&mut internet_buf) => {
                        match res? {
                            0 => break,
                            n => {
                                let packet = &internet_buf[..n];
                                // 【可在此处理互联网→客户端的数据包】
                                println!("互联网→客户端 数据包大小: {} 字节", n);
                                c_write.write_all(packet).await?;
                            }
                        }
                    }
                }
            }
            Ok(())
        }
        Err(e) => {
            // 即使连接失败，也尝试通知客户端，但不强求通知成功
            let _ = client_stream.write_all(&[0x01]).await;
            bail!("Connect target {} failed: {}", target_addr, e);
        }
    }
}
