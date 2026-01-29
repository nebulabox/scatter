
#include "transport_pool.hpp"
#include "logging.hpp"
#include "session.hpp"
#include <cstring>

namespace scatter {

TransportPool::TransportPool(asio::io_context &io, const ClientConfig &cfg)
    : io_(io), cfg_(cfg) {
#ifdef SCATTER_HAVE_SODIUM
  crypto_.reset(new SodiumAead());
#else
  crypto_.reset(new XorStreamCipher());
#endif
  crypto_->set_key(cfg_.key);
}

void TransportPool::start() {
  conns_.resize(cfg_.pool_size);
  for (int i = 0; i < cfg_.pool_size; i++) {
    auto c = std::make_shared<Conn>(io_);
    conns_[i] = c;
    connect_one(c);
  }
}

void TransportPool::connect_one(std::shared_ptr<Conn> c) {
  tcp::resolver res(io_);
  auto results =
      res.resolve(cfg_.server_host, std::to_string(cfg_.server_port));
  asio::async_connect(
      c->sock, results, [this, c](std::error_code ec, const tcp::endpoint &) {
        if (ec) {
          Logger::instance().log(LogLevel::WARN, "pool connect failed: %s",
                                 ec.message().c_str());
          auto timer = std::make_shared<asio::steady_timer>(
              io_, std::chrono::seconds(1));
          timer->async_wait([this, c, timer](auto) { connect_one(c); });
          return;
        }
        Logger::instance().log(LogLevel::INFO, "pool connected");
        do_read(c);
        do_write(c);
      });
}

void TransportPool::do_read(std::shared_ptr<Conn> c) {
  c->sock.async_read_some(
      asio::buffer(c->read_buf), [this, c](std::error_code ec, std::size_t n) {
        if (ec) {
          Logger::instance().log(LogLevel::WARN, "pool read error: %s",
                                 ec.message().c_str());
          std::error_code ec2;
          c->sock.close(ec2);
          connect_one(c);
          return;
        }
        parse_and_handle(c, c->read_buf.data(), n);
        do_read(c);
      });
}

void TransportPool::do_write(std::shared_ptr<Conn> c) {
  if (c->write_q.empty())
    return;
  auto &front = c->write_q.front();
  asio::async_write(
      c->sock, asio::buffer(front), [this, c](std::error_code ec, std::size_t) {
        if (ec) {
          Logger::instance().log(LogLevel::WARN, "pool write error: %s",
                                 ec.message().c_str());
          std::error_code ec2;
          c->sock.close(ec2);
          connect_one(c);
          return;
        }
        c->write_q.pop_front();
        if (!c->write_q.empty())
          do_write(c);
      });
}

void TransportPool::send_frame(Frame &&f) {
  std::vector<uint8_t> buf(sizeof(FrameHeader) + f.payload.size());
  std::memcpy(buf.data(), &f.hdr, sizeof(FrameHeader));
  if (!f.payload.empty())
    std::memcpy(buf.data() + sizeof(FrameHeader), f.payload.data(),
                f.payload.size());
  size_t idx = rr_.fetch_add(1) % conns_.size();
  auto c = conns_[idx];
  c->write_q.emplace_back(std::move(buf));
  if (c->write_q.size() == 1)
    do_write(c);
}

void TransportPool::parse_and_handle(std::shared_ptr<Conn> c,
                                     const uint8_t *data, size_t len) {
  c->inbuf.insert(c->inbuf.end(), data, data + len);
  size_t off = 0;
  while (c->inbuf.size() - off >= sizeof(FrameHeader)) {
    FrameHeader hdr;
    std::memcpy(&hdr, c->inbuf.data() + off, sizeof(hdr));
    if (hdr.magic != kMagic || hdr.version != kVersion) {
      off += 1;
      continue;
    }
    size_t need = sizeof(FrameHeader) + hdr.payload_len;
    if (c->inbuf.size() - off < need)
      break;
    std::vector<uint8_t> payload(hdr.payload_len);
    if (hdr.payload_len)
      std::memcpy(payload.data(), c->inbuf.data() + off + sizeof(FrameHeader),
                  hdr.payload_len);
    if (!crypto_->decrypt(hdr.session_id, hdr.block_id, hdr.shard_id,
                          hdr.direction, payload)) {
      Logger::instance().log(LogLevel::WARN, "decrypt failed for session=%llu",
                             (unsigned long long)hdr.session_id);
      off += need;
      continue;
    }
    auto it = sessions_.find(hdr.session_id);
    if (it != sessions_.end())
      it->second->on_frame(hdr, std::move(payload));
    off += need;
  }
  if (off > 0)
    c->inbuf.erase(c->inbuf.begin(), c->inbuf.begin() + off);
}

void TransportPool::start_socks_session(tcp::socket sock) {
  static std::atomic<uint64_t> sid{1};
  uint64_t id = sid.fetch_add(1);
  auto s = std::make_shared<Session>(io_, *this, id, std::move(sock));
  sessions_[id] = s;
  s->start();
}

} // namespace scatter
