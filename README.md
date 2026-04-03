# Scatter

A SOCKS5 relay tool with two binaries:

- `sc_server`
- `sc_client`

## Build

```bash
cargo build --release
```

## Usage

### 1. Start server

```bash
cargo run --bin sc_server -- -b 0.0.0.0:19111
```

Arguments:

- `-b, --bind-addr <ADDR:PORT>`: server bind address

### 2. Start client

```bash
cargo run --bin sc_client -- -b 127.0.0.1:19080 -s 127.0.0.1:19111
```

Arguments:

- `-b, --bind-addr <ADDR:PORT>`: local SOCKS5 bind address
- `-s, --server-addr <ADDR:PORT>`: remote server address

### 3. Configure your app proxy

Set your app to use SOCKS5 proxy at:

- `127.0.0.1:19080` (or your custom client bind address)

## Help

```bash
cargo run --bin sc_server -- --help
cargo run --bin sc_client -- --help
```
