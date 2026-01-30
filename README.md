# scatter
高可靠跨平台传输


## vcpkg 依赖
```bash
cd $VCPKG
# git clean -dxf
git reset --hard
git checkout tags/2026.01.16
vcpkg install asio libsodium
```

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=${VCPKG}/scripts/buildsystems/vcpkg.cmake
make -j8
```

## 运行

服务端：
```bash
./build/scatter_server --listen 0.0.0.0:46080 --threads 8 --shard-size 1400 --shards 8 --parity 1 --copies 2 --key 64513299
```

客户端：
```bash
./build/scatter_client --socks-listen 127.0.0.1:11080 --server 127.0.0.1:46080 --pool 8 --threads 8 --shard-size 1400 --shards 8 --parity 1 --copies 2 --key 64513299
```

测试：
```bash
curl -v -x socks5://127.0.0.1:11080 http://142.250.72.132
```

