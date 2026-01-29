
#include "socks5_server.hpp"
#include "transport_pool.hpp"
#include "util.hpp"
#include <asio.hpp>
#include <iostream>
#include <thread>

using namespace scatter;

int main(int argc, char **argv) {
  std::string socks_bind = "127.0.0.1:1080";
  std::string server = "127.0.0.1:46080";
  int pool = 8;
  int threads = std::max(2u, std::thread::hardware_concurrency());
  uint32_t shard_size = 1400;
  uint16_t shard_count = 8, parity = 1, copies = 2;
  std::string key_hex;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](int &i) -> std::string {
      if (i + 1 < argc)
        return std::string(argv[++i]);
      std::cerr << "missing value for " << a << "\n";
      std::exit(1);
    };
    if (a == "--socks-listen")
      socks_bind = next(i);
    else if (a == "--server")
      server = next(i);
    else if (a == "--pool")
      pool = std::stoi(next(i));
    else if (a == "--threads")
      threads = std::stoi(next(i));
    else if (a == "--shard-size")
      shard_size = (uint32_t)std::stoi(next(i));
    else if (a == "--shards")
      shard_count = (uint16_t)std::stoi(next(i));
    else if (a == "--parity")
      parity = (uint16_t)std::stoi(next(i));
    else if (a == "--copies")
      copies = (uint16_t)std::stoi(next(i));
    else if (a == "--key")
      key_hex = next(i);
  }

  std::string host;
  uint16_t port;
  if (!parse_host_port(socks_bind, host, port)) {
    std::cerr << "bad socks listen" << std::endl;
    return 1;
  }
  std::string shost;
  uint16_t sport;
  if (!parse_host_port(server, shost, sport)) {
    std::cerr << "bad server" << std::endl;
    return 1;
  }

  ClientConfig cfg;
  cfg.server_host = shost;
  cfg.server_port = sport;
  cfg.pool_size = pool;
  cfg.threads = threads;
  cfg.shard_size = shard_size;
  cfg.shard_count = shard_count;
  cfg.parity_count = parity;
  cfg.copies_per_shard = copies;
  cfg.key = hex_to_bytes(key_hex);
  if (cfg.key.empty())
    cfg.key = hex_to_bytes(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");

  asio::io_context io;
  auto pool_ptr = std::make_shared<TransportPool>(io, cfg);
  pool_ptr->start();

  asio::ip::tcp::endpoint ep(asio::ip::make_address(host), port);
  Socks5Server s(io, ep, *pool_ptr);
  s.start();

  std::vector<std::thread> th;
  th.reserve(cfg.threads);
  for (int i = 0; i < cfg.threads; i++)
    th.emplace_back([&]() { io.run(); });
  for (auto &t : th)
    t.join();
  return 0;
}
