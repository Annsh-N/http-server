#include "http/server.hpp"

#include "http/connection.hpp"

#include <stdexcept>
#include <utility>

namespace http {

HttpServer::HttpServer(std::filesystem::path document_root, std::uint16_t port,
                       int backlog, std::string address,
                       ConnectionConfig connection_config,
                       std::size_t worker_count, std::size_t queue_capacity)
    : file_handler_(std::move(document_root)),
      listener_(TcpListener::bind_ipv4(port, backlog, address)),
      worker_pool_(worker_count, queue_capacity, file_handler_,
                   connection_config) {}

HttpServer::HttpServer(std::filesystem::path document_root, std::uint16_t port,
                       int backlog, std::string address,
                       ConnectionTimeouts timeouts, std::size_t worker_count,
                       std::size_t queue_capacity)
    : HttpServer(std::move(document_root), port, backlog, std::move(address),
                 ConnectionConfig{timeouts, {}}, worker_count,
                 queue_capacity) {}

std::uint16_t HttpServer::port() const noexcept {
    return listener_.port();
}

HttpServerStats HttpServer::stats() const {
    return HttpServerStats{
        accepted_connections_.load(std::memory_order_relaxed),
        worker_pool_.stats(),
    };
}

void HttpServer::serve_one() {
    Fd client = listener_.accept();
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
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
