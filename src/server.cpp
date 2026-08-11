#include "http/server.hpp"

#include "http/connection.hpp"

#include <stdexcept>
#include <utility>

namespace http {

HttpServer::HttpServer(std::filesystem::path document_root, std::uint16_t port,
                       int backlog, std::string address,
                       ConnectionTimeouts timeouts, std::size_t worker_count,
                       std::size_t queue_capacity)
    : file_handler_(std::move(document_root)),
      listener_(TcpListener::bind_ipv4(port, backlog, address)),
      worker_pool_(worker_count, queue_capacity, file_handler_, timeouts) {}

std::uint16_t HttpServer::port() const noexcept {
    return listener_.port();
}

void HttpServer::serve_one() {
    Fd client = listener_.accept();
    if (!worker_pool_.dispatch(std::move(client))) {
        throw std::runtime_error("worker pool is closed");
    }
}

[[noreturn]] void HttpServer::serve_forever() {
    while (true) {
        serve_one();
    }
}

} // namespace http
