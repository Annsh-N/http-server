#pragma once

#include "http/connection.hpp"
#include "http/static_file_handler.hpp"
#include "http/tcp_listener.hpp"
#include "http/worker_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace http {

class HttpServer {
public:
    explicit HttpServer(std::filesystem::path document_root,
                        std::uint16_t port = 8080, int backlog = 128,
                        std::string address = "127.0.0.1",
                        ConnectionTimeouts timeouts = {},
                        std::size_t worker_count = 4,
                        std::size_t queue_capacity = 128);

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;

    void serve_one();
    [[noreturn]] void serve_forever();

private:
    StaticFileHandler file_handler_;
    TcpListener listener_;
    WorkerPool worker_pool_;
};

} // namespace http
