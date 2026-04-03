use anyhow::{Result, bail};
use quinn::{Endpoint, ServerConfig};
use rustls::pki_types::{CertificateDer, PrivateKeyDer};
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use scatter::packet::Packet;

// Generate a dummy certificate for testing/proxy purposes
fn generate_dummy_cert() -> Result<(CertificateDer<'static>, PrivateKeyDer<'static>)> {
    let cert = rcgen::generate_simple_self_signed(vec!["localhost".into()])?;
    Ok((
        cert.cert.into(),
        rustls::pki_types::PrivatePkcs8KeyDer::from(cert.key_pair.serialize_der()).into(),
    ))
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let (cert, key) = generate_dummy_cert()?;
    let mut server_crypto = rustls::ServerConfig::builder()
        .with_no_client_auth()
        .with_single_cert(vec![cert], key)?;
    server_crypto.alpn_protocols = vec![b"h3".to_vec()]; // fake h3 alpn
    
    let server_config = ServerConfig::with_crypto(Arc::new(
        quinn::crypto::rustls::QuicServerConfig::try_from(server_crypto)?
    ));
    let endpoint = Endpoint::server(server_config, "0.0.0.0:19911".parse()?)?;
    println!("Server (QUIC/HTTP3 transport) 监听在 19911 端口...");

    while let Some(conn) = endpoint.accept().await {
        tokio::spawn(async move {
            match conn.await {
                Ok(connection) => {
                    // Accept a bidirectional stream from the client
                    match connection.accept_bi().await {
                        Ok((send, recv)) => {
                            if let Err(e) = handle_connection(send, recv).await {
                                eprintln!("Server 处理任务出错: {}", e);
                            }
                        }
                        Err(e) => eprintln!("Accept bi stream failed: {}", e),
                    }
                }
                Err(e) => eprintln!("Connection failed: {}", e),
            }
        });
    }
    
    Ok(())
}

async fn handle_connection(mut client_write: quinn::SendStream, mut client_read: quinn::RecvStream) -> Result<()> {
    // 使用自定义数据包读取目标地址
    let target_packet = Packet::read_from(&mut client_read).await?;
    let target_addr = String::from_utf8(target_packet.payload)?;

    println!("代为连接目标: {}", target_addr);

    match tokio::net::TcpStream::connect(&target_addr).await {
        Ok(mut internet_stream) => {
            client_write.write_all(&[0x00]).await?;

            let (mut i_read, mut i_write) = internet_stream.split();
            let mut internet_buf = vec![0u8; 4096];

            loop {
                tokio::select! {
                    res = Packet::read_from(&mut client_read) => {
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
                    res = i_read.read(&mut internet_buf) => {
                        match res {
                            Ok(0) | Err(_) => break, // EOF or error
                            Ok(n) => {
                                let payload = &internet_buf[..n];
                                println!("互联网→客户端 数据包大小: {} 字节, 加密: {}, 压缩: {}", 
                                    payload.len(), 0x00, 0x00);
                                
                                let response_packet = Packet::new(0x00, 0x00, payload.to_vec());
                                if let Err(_) = response_packet.write_to(&mut client_write).await {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            Ok(())
        }
        Err(e) => {
            let _ = client_write.write_all(&[0x01]).await;
            bail!("Connect target {} failed: {}", target_addr, e);
        }
    }
}
