
#include "socks5_server.hpp"
#include "logging.hpp"
#include "transport_pool.hpp"

namespace scatter {
Socks5Server::Socks5Server(asio::io_context &io, const tcp::endpoint &ep,
                           TransportPool &pool)
    : io_(io), acceptor_(io, ep), pool_(pool) {}

void Socks5Server::start() { do_accept(); }

void Socks5Server::do_accept() {
  acceptor_.async_accept([this](std::error_code ec, tcp::socket sock) {
    if (!ec)
      pool_.start_socks_session(std::move(sock));
    else
      Logger::instance().log(LogLevel::ERROR, "accept failed: %s",
                             ec.message().c_str());
    do_accept();
  });
}
} // namespace scatter
