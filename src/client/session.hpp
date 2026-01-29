
#pragma once
#include <asio.hpp>
#include <unordered_map>
#include "protocol.hpp"
#include "fec.hpp"

namespace scatter {
class TransportPool;

class Session : public std::enable_shared_from_this<Session> {
public:
    using tcp = asio::ip::tcp;
    Session(asio::io_context& io, TransportPool& pool, uint64_t session_id, tcp::socket sock);
    void start();
    void on_frame(const FrameHeader& hdr, std::vector<uint8_t>&& payload);
private:
    void handle_socks_handshake();
    void send_open_control(const std::string& host, uint16_t port);
    void pump_read_from_client();
    void send_data_block(const std::vector<uint8_t>& data);

    asio::io_context& io_;
    TransportPool& pool_;
    uint64_t session_id_;
    tcp::socket client_sock_;
    uint64_t next_block_id_{0};

    struct BlockState {
        uint16_t shard_count{0};
        uint16_t parity_count{0};
        uint32_t shard_size{0};
        std::vector<std::vector<uint8_t>> shards;
        std::vector<bool> present;
    };
    std::unordered_map<uint64_t, BlockState> down_blocks_;
};
} // namespace scatter
