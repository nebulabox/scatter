use anyhow::{Result, bail};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

const XOR_KEY: &[u8] = b"scatter_super_secret_xor_key";

/// 完整数据包
#[derive(Debug)]
pub struct Packet {
    pub payload: Vec<u8>,
}

impl Packet {
    /// 构造数据包，强制进行无感的压缩和加密
    pub fn new(mut payload: Vec<u8>) -> Self {
        payload = lz4_flex::compress_prepend_size(&payload);
        payload = xor_cipher(&payload, XOR_KEY);
        Packet { payload }
    }

    /// 写入数据包到流
    pub async fn write_to<W: AsyncWriteExt + Unpin>(&self, writer: &mut W) -> Result<()> {
        let payload_len = self.payload.len() as u32;
        // 1. 写入 4 字节的长度头部 (大端序)
        writer.write_all(&payload_len.to_be_bytes()).await?;
        // 2. 写入负载数据
        writer.write_all(&self.payload).await?;
        Ok(())
    }

    /// 从流中读取并解密解压数据包
    pub async fn read_from<R: AsyncReadExt + Unpin>(reader: &mut R) -> Result<Self> {
        // 1. 读取 4 字节头部，获知数据包长度
        let mut len_buf = [0u8; 4];
        reader.read_exact(&mut len_buf).await?;
        let payload_len = u32::from_be_bytes(len_buf);

        // 2. 防御性编程：限制最大数据包大小 (10MB)
        if payload_len > 10 * 1024 * 1024 {
            bail!("数据包过大: {} 字节", payload_len);
        }

        // 3. 读取负载数据
        let mut payload = vec![0u8; payload_len as usize];
        reader.read_exact(&mut payload).await?;

        // 4. 解密
        payload = xor_cipher(&payload, XOR_KEY);
        
        // 5. 解压缩
        payload = match lz4_flex::decompress_size_prepended(&payload) {
            Ok(data) => data,
            Err(e) => bail!("数据解压出错: {}", e),
        };

        Ok(Packet { payload })
    }
}

/// 简单加密/解密 (XOR)
pub fn xor_cipher(data: &[u8], key: &[u8]) -> Vec<u8> {
    data.iter()
        .enumerate()
        .map(|(i, &b)| b ^ key[i % key.len()])
        .collect()
}