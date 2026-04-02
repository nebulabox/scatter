use anyhow::{Result, bail};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

#[tokio::main]
async fn main() -> Result<()> {
    let addr = "127.0.0.1:19080";
    let listener = TcpListener::bind(addr).await?;
    println!("SOCKS5 代理监听在: {}", addr);

    loop {
        let (socket, _) = listener.accept().await?;
        // 为每个连接开一个协程（Task）
        tokio::spawn(async move {
            if let Err(e) = handle_client(socket).await {
                eprintln!("处理连接出错: {}", e);
            }
        });
    }
}

async fn handle_client(mut socket: TcpStream) -> Result<()> {
    // 1. 处理版本和认证协商
    /* 客户端发送 : | 0x05 (版本号) | 支持的认证方法数量 | 每个字节代表一种方法 | */
    let mut header = [0u8; 2];
    socket.read_exact(&mut header).await?;
    if header[0] != 0x05 {
        bail!("仅支持 SOCKS5");
    }

    let n_methods = header[1] as usize;
    let mut methods = vec![0u8; n_methods];
    socket.read_exact(&mut methods).await?;

    /* 服务器回复 : | 0x05 (版本号) | 选择的认证方法 | */
    // 回复：版本 5，选择“无需认证”模式 (0x00)
    socket.write_all(&[0x05, 0x00]).await?;

    // 2. 处理请求路由
    /* 客户端发送 : | VER (1) | CMD (1) | 0x00 (保留) | ATYP (1) | DST.ADDR (变长) | DST.PORT (2) |
       CMD (命令码) 0x01: CONNECT 0x02: BIND 0x03: UDP ASSOCIATE
       ATYP (地址类型) 0x01: IPv4   0x03: 域名   0x04: IPv6
       SOCKS5 规定端口号使用大端序 (Network Byte Order)
    */
    /* 服务端回复 :  | VER (1) | REP (1) | 0x00 (保留) | ATYP (1) | BND.ADDR (变长) | BND.PORT (2) |
      REP (回复码) 0x00: 成功  0x01: 一般错误  0x02: 连接不允许  0x03: 网络不可达
                   0x04: 主机不可达  0x05: 连接拒绝  0x06: TTL过期  0x07: 不支持的命令
                   0x08: 不支持的地址类型 0x09: 其他错误
       BND.ADDR 和 BND.PORT 是服务器端的绑定地址和端口，通常在 CONNECT 命令中不重要，可以填充为 0
    */
    let mut request_header = [0u8; 4];
    socket.read_exact(&mut request_header).await?;

    let ver = request_header[0];
    let cmd = request_header[1]; // 0x01 是 CONNECT
    let atyp = request_header[3]; // 地址类型：0x01 (IPv4), 0x03 (Domain), 0x04 (IPv6)

    if ver != 0x05 || cmd != 0x01 {
        bail!("不支持的命令: {}", cmd);
    }

    // 解析目标地址
    let dest_addr = match atyp {
        0x01 => {
            // IPv4
            let mut ip = [0u8; 4];
            socket.read_exact(&mut ip).await?;
            format!("{}.{}.{}.{}", ip[0], ip[1], ip[2], ip[3])
        }
        0x03 => {
            // 域名
            let len = socket.read_u8().await? as usize;
            let mut domain = vec![0u8; len];
            socket.read_exact(&mut domain).await?;
            String::from_utf8(domain)?
        }
        _ => bail!("暂不支持该地址类型"),
    };

    let dest_port = socket.read_u16().await?;
    println!("客户端请求连接目标: {}:{}", dest_addr, dest_port);

    // 数据透传 (Relay) 握手和请求阶段完成，SOCKS5 协议就退场了。接下来的所有流量都是原始的 TCP 数据流，只需要做双向奔赴的搬运工

    // 1. 连接远程 Server
    let mut remote_server = TcpStream::connect("127.0.0.1:19911").await?;

    // 2. 告诉 Server 目标地址
    let target_info = format!("{}:{}", dest_addr, dest_port);
    let target_bytes = target_info.as_bytes();
    if target_bytes.len() > 512 {
        bail!("目标地址过长");
    } // 防御性编程

    remote_server.write_u16(target_bytes.len() as u16).await?;
    remote_server.write_all(target_bytes).await?;

    // 3. 【新增】等待 Server 确认它连接目标成功 (简单握手协议)
    let mut server_confirm = [0u8; 1];
    remote_server.read_exact(&mut server_confirm).await?;
    if server_confirm[0] != 0x00 {
        // 回复本地 SOCKS 客户端：主机不可达 (0x04)
        socket
            .write_all(&[0x05, 0x04, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
            .await?;
        bail!("远程服务器连接目标失败");
    }

    // 4. 回复本地 SOCKS 客户端：连接成功
    let reply = [0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0];
    socket.write_all(&reply).await?;

    // 5. 数据透传 (使用更现代的 API)
    let (mut local_read, mut local_write) = socket.split();
    let (mut server_read, mut server_write) = remote_server.split();

    // 只要有一方断开，就结束
    let _ = tokio::select! {
        res = tokio::io::copy(&mut local_read, &mut server_write) => res,
        res = tokio::io::copy(&mut server_read, &mut local_write) => res,
    };

    Ok(())
}
