
#include "server_pool.hpp"
#include "util.hpp"
#include <asio.hpp>
#include <iostream>
#include <thread>

using namespace scatter;

int main(int argc, char **argv) {
  std::string listen = "0.0.0.0:46080";
  int threads = std::max(2u, std::thread::hardware_concurrency());
  std::string key_hex;
  uint32_t shard_size = 1400;
  uint16_t shard_count = 8, parity = 1, copies = 2;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](int &i) -> std::string {
      if (i + 1 < argc)
        return std::string(argv[++i]);
      std::cerr << "missing value for " << a << "\n";
      std::exit(1);
    };
    if (a == "--listen")
      listen = next(i);
    else if (a == "--threads")
      threads = std::stoi(next(i));
    else if (a == "--key")
      key_hex = next(i);
    else if (a == "--shard-size")
      shard_size = (uint32_t)std::stoi(next(i));
    else if (a == "--shards")
      shard_count = (uint16_t)std::stoi(next(i));
    else if (a == "--parity")
      parity = (uint16_t)std::stoi(next(i));
    else if (a == "--copies")
      copies = (uint16_t)std::stoi(next(i));
  }

  std::string host;
  uint16_t port;
  if (!parse_host_port(listen, host, port)) {
    std::cerr << "bad listen" << std::endl;
    return 1;
  }

  ServerConfig cfg;
  cfg.listen_host = host;
  cfg.listen_port = port;
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
  ServerPool sp(io, cfg);
  sp.start();

  std::vector<std::thread> th;
  th.reserve(cfg.threads);
  for (int i = 0; i < cfg.threads; i++)
    th.emplace_back([&]() { io.run(); });
  for (auto &t : th)
    t.join();
  return 0;
}
