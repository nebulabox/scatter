
#pragma once
#include <asio.hpp>
#include <unordered_map>
#include <deque>
#include <memory>
#include "protocol.hpp"
#include "crypto.hpp"

namespace scatter {

struct ServerConfig {
    std::string listen_host;
    uint16_t listen_port{};
    int threads{4};
    std::vector<uint8_t> key;
    uint32_t shard_size{1400};
    uint16_t shard_count{8};
    uint16_t parity_count{1};
    uint16_t copies_per_shard{2};
};

class RemoteConnector;

class ServerPool {
public:
    using tcp = asio::ip::tcp;

    // IMPORTANT: Conn must be visible for RemoteConnector header (macOS clang)
    struct Conn : public std::enable_shared_from_this<Conn> {
        tcp::socket sock;
        std::vector<uint8_t> read_buf;
        std::vector<uint8_t> inbuf;
        std::deque<std::vector<uint8_t>> write_q;
        Conn(asio::io_context& io) : sock(io), read_buf(64*1024) {}
    };

    ServerPool(asio::io_context& io, const ServerConfig& cfg);
    void start();

private:
    friend class RemoteConnector;

    asio::io_context& io_;
    ServerConfig cfg_;
    tcp::acceptor acceptor_;
    std::unique_ptr<CryptoProvider> crypto_;

    std::unordered_map<uint64_t, std::shared_ptr<RemoteConnector>> sessions_;

    void do_accept();
    void do_read(std::shared_ptr<Conn> c);
    void do_write(std::shared_ptr<Conn> c);
    void parse_and_handle(std::shared_ptr<Conn> c, const uint8_t* data, size_t n);
    void handle_frame(std::shared_ptr<Conn> c, const FrameHeader& hdr, std::vector<uint8_t>&& payload);
    void send_via(std::shared_ptr<Conn> c, Frame&& f);
};

} // namespace scatter
