
#include "remote_connector.hpp"
#include "logging.hpp"

namespace scatter {

RemoteConnector::RemoteConnector(asio::io_context &io, uint64_t session_id,
                                 std::string host, uint16_t port,
                                 ServerPool &pool,
                                 std::shared_ptr<ServerPool::Conn> conn_ref)
    : io_(io), session_id_(session_id), host_(std::move(host)), port_(port),
      pool_(pool), conn_ref_(conn_ref), resolver_(io), remote_(io) {}

void RemoteConnector::start() {
  auto self = shared_from_this();
  resolver_.async_resolve(
      host_, std::to_string(port_), [this, self](std::error_code ec, auto res) {
        if (ec) {
          Logger::instance().log(LogLevel::ERROR, "resolve fail: %s",
                                 ec.message().c_str());
          return;
        }
        asio::async_connect(
            remote_, res,
            [this, self](std::error_code ec, const asio::ip::tcp::endpoint &) {
              if (ec) {
                Logger::instance().log(LogLevel::ERROR, "connect fail: %s",
                                       ec.message().c_str());
                return;
              }
              remote_ready_ = true;
              // flush pending
              while (!pending_writes_.empty()) {
                auto data = std::move(pending_writes_.front());
                pending_writes_.pop_front();
                asio::async_write(
                    remote_, asio::buffer(data),
                    [this, self](std::error_code, std::size_t) {});
              }
              read_from_remote();
            });
      });
}

void RemoteConnector::on_uplink_shard(const FrameHeader &hdr,
                                      const std::vector<uint8_t> &payload) {
  auto &bs = blocks_[hdr.block_id];
  if (bs.shard_count == 0) {
    bs.shard_count = hdr.shard_count;
    bs.parity_count = hdr.parity_count;
    bs.shard_size = (uint32_t)payload.size();
    bs.shards.resize(bs.shard_count);
    bs.present.assign(bs.shard_count, false);
  }
  if (hdr.shard_id < bs.shard_count && !bs.present[hdr.shard_id]) {
    bs.shards[hdr.shard_id] = payload;
    bs.present[hdr.shard_id] = true;
  }
  bool complete = true;
  for (bool p : bs.present)
    if (!p) {
      complete = false;
      break;
    }
  if (complete)
    try_flush_block(hdr.block_id);
}

void RemoteConnector::try_flush_block(uint64_t block_id) {
  auto it = blocks_.find(block_id);
  if (it == blocks_.end())
    return;
  auto &bs = it->second;
  std::vector<uint8_t> out;
  out.reserve(bs.shard_count * bs.shard_size);
  for (size_t i = 0; i < bs.shard_count; i++)
    out.insert(out.end(), bs.shards[i].begin(), bs.shards[i].end());
  while (!out.empty() && out.back() == 0)
    out.pop_back();

  auto self = shared_from_this();
  if (!remote_ready_) {
    pending_writes_.push_back(std::move(out));
  } else {
    asio::async_write(remote_, asio::buffer(out),
                      [this, self](std::error_code, std::size_t) {});
  }
  blocks_.erase(it);
}

void RemoteConnector::read_from_remote() {
  auto self = shared_from_this();
  auto buf = std::make_shared<std::vector<uint8_t>>(64 * 1024);
  remote_.async_read_some(
      asio::buffer(*buf), [this, self, buf](std::error_code ec, std::size_t n) {
        if (ec)
          return;
        auto conn = conn_ref_.lock();
        if (!conn)
          return;

        uint16_t copies = pool_.cfg_.copies_per_shard;
        ShardPlan plan{pool_.cfg_.shard_count, pool_.cfg_.parity_count,
                       pool_.cfg_.shard_size, copies};
        SimpleFEC fec(plan);
        std::vector<std::vector<uint8_t>> data_shards, parity_shards;
        std::vector<uint8_t> data(buf->begin(), buf->begin() + n);

        static std::unordered_map<uint64_t, uint64_t> down_block_id;
        uint64_t &bid = down_block_id[session_id_];

        fec.encode(data, data_shards, parity_shards);

        for (uint16_t i = 0; i < plan.shard_count; i++) {
          std::vector<uint8_t> payload = std::move(data_shards[i]);
          pool_.crypto_->encrypt(session_id_, bid, i,
                                 (uint8_t)Direction::Downlink, payload);
          FrameHeader h{};
          h.magic = kMagic;
          h.version = kVersion;
          h.flags = FF_DATA;
          h.direction = (uint8_t)Direction::Downlink;
          h.session_id = session_id_;
          h.block_id = bid;
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
            pool_.send_via(conn, Frame{hd, payload});
          }
        }

        for (uint16_t pi = 0; pi < parity_shards.size(); ++pi) {
          std::vector<uint8_t> payload = std::move(parity_shards[pi]);
          pool_.crypto_->encrypt(session_id_, bid,
                                 (uint16_t)(plan.shard_count + pi),
                                 (uint8_t)Direction::Downlink, payload);
          FrameHeader h{};
          h.magic = kMagic;
          h.version = kVersion;
          h.flags = FF_PARITY;
          h.direction = (uint8_t)Direction::Downlink;
          h.session_id = session_id_;
          h.block_id = bid;
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
            pool_.send_via(conn, Frame{hd, payload});
          }
        }

        bid++;
        read_from_remote();
      });
}

} // namespace scatter
