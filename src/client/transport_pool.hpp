
#pragma once
#include <asio.hpp>
#include <deque>
#include <unordered_map>
#include <memory>
#include "protocol.hpp"
#include "crypto.hpp"
#include "fec.hpp"

namespace scatter {

struct ClientConfig {
    std::string server_host; uint16_t server_port{};
    int pool_size{8}; int threads{4};
    uint32_t shard_size{1400}; uint16_t shard_count{8};
    uint16_t parity_count{1}; uint16_t copies_per_shard{2};
    std::vector<uint8_t> key;
};

class Session;

class TransportPool : public std::enable_shared_from_this<TransportPool> {
public:
    using tcp = asio::ip::tcp;
    TransportPool(asio::io_context& io, const ClientConfig& cfg);
    void start();
    void start_socks_session(tcp::socket sock);
    void send_frame(Frame&& f);
    CryptoProvider& crypto(){ return *crypto_; }
    ShardPlan shard_plan() const { return ShardPlan{cfg_.shard_count, cfg_.parity_count, cfg_.shard_size, cfg_.copies_per_shard}; }
private:
    struct Conn : public std::enable_shared_from_this<Conn> {
        tcp::socket sock;
        std::deque<std::vector<uint8_t>> write_q;
        std::vector<uint8_t> read_buf;
        std::vector<uint8_t> inbuf;
        Conn(asio::io_context& io) : sock(io), read_buf(64*1024) {}
    };

    asio::io_context& io_;
    ClientConfig cfg_;
    std::vector<std::shared_ptr<Conn>> conns_;
    std::atomic<size_t> rr_{0};
    std::unique_ptr<CryptoProvider> crypto_;
    std::unordered_map<uint64_t, std::shared_ptr<Session>> sessions_;

    void connect_one(std::shared_ptr<Conn> c);
    void do_read(std::shared_ptr<Conn> c);
    void do_write(std::shared_ptr<Conn> c);
    void parse_and_handle(std::shared_ptr<Conn> c, const uint8_t* data, size_t len);
};

} // namespace scatter
