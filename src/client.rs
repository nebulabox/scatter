use crate::packet::Packet;
use crate::tuning::{
    PACKET_BATCH_MAX_BYTES, PACKET_BATCH_MAX_DELAY_MS, QUIC_IDLE_TIMEOUT_SECS, QUIC_KEEPALIVE_SECS,
    QUIC_MAX_CONCURRENT_BI_STREAMS, RELAY_BUFFER_SIZE,
};
use anyhow::{Result, bail};
use quinn::{ClientConfig, Connection, Endpoint, RecvStream, SendStream};
use rustls::pki_types::{CertificateDer, ServerName};
use std::net::{Ipv6Addr, SocketAddr};
use std::str;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Mutex;
use tokio::time::Instant;

const REMOTE_SERVER_NAME: &str = "analytics.itunes.apple.com";

#[derive(Debug, Clone)]
pub struct ClientConfigArgs {
    pub listen_addr: String,
    pub server_addr: String,
}

struct SharedQuicConnection {
    endpoint: Endpoint,
    remote_server_addr: SocketAddr,
    connection: Mutex<Option<Connection>>,
}

impl SharedQuicConnection {
    fn new(endpoint: Endpoint, remote_server_addr: SocketAddr) -> Self {
        Self {
            endpoint,
            remote_server_addr,
            connection: Mutex::new(None),
        }
    }

    async fn ensure_connected(&self) -> Result<Connection> {
        let mut guard = self.connection.lock().await;
        if let Some(connection) = guard.as_ref() {
            if connection.close_reason().is_none() {
                return Ok(connection.clone());
            }
            *guard = None;
        }

        let connection = self
            .endpoint
            .connect(self.remote_server_addr, REMOTE_SERVER_NAME)?
            .await?;
        *guard = Some(connection.clone());
        Ok(connection)
    }

    async fn open_bi(&self) -> Result<(SendStream, RecvStream)> {
        let connection = self.ensure_connected().await?;
        match connection.open_bi().await {
            Ok(stream) => Ok(stream),
            Err(_) => {
                let mut guard = self.connection.lock().await;
                *guard = None;
                drop(guard);

                let reconnected = self.ensure_connected().await?;
                Ok(reconnected.open_bi().await?)
            }
        }
    }
}

#[derive(Debug)]
struct InsecureNoopVerifier {
    supported_schemes: Vec<rustls::SignatureScheme>,
}

impl InsecureNoopVerifier {
    fn new() -> Self {
        Self {
            supported_schemes: rustls::crypto::ring::default_provider()
                .signature_verification_algorithms
                .supported_schemes(),
        }
    }
}

impl rustls::client::danger::ServerCertVerifier for InsecureNoopVerifier {
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
        self.supported_schemes.clone()
    }
}

fn build_transport_config() -> Result<quinn::TransportConfig> {
    let mut transport = quinn::TransportConfig::default();
    transport.keep_alive_interval(Some(Duration::from_secs(QUIC_KEEPALIVE_SECS)));
    transport.max_idle_timeout(Some(quinn::IdleTimeout::try_from(Duration::from_secs(
        QUIC_IDLE_TIMEOUT_SECS,
    ))?));
    transport.max_concurrent_bidi_streams(quinn::VarInt::from_u32(QUIC_MAX_CONCURRENT_BI_STREAMS));
    Ok(transport)
}

fn create_quinn_endpoint() -> Result<Endpoint> {
    let mut crypto = rustls::ClientConfig::builder()
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(InsecureNoopVerifier::new()))
        .with_no_client_auth();
    crypto.alpn_protocols = vec![b"h3".to_vec()];

    let mut client_config = ClientConfig::new(Arc::new(
        quinn::crypto::rustls::QuicClientConfig::try_from(crypto)?,
    ));
    client_config.transport_config(Arc::new(build_transport_config()?));

    let mut endpoint = Endpoint::client("0.0.0.0:0".parse()?)?;
    endpoint.set_default_client_config(client_config);

    Ok(endpoint)
}

pub async fn run(config: ClientConfigArgs) -> Result<()> {
    let listener = TcpListener::bind(&config.listen_addr).await?;
    println!("SOCKS5 代理监听在: {}", config.listen_addr);

    let endpoint = create_quinn_endpoint()?;
    let remote_server_addr: SocketAddr = config.server_addr.parse()?;
    let shared_quic = Arc::new(SharedQuicConnection::new(endpoint, remote_server_addr));
    loop {
        let (socket, _) = listener.accept().await?;
        let shared_quic_clone = shared_quic.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_client(socket, shared_quic_clone).await {
                eprintln!("处理连接出错: {}", e);
            }
        });
    }
}

async fn handle_client(
    mut socket: TcpStream,
    shared_quic: Arc<SharedQuicConnection>,
) -> Result<()> {
    negotiate_socks5(&mut socket).await?;
    let (dest_addr, dest_port) = read_connect_target(&mut socket).await?;
    println!("客户端请求连接目标: {}:{}", dest_addr, dest_port);

    // 1. 在共享 QUIC 连接上为当前 SOCKS 会话打开一个独立双向流
    let (mut server_write, mut server_read) = shared_quic.open_bi().await?;

    // 2. 告诉 Server 目标地址
    let target_info = format!("{}:{}", dest_addr, dest_port);
    if target_info.len() > 512 {
        bail!("目标地址过长");
    }

    let packet = Packet::new(target_info.into_bytes());
    packet.write_to(&mut server_write).await?;

    // 3. 等待 Server 确认它连接目标成功
    let mut server_confirm = [0u8; 1];
    server_read.read_exact(&mut server_confirm).await?;
    if server_confirm[0] != 0x00 {
        // SOCKS5 失败回复: VER=0x05, REP=0x04(host unreachable), RSV=0x00, ATYP=IPv4, BND=0.0.0.0:0
        socket
            .write_all(&[0x05, 0x04, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
            .await?;
        bail!("远程服务器连接目标失败");
    }

    // 4. 回复本地 SOCKS 客户端：连接成功
    // SOCKS5 成功回复: VER=0x05, REP=0x00(ok), RSV=0x00, ATYP=IPv4, BND=0.0.0.0:0
    socket
        .write_all(&[0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
        .await?;

    // 5. 数据透传
    let (mut local_read, mut local_write) = socket.split();
    let mut local_buf = vec![0u8; RELAY_BUFFER_SIZE];
    let mut batched_local_payload = Vec::with_capacity(PACKET_BATCH_MAX_BYTES);
    let batch_delay = Duration::from_millis(PACKET_BATCH_MAX_DELAY_MS);
    let flush_timer = tokio::time::sleep(batch_delay);
    tokio::pin!(flush_timer);
    let mut timer_armed = false;

    loop {
        tokio::select! {
            res = local_read.read(&mut local_buf) => {
                match res? {
                    0 => {
                        flush_batched_payload(&mut batched_local_payload, &mut server_write).await?;
                        break;
                    }
                    n => {
                        if !timer_armed {
                            flush_timer.as_mut().reset(Instant::now() + batch_delay);
                            timer_armed = true;
                        }

                        batched_local_payload.extend_from_slice(&local_buf[..n]);
                        if batched_local_payload.len() >= PACKET_BATCH_MAX_BYTES {
                            flush_batched_payload(&mut batched_local_payload, &mut server_write).await?;
                            timer_armed = false;
                        }
                    }
                }
            }
            _ = &mut flush_timer, if timer_armed => {
                flush_batched_payload(&mut batched_local_payload, &mut server_write).await?;
                timer_armed = false;
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

async fn flush_batched_payload(
    batched_payload: &mut Vec<u8>,
    server_write: &mut SendStream,
) -> Result<()> {
    if batched_payload.is_empty() {
        return Ok(());
    }

    println!("本地→服务器 聚合数据包大小: {} 字节", batched_payload.len());
    let payload = std::mem::replace(batched_payload, Vec::with_capacity(PACKET_BATCH_MAX_BYTES));
    let packet = Packet::new(payload);
    packet.write_to(server_write).await?;
    Ok(())
}

async fn negotiate_socks5(socket: &mut TcpStream) -> Result<()> {
    let mut header = [0u8; 2];
    socket.read_exact(&mut header).await?;

    // SOCKS5 握手版本号固定为 0x05
    if header[0] != 0x05 {
        bail!("仅支持 SOCKS5");
    }

    let methods_len = header[1] as usize;
    let mut methods = vec![0u8; methods_len];
    socket.read_exact(&mut methods).await?;

    // 告诉客户端: 使用 "NO AUTH"(0x00)
    socket.write_all(&[0x05, 0x00]).await?;
    Ok(())
}

async fn read_connect_target(socket: &mut TcpStream) -> Result<(String, u16)> {
    let mut request_header = [0u8; 4];
    socket.read_exact(&mut request_header).await?;

    let version = request_header[0];
    let command = request_header[1];
    let address_type = request_header[3];

    // 仅支持 SOCKS5 CONNECT(0x01)
    if version != 0x05 || command != 0x01 {
        bail!("不支持的命令: {}", command);
    }

    let address = read_target_address(socket, address_type).await?;
    let port = socket.read_u16().await?;
    Ok((address, port))
}

async fn read_target_address(socket: &mut TcpStream, address_type: u8) -> Result<String> {
    match address_type {
        0x01 => {
            // ATYP=0x01: IPv4
            let mut ip = [0u8; 4];
            socket.read_exact(&mut ip).await?;
            Ok(format!("{}.{}.{}.{}", ip[0], ip[1], ip[2], ip[3]))
        }
        0x03 => {
            // ATYP=0x03: 域名
            let len = socket.read_u8().await? as usize;
            let mut domain = vec![0u8; len];
            socket.read_exact(&mut domain).await?;
            Ok(str::from_utf8(&domain)?.to_owned())
        }
        0x04 => {
            // ATYP=0x04: IPv6
            let mut ip = [0u8; 16];
            socket.read_exact(&mut ip).await?;
            Ok(Ipv6Addr::from(ip).to_string())
        }
        _ => bail!("暂不支持该地址类型"),
    }
}
