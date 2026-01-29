
#pragma once
#include <asio.hpp>
#include <unordered_map>
#include <deque>
#include <memory>
#include "protocol.hpp"
#include "fec.hpp"
#include "server_pool.hpp"

namespace scatter {

class RemoteConnector : public std::enable_shared_from_this<RemoteConnector> {
public:
    using tcp = asio::ip::tcp;
    RemoteConnector(asio::io_context& io, uint64_t session_id, std::string host, uint16_t port,
                    ServerPool& pool, std::shared_ptr<ServerPool::Conn> conn_ref);
    void start();
    void on_uplink_shard(const FrameHeader& hdr, const std::vector<uint8_t>& payload);

private:
    void try_flush_block(uint64_t block_id);
    void read_from_remote();

    asio::io_context& io_;
    uint64_t session_id_;
    std::string host_;
    uint16_t port_;
    ServerPool& pool_;
    std::weak_ptr<ServerPool::Conn> conn_ref_;

    tcp::resolver resolver_;
    tcp::socket remote_;

    bool remote_ready_{false};
    std::deque<std::vector<uint8_t>> pending_writes_;

    struct BlockState {
        uint16_t shard_count{0};
        uint16_t parity_count{0};
        uint32_t shard_size{0};
        std::vector<std::vector<uint8_t>> shards;
        std::vector<bool> present;
    };

    std::unordered_map<uint64_t, BlockState> blocks_;
};

} // namespace scatter
