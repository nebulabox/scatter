
#include "session.hpp"
#include "logging.hpp"
#include "transport_pool.hpp"

namespace scatter {

static constexpr uint8_t SOCKS_VERSION = 5;

Session::Session(asio::io_context &io, TransportPool &pool, uint64_t session_id,
                 tcp::socket sock)
    : io_(io), pool_(pool), session_id_(session_id),
      client_sock_(std::move(sock)) {}

void Session::start() { handle_socks_handshake(); }

void Session::handle_socks_handshake() {
  auto self = shared_from_this();
  auto buf = std::make_shared<std::vector<uint8_t>>(262);

  asio::async_read(
      client_sock_, asio::buffer(*buf, 2),
      [this, self, buf](std::error_code ec, std::size_t) {
        if (ec || (*buf)[0] != SOCKS_VERSION)
          return;
        uint8_t nmethods = (*buf)[1];
        asio::async_read(
            client_sock_, asio::buffer(buf->data() + 2, nmethods),
            [this, self, buf](std::error_code ec, std::size_t) {
              if (ec)
                return;
              uint8_t reply[2] = {SOCKS_VERSION, 0x00};
              asio::async_write(
                  client_sock_, asio::buffer(reply, 2),
                  [this, self](std::error_code ec, std::size_t) {
                    if (ec)
                      return;
                    auto rbuf = std::make_shared<std::vector<uint8_t>>(4);
                    asio::async_read(
                        client_sock_, asio::buffer(*rbuf),
                        [this, self, rbuf](std::error_code ec, std::size_t) {
                          if (ec)
                            return;
                          if ((*rbuf)[0] != SOCKS_VERSION || (*rbuf)[1] != 0x01)
                            return; // CONNECT only
                          uint8_t atyp = (*rbuf)[3];
                          if (atyp == 0x01) {
                            auto addr =
                                std::make_shared<std::vector<uint8_t>>(6);
                            asio::async_read(
                                client_sock_, asio::buffer(*addr),
                                [this, self, addr](std::error_code ec,
                                                   std::size_t) {
                                  if (ec)
                                    return;
                                  char ip[32];
                                  std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                                                (*addr)[0], (*addr)[1],
                                                (*addr)[2], (*addr)[3]);
                                  uint16_t port = (uint16_t)(((*addr)[4] << 8) |
                                                             (*addr)[5]);
                                  send_open_control(ip, port);
                                });
                          } else if (atyp == 0x03) {
                            auto lb = std::make_shared<std::vector<uint8_t>>(1);
                            asio::async_read(
                                client_sock_, asio::buffer(*lb),
                                [this, self, lb](std::error_code ec,
                                                 std::size_t) {
                                  if (ec)
                                    return;
                                  size_t l = (*lb)[0];
                                  auto db =
                                      std::make_shared<std::vector<uint8_t>>(l +
                                                                             2);
                                  asio::async_read(
                                      client_sock_, asio::buffer(*db),
                                      [this, self, db](std::error_code ec,
                                                       std::size_t) {
                                        if (ec)
                                          return;
                                        std::string host((char *)db->data(),
                                                         db->size() - 2);
                                        uint16_t port =
                                            (uint16_t)(((*db)[db->size() - 2]
                                                        << 8) |
                                                       (*db)[db->size() - 1]);
                                        send_open_control(host, port);
                                      });
                                });
                          } else {
                            // unsupported
                            return;
                          }
                        });
                  });
            });
      });
}

void Session::send_open_control(const std::string &host, uint16_t port) {
  // Reply success optimistically (keeps minimal SOCKS5)
  uint8_t rep[10] = {SOCKS_VERSION, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
  auto self = shared_from_this();
  asio::async_write(client_sock_, asio::buffer(rep, 10),
                    [this, self](std::error_code, std::size_t) {});

  std::vector<uint8_t> payload;
  payload.push_back(static_cast<uint8_t>(ControlType::SESSION_OPEN));
  uint16_t hl = (uint16_t)host.size();
  payload.push_back((uint8_t)(hl >> 8));
  payload.push_back((uint8_t)(hl & 0xFF));
  payload.insert(payload.end(), host.begin(), host.end());
  payload.push_back((uint8_t)(port >> 8));
  payload.push_back((uint8_t)(port & 0xFF));

  FrameHeader h{};
  h.magic = kMagic;
  h.version = kVersion;
  h.flags = FF_CONTROL;
  h.direction = (uint8_t)Direction::Uplink;
  h.session_id = session_id_;
  h.block_id = 0;
  h.shard_id = 0;
  h.shard_count = 0;
  h.parity_count = 0;

  // FIX: encrypt control payload (server always decrypts)
  pool_.crypto().encrypt(session_id_, 0, 0, (uint8_t)Direction::Uplink,
                         payload);
  h.payload_len = (uint32_t)payload.size();
  h.header_crc32 = crc32((const uint8_t *)&h, sizeof(h) - 4);

  pool_.send_frame(Frame{h, std::move(payload)});
  pump_read_from_client();
}

void Session::pump_read_from_client() {
  auto self = shared_from_this();
  auto buf = std::make_shared<std::vector<uint8_t>>(64 * 1024);
  client_sock_.async_read_some(
      asio::buffer(*buf), [this, self, buf](std::error_code ec, std::size_t n) {
        if (ec)
          return;
        std::vector<uint8_t> data(buf->begin(), buf->begin() + n);
        send_data_block(data);
        pump_read_from_client();
      });
}

void Session::send_data_block(const std::vector<uint8_t> &data) {
  ShardPlan plan = pool_.shard_plan();
  SimpleFEC fec(plan);
  std::vector<std::vector<uint8_t>> data_shards, parity_shards;
  fec.encode(data, data_shards, parity_shards);

  uint64_t block_id = next_block_id_++;

  for (uint16_t i = 0; i < plan.shard_count; i++) {
    std::vector<uint8_t> payload = std::move(data_shards[i]);
    pool_.crypto().encrypt(session_id_, block_id, i, (uint8_t)Direction::Uplink,
                           payload);
    FrameHeader h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.flags = FF_DATA;
    h.direction = (uint8_t)Direction::Uplink;
    h.session_id = session_id_;
    h.block_id = block_id;
    h.shard_id = i;
    h.shard_count = plan.shard_count;
    h.parity_count = plan.parity_count;
    h.payload_len = (uint32_t)payload.size();
    h.header_crc32 = crc32((const uint8_t *)&h, sizeof(h) - 4);
    for (uint16_t dup = 0; dup < plan.copies_per_shard; ++dup) {
      FrameHeader hd = h;
      if (dup > 0)
        hd.flags |= FF_DUP;
      hd.copy_seq = dup;
      pool_.send_frame(Frame{hd, payload});
    }
  }

  for (uint16_t pi = 0; pi < parity_shards.size(); ++pi) {
    std::vector<uint8_t> payload = std::move(parity_shards[pi]);
    pool_.crypto().encrypt(session_id_, block_id,
                           (uint16_t)(plan.shard_count + pi),
                           (uint8_t)Direction::Uplink, payload);
    FrameHeader h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.flags = FF_PARITY;
    h.direction = (uint8_t)Direction::Uplink;
    h.session_id = session_id_;
    h.block_id = block_id;
    h.shard_id = pi;
    h.shard_count = plan.shard_count;
    h.parity_count = plan.parity_count;
    h.payload_len = (uint32_t)payload.size();
    h.header_crc32 = crc32((const uint8_t *)&h, sizeof(h) - 4);
    for (uint16_t dup = 0; dup < plan.copies_per_shard; ++dup) {
      FrameHeader hd = h;
      if (dup > 0)
        hd.flags |= FF_DUP;
      hd.copy_seq = dup;
      pool_.send_frame(Frame{hd, payload});
    }
  }
}

void Session::on_frame(const FrameHeader &hdr, std::vector<uint8_t> &&payload) {
  // payload here is already decrypted by TransportPool
  auto &bs = down_blocks_[hdr.block_id];
  if (bs.shard_count == 0) {
    bs.shard_count = hdr.shard_count;
    bs.parity_count = hdr.parity_count;
    bs.shard_size = (uint32_t)payload.size();
    bs.shards.resize(bs.shard_count);
    bs.present.assign(bs.shard_count, false);
  }
  if ((hdr.flags & FF_DATA) && hdr.shard_id < bs.shard_count &&
      !bs.present[hdr.shard_id]) {
    bs.shards[hdr.shard_id] = payload;
    bs.present[hdr.shard_id] = true;
  }
  bool complete = true;
  for (bool p : bs.present)
    if (!p) {
      complete = false;
      break;
    }
  if (complete) {
    std::vector<uint8_t> out;
    out.reserve(bs.shard_count * bs.shard_size);
    for (size_t i = 0; i < bs.shard_count; i++)
      out.insert(out.end(), bs.shards[i].begin(), bs.shards[i].end());
    while (!out.empty() && out.back() == 0)
      out.pop_back();
    auto self = shared_from_this();
    asio::async_write(client_sock_, asio::buffer(out),
                      [this, self](std::error_code, std::size_t) {});
    down_blocks_.erase(hdr.block_id);
  }
}

} // namespace scatter
