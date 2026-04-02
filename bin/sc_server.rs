use anyhow::{Result, bail};
use tokio::io::{AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use scatter::packet::Packet;

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
    // 使用自定义数据包读取目标地址
    let target_packet = Packet::read_from(&mut client_stream).await?;
    let target_addr = String::from_utf8(target_packet.payload)?;

    println!("代为连接目标: {}", target_addr);

    match TcpStream::connect(&target_addr).await {
        Ok(mut internet_stream) => {
            client_stream.write_all(&[0x00]).await?;

            let (mut c_read, mut c_write) = client_stream.split();
            let (mut i_read, mut i_write) = internet_stream.split();

            loop {
                tokio::select! {
                    res = Packet::read_from(&mut c_read) => {
                        match res {
                            Ok(packet) => {
                                // 【可在此处理客户端→互联网的数据包】
                                println!("客户端→互联网 数据包大小: {} 字节, 加密: {}, 压缩: {}", 
                                    packet.payload.len(), packet.header.encryption, packet.header.compression);
                                
                                i_write.write_all(&packet.payload).await?;
                            }
                            Err(_) => break,
                        }
                    }
                    res = Packet::read_from(&mut i_read) => {
                        match res {
                            Ok(packet) => {
                                // 【可在此处理互联网→客户端的数据包】
                                println!("互联网→客户端 数据包大小: {} 字节, 加密: {}, 压缩: {}", 
                                    packet.payload.len(), packet.header.encryption, packet.header.compression);
                                
                                let response_packet = Packet::new(0x00, 0x00, packet.payload);
                                response_packet.write_to(&mut c_write).await?;
                            }
                            Err(_) => break,
                        }
                    }
                }
            }
            Ok(())
        }
        Err(e) => {
            let _ = client_stream.write_all(&[0x01]).await;
            bail!("Connect target {} failed: {}", target_addr, e);
        }
    }
}
