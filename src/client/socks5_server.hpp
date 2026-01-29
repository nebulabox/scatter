
#pragma once
#include <asio.hpp>

namespace scatter {
class TransportPool;
class Socks5Server {
public:
    using tcp = asio::ip::tcp;
    Socks5Server(asio::io_context& io, const tcp::endpoint& ep, TransportPool& pool);
    void start();
private:
    void do_accept();
    asio::io_context& io_;
    tcp::acceptor acceptor_;
    TransportPool& pool_;
};
} // namespace scatter
