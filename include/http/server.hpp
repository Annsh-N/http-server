#pragma once

#include "http/connection.hpp"
#include "http/static_file_handler.hpp"
#include "http/tcp_listener.hpp"
#include "http/worker_pool.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace http {

struct HttpServerStats {
    std::size_t accepted_connections = 0;
    WorkerPoolStats workers;
};

class HttpServer {
public:
    explicit HttpServer(std::filesystem::path document_root,
                        std::uint16_t port = 8080, int backlog = 128,
                        std::string address = "127.0.0.1",
                        ConnectionConfig connection_config = {},
                        std::size_t worker_count = 4,
                        std::size_t queue_capacity = 128);
    HttpServer(std::filesystem::path document_root, std::uint16_t port,
               int backlog, std::string address,
               ConnectionTimeouts timeouts, std::size_t worker_count,
               std::size_t queue_capacity);

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;
    ~HttpServer();

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] HttpServerStats stats() const;
    [[nodiscard]] int shutdown_notification_fd() const noexcept;
    [[nodiscard]] bool shutdown_requested() const noexcept;

    void serve_one();
    void serve_forever();
    void request_shutdown();

private:
    StaticFileHandler file_handler_;
    TcpListener listener_;
    WorkerPool worker_pool_;
    std::array<Fd, 2> shutdown_pipe_;
    std::atomic<std::size_t> accepted_connections_{0};
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace http
