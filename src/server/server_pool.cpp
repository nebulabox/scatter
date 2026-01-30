
#include "server_pool.hpp"
#include "logging.hpp"
#include "remote_connector.hpp"
#include <cstring>

namespace scatter {

ServerPool::ServerPool(asio::io_context &io, const ServerConfig &cfg)
    : io_(io), cfg_(cfg), acceptor_(io) {
  crypto_.reset(new SodiumAead());
  crypto_->set_key(cfg_.key);
}

void ServerPool::start() {
  asio::ip::tcp::endpoint ep(asio::ip::make_address(cfg_.listen_host),
                             cfg_.listen_port);
  acceptor_.open(ep.protocol());
  acceptor_.set_option(asio::socket_base::reuse_address(true));
  acceptor_.bind(ep);
  acceptor_.listen();
  do_accept();
}

void ServerPool::do_accept() {
  auto c = std::make_shared<Conn>(io_);
  acceptor_.async_accept(c->sock, [this, c](std::error_code ec) {
    if (!ec) {
      Logger::instance().log(LogLevel::INFO, "pool accepted");
      do_read(c);
      do_write(c);
    }
    do_accept();
  });
}

void ServerPool::do_read(std::shared_ptr<Conn> c) {
  c->sock.async_read_some(asio::buffer(c->read_buf),
                          [this, c](std::error_code ec, std::size_t n) {
                            if (ec)
                              return;
                            parse_and_handle(c, c->read_buf.data(), n);
                            do_read(c);
                          });
}

void ServerPool::parse_and_handle(std::shared_ptr<Conn> c, const uint8_t *data,
                                  size_t n) {
  c->inbuf.insert(c->inbuf.end(), data, data + n);
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
      Logger::instance().log(LogLevel::WARN, "decrypt failed session=%llu",
                             (unsigned long long)hdr.session_id);
      off += need;
      continue;
    }
    handle_frame(c, hdr, std::move(payload));
    off += need;
  }
  if (off > 0)
    c->inbuf.erase(c->inbuf.begin(), c->inbuf.begin() + off);
}

void ServerPool::do_write(std::shared_ptr<Conn> c) {
  if (c->write_q.empty())
    return;
  auto &front = c->write_q.front();
  asio::async_write(c->sock, asio::buffer(front),
                    [this, c](std::error_code ec, std::size_t) {
                      if (ec)
                        return;
                      c->write_q.pop_front();
                      if (!c->write_q.empty())
                        do_write(c);
                    });
}

void ServerPool::send_via(std::shared_ptr<Conn> c, Frame &&f) {
  std::vector<uint8_t> buf(sizeof(FrameHeader) + f.payload.size());
  std::memcpy(buf.data(), &f.hdr, sizeof(FrameHeader));
  if (!f.payload.empty())
    std::memcpy(buf.data() + sizeof(FrameHeader), f.payload.data(),
                f.payload.size());
  c->write_q.emplace_back(std::move(buf));
  if (c->write_q.size() == 1)
    do_write(c);
}

void ServerPool::handle_frame(std::shared_ptr<Conn> c, const FrameHeader &hdr,
                              std::vector<uint8_t> &&payload) {
  if (hdr.flags & FF_CONTROL) {
    if (payload.empty())
      return;
    if (payload[0] != static_cast<uint8_t>(ControlType::SESSION_OPEN))
      return;
    if (payload.size() < 1 + 2 + 2)
      return;
    uint16_t hl = (payload[1] << 8) | payload[2];
    if (payload.size() < 1 + 2 + hl + 2)
      return;
    std::string host((char *)payload.data() + 3, hl);
    uint16_t port = (payload[3 + hl] << 8) | payload[3 + hl + 1];
    Logger::instance().log(LogLevel::INFO, "SESSION_OPEN sid=%llu -> %s:%u",
                           (unsigned long long)hdr.session_id, host.c_str(),
                           (unsigned)port);
    auto rc = std::make_shared<RemoteConnector>(io_, hdr.session_id, host, port,
                                                *this, c);
    sessions_[hdr.session_id] = rc;
    rc->start();
    return;
  }

  if (hdr.flags & FF_DATA) {
    auto it = sessions_.find(hdr.session_id);
    if (it != sessions_.end())
      it->second->on_uplink_shard(hdr, payload);
    return;
  }
}

} // namespace scatter
