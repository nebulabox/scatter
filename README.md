
# scatter (最终修复版)

你反馈的“curl 走 socks5 卡住无返回”的根因是：

1. **控制帧（SESSION_OPEN）在客户端没有加密，但服务端对所有帧都做了解密**，导致服务端解析到的 host/port 是随机垃圾，远端连接无法建立，随后客户端仍然乐观返回 SOCKS5 成功，于是请求卡住。
2. **服务端在远端 TCP 连接尚未建立时就可能收到并尝试写入 uplink 数据块**，写未连接 socket 会失败并丢数据，造成长时间无响应。

本版本已修复：

- ✅ 控制帧与数据帧统一走 `CryptoProvider::encrypt/decrypt`（如果启用 libsodium 则为 AEAD）。
- ✅ 服务端 RemoteConnector 在远端连接未完成前会缓存数据块，连接建立后再 flush。
- ✅ ServerPool::Conn 类型可见性修复（macOS clang 下不再出现 `ServerPool::Conn` incomplete type）。

## vcpkg 依赖（可选）

- `libsodium`：启用 XChaCha20-Poly1305 AEAD。

```bash
vcpkg install libsodium
```

## 构建

```bash
cmake -S . -B build -DSCATTER_USE_STANDALONE_ASIO=ON -DASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build -j
```

## 运行

服务端：
```bash
./build/scatter_server --listen 0.0.0.0:46080 --threads 8 --shard-size 1400 --shards 8 --parity 1 --copies 2 --key <hex>
```

客户端：
```bash
./build/scatter_client --socks-listen 127.0.0.1:11080 --server 127.0.0.1:46080 --pool 8 --threads 8 --shard-size 1400 --shards 8 --parity 1 --copies 2 --key <hex>
```

测试：
```bash
curl -v -x socks5://127.0.0.1:11080 http://45.33.105.96
```

> 注意：`--key` 需要十六进制字符串（建议 64 hex 字符）。如果输入非法（如 `111`），程序会回退到默认 key。
