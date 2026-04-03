use anyhow::{Result, bail};
use quinn::{Endpoint, ClientConfig};
use rustls::pki_types::{CertificateDer, ServerName};
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use scatter::packet::Packet;

#[derive(Debug)]
struct SkipServerVerification;

impl rustls::client::danger::ServerCertVerifier for SkipServerVerification {
    fn verify_server_cert(
        &self,
        _end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp_response: &[u8],
        _now: rustls::pki_types::UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }

    fn verify_tls13_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        Ok(rustls::client::danger::HandshakeSignatureValid::assertion())
    }
    
    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        vec![
            rustls::SignatureScheme::RSA_PKCS1_SHA1,
            rustls::SignatureScheme::ECDSA_SHA1_Legacy,
            rustls::SignatureScheme::RSA_PKCS1_SHA256,
            rustls::SignatureScheme::ECDSA_NISTP256_SHA256,
            rustls::SignatureScheme::RSA_PKCS1_SHA384,
            rustls::SignatureScheme::ECDSA_NISTP384_SHA384,
            rustls::SignatureScheme::RSA_PKCS1_SHA512,
            rustls::SignatureScheme::ECDSA_NISTP521_SHA512,
            rustls::SignatureScheme::RSA_PSS_SHA256,
            rustls::SignatureScheme::RSA_PSS_SHA384,
            rustls::SignatureScheme::RSA_PSS_SHA512,
            rustls::SignatureScheme::ED25519,
            rustls::SignatureScheme::ED448,
        ]
    }
}

fn create_quinn_endpoint() -> Result<Endpoint> {
    let mut crypto = rustls::ClientConfig::builder()
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(SkipServerVerification))
        .with_no_client_auth();
    crypto.alpn_protocols = vec![b"h3".to_vec()];

    let client_config = ClientConfig::new(Arc::new(
        quinn::crypto::rustls::QuicClientConfig::try_from(crypto)?
    ));

    let mut endpoint = Endpoint::client("0.0.0.0:0".parse()?)?;
    endpoint.set_default_client_config(client_config);

    Ok(endpoint)
}

#[tokio::main]
async fn main() -> Result<()> {
    let addr = "127.0.0.1:19080";
    let listener = TcpListener::bind(addr).await?;
    println!("SOCKS5 代理监听在: {}", addr);

    let endpoint = create_quinn_endpoint()?;

    loop {
        let (socket, _) = listener.accept().await?;
        let endpoint_clone = endpoint.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_client(socket, endpoint_clone).await {
                eprintln!("处理连接出错: {}", e);
            }
        });
    }
}

async fn handle_client(mut socket: TcpStream, quic_endpoint: Endpoint) -> Result<()> {
    // 1. 处理版本和认证协商
    let mut header = [0u8; 2];
    socket.read_exact(&mut header).await?;
    if header[0] != 0x05 {
        bail!("仅支持 SOCKS5");
    }

    let n_methods = header[1] as usize;
    let mut methods = vec![0u8; n_methods];
    socket.read_exact(&mut methods).await?;

    socket.write_all(&[0x05, 0x00]).await?;

    // 2. 处理请求路由
    let mut request_header = [0u8; 4];
    socket.read_exact(&mut request_header).await?;

    let ver = request_header[0];
    let cmd = request_header[1];
    let atyp = request_header[3];

    if ver != 0x05 || cmd != 0x01 {
        bail!("不支持的命令: {}", cmd);
    }

    let dest_addr = match atyp {
        0x01 => {
            let mut ip = [0u8; 4];
            socket.read_exact(&mut ip).await?;
            format!("{}.{}.{}.{}", ip[0], ip[1], ip[2], ip[3])
        }
        0x03 => {
            let len = socket.read_u8().await? as usize;
            let mut domain = vec![0u8; len];
            socket.read_exact(&mut domain).await?;
            String::from_utf8(domain)?
        }
        0x04 => {
            let mut ip = [0u8; 16];
            socket.read_exact(&mut ip).await?;
            format!(
                "{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}:{:02x}{:02x}",
                ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7],
                ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]
            )
        }
        _ => bail!("暂不支持该地址类型"),
    };

    let dest_port = socket.read_u16().await?;
    println!("客户端请求连接目标: {}:{}", dest_addr, dest_port);

    // 1. 连接远程 Server (HTTP/3 QUIC transport)
    let connection = quic_endpoint.connect("127.0.0.1:19911".parse()?, "localhost")?.await?;
    let (mut server_write, mut server_read) = connection.open_bi().await?;

    // 2. 告诉 Server 目标地址
    let target_info = format!("{}:{}", dest_addr, dest_port);
    let target_bytes = target_info.as_bytes();
    if target_bytes.len() > 512 {
        bail!("目标地址过长");
    }

    let packet = Packet::new(0x01, 0x01, target_bytes.to_vec());
    packet.write_to(&mut server_write).await?;

    // 3. 等待 Server 确认它连接目标成功
    let mut server_confirm = [0u8; 1];
    server_read.read_exact(&mut server_confirm).await?;
    if server_confirm[0] != 0x00 {
        socket
            .write_all(&[0x05, 0x04, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
            .await?;
        bail!("远程服务器连接目标失败");
    }

    // 4. 回复本地 SOCKS 客户端：连接成功
    let reply = [0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0];
    socket.write_all(&reply).await?;

    // 5. 数据透传
    let (mut local_read, mut local_write) = socket.split();

    let mut local_buf = vec![0u8; 4096];


    loop {
        tokio::select! {
            res = local_read.read(&mut local_buf) => {
                match res? {
                    0 => break,
                    n => {
                        let packet_data = &local_buf[..n];
                        println!("本地→服务器 数据包大小: {} 字节", n);
                        
                        let packet = Packet::new(0x01, 0x01, packet_data.to_vec());
                        packet.write_to(&mut server_write).await?;
                    }
                }
            }
            res = Packet::read_from(&mut server_read) => {
                match res {
                    Ok(packet) => {
                        println!("服务器→本地 数据包大小: {} 字节", packet.payload.len());
                        local_write.write_all(&packet.payload).await?;
                    }
                    Err(_) => break,
                }
            }
        }
    }

    Ok(())
}
