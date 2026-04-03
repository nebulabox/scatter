# Scatter

A SOCKS5 relay tool with two binaries:

- `sc_server`
- `sc_client`

## Usage

### 1. Start server

```bash
sc_server -b 0.0.0.0:19111
```

Arguments:

- `-b, --bind-addr <ADDR:PORT>`: server bind address

### 2. Start client

```bash
sc_client -b 127.0.0.1:19080 -s 127.0.0.1:19111
```

Arguments:

- `-b, --bind-addr <ADDR:PORT>`: local SOCKS5 bind address
- `-s, --server-addr <ADDR:PORT>`: remote server address

### 3. Configure your app proxy

Set your app to use SOCKS5 proxy at:

- `127.0.0.1:19080` (or your custom client bind address)

## Help

```bash
sc_server --help
sc_client --help
```
