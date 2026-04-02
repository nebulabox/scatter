use anyhow::{Result, bail};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use rand::Rng;

/// 数据包头结构
/// 总长度: 16 字节
#[derive(Debug, Clone)]
pub struct PacketHeader {
    pub payload_len: u32,      // 后续数据长度 (4 字节, 大端序) - 第一位
    pub version: u8,           // 协议版本 (1 字节)
    pub encryption: u8,        // 加密方法: 0x00=无加密, 0x01=XOR, 0x02=AES (1 字节)
    pub compression: u8,       // 压缩方法: 0x00=无压缩, 0x01=GZIP (1 字节)
    pub reserved1: u8,         // 保留字段 (1 字节, 目前未使用)
    pub reserved2: u32,        // 保留字段 (4 字节, 大端序, 目前未使用)
    pub reserved3: u32,        // 保留字段 (4 字节, 大端序, 目前未使用)
}

impl PacketHeader {
    pub const SIZE: usize = 16; // 实际头部大小
    pub const VERSION: u8 = 0x01;

    pub fn new(encryption: u8, compression: u8, payload_len: u32) -> Self {
        let mut rng = rand::thread_rng();
        PacketHeader {
            payload_len,
            version: Self::VERSION,
            encryption,
            compression,
            reserved1: rng.gen_range(0..=255),
            reserved2: rng.gen_range(0..=u32::MAX),
            reserved3: rng.gen_range(0..=u32::MAX),
        }
    }

    /// 序列化为字节
    pub fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut buf = [0u8; Self::SIZE];
        buf[0..4].copy_from_slice(&self.payload_len.to_be_bytes());
        buf[4] = self.version;
        buf[5] = self.encryption;
        buf[6] = self.compression;
        buf[7] = self.reserved1;
        buf[8..12].copy_from_slice(&self.reserved2.to_be_bytes());
        buf[12..16].copy_from_slice(&self.reserved3.to_be_bytes());
        buf
    }

    /// 从字节反序列化
    pub fn from_bytes(buf: &[u8; Self::SIZE]) -> Result<Self> {
        Ok(PacketHeader {
            payload_len: u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]),
            version: buf[4],
            encryption: buf[5],
            compression: buf[6],
            reserved1: buf[7],
            reserved2: u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]),
            reserved3: u32::from_be_bytes([buf[12], buf[13], buf[14], buf[15]]),
        })
    }
}

/// 完整数据包
#[derive(Debug)]
pub struct Packet {
    pub header: PacketHeader,
    pub payload: Vec<u8>,
}

impl Packet {
    pub fn new(encryption: u8, compression: u8, payload: Vec<u8>) -> Self {
        Packet {
            header: PacketHeader::new(encryption, compression, payload.len() as u32),
            payload,
        }
    }

    /// 写入数据包到流
    pub async fn write_to<W: AsyncWriteExt + Unpin>(
        &self,
        writer: &mut W,
    ) -> Result<()> {
        let header_bytes = self.header.to_bytes();
        writer.write_all(&header_bytes).await?;
        writer.write_all(&self.payload).await?;
        Ok(())
    }

    /// 从流中读取数据包
    pub async fn read_from<R: AsyncReadExt + Unpin>(reader: &mut R) -> Result<Self> {
        // 1. 读取头部 (16 字节)
        let mut header_buf = [0u8; PacketHeader::SIZE];
        reader.read_exact(&mut header_buf).await?;
        let header = PacketHeader::from_bytes(&header_buf)?;

        // 2. 验证协议版本
        if header.version != PacketHeader::VERSION {
            bail!("不支持的协议版本: {}", header.version);
        }

        // 3. 验证加密方法
        if header.encryption > 0x02 {
            bail!("不支持的加密方法: {}", header.encryption);
        }

        // 4. 验证压缩方法
        if header.compression > 0x01 {
            bail!("不支持的压缩方法: {}", header.compression);
        }

        // 5. 防御性编程：限制最大数据包大小
        if header.payload_len > 10 * 1024 * 1024 {
            // 10MB
            bail!("数据包过大: {} 字节", header.payload_len);
        }

        // 6. 读取负载数据
        let mut payload = vec![0u8; header.payload_len as usize];
        reader.read_exact(&mut payload).await?;

        Ok(Packet { header, payload })
    }
}

/// 简单加密/解密 (演示用)
pub fn xor_cipher(data: &[u8], key: &[u8]) -> Vec<u8> {
    data.iter()
        .enumerate()
        .map(|(i, &b)| b ^ key[i % key.len()])
        .collect()
}