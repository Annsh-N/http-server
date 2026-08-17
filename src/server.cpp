#include "http/server.hpp"

#include "http/connection.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace {

std::runtime_error syscall_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

std::array<http::Fd, 2> make_shutdown_pipe() {
    int descriptors[2];
    if (pipe(descriptors) != 0) {
        throw syscall_error("pipe");
    }

    std::array<http::Fd, 2> result{http::Fd(descriptors[0]),
                                   http::Fd(descriptors[1])};
    for (const http::Fd& descriptor : result) {
        if (fcntl(descriptor.get(), F_SETFD, FD_CLOEXEC) == -1) {
            throw syscall_error("fcntl(FD_CLOEXEC)");
        }
    }

    const int flags = fcntl(result[1].get(), F_GETFL);
    if (flags == -1 ||
        fcntl(result[1].get(), F_SETFL, flags | O_NONBLOCK) == -1) {
        throw syscall_error("fcntl(O_NONBLOCK)");
    }
    return result;
}

} // namespace

namespace http {

HttpServer::HttpServer(std::filesystem::path document_root, std::uint16_t port,
                       int backlog, std::string address,
                       ConnectionConfig connection_config,
                       std::size_t worker_count, std::size_t queue_capacity)
    : file_handler_(std::move(document_root)),
      listener_(TcpListener::bind_ipv4(port, backlog, address)),
      worker_pool_(worker_count, queue_capacity, file_handler_,
                   connection_config),
      shutdown_pipe_(make_shutdown_pipe()) {}

HttpServer::HttpServer(std::filesystem::path document_root, std::uint16_t port,
                       int backlog, std::string address,
                       ConnectionTimeouts timeouts, std::size_t worker_count,
                       std::size_t queue_capacity)
    : HttpServer(std::move(document_root), port, backlog, std::move(address),
                 ConnectionConfig{timeouts, {}}, worker_count,
                 queue_capacity) {}

HttpServer::~HttpServer() {
    request_shutdown();
    worker_pool_.shutdown();
}

std::uint16_t HttpServer::port() const noexcept {
    return listener_.port();
}

HttpServerStats HttpServer::stats() const {
    return HttpServerStats{
        accepted_connections_.load(std::memory_order_relaxed),
        worker_pool_.stats(),
    };
}

int HttpServer::shutdown_notification_fd() const noexcept {
    return shutdown_pipe_[1].get();
}

bool HttpServer::shutdown_requested() const noexcept {
    return shutdown_requested_.load(std::memory_order_acquire);
}

void HttpServer::serve_one() {
    Fd client = listener_.accept();
    accepted_connections_.fetch_add(1, std::memory_order_relaxed);
    if (!worker_pool_.dispatch(std::move(client))) {
        if (!shutdown_requested()) {
            throw std::runtime_error("worker pool is closed");
        }
    }
}

void HttpServer::serve_forever() {
    pollfd descriptors[2] = {
        {listener_.fd(), POLLIN, 0},
        {shutdown_pipe_[0].get(), POLLIN, 0},
    };

    while (!shutdown_requested()) {
        const int result = poll(descriptors, 2, -1);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw syscall_error("poll");
        }

        if ((descriptors[1].revents & POLLIN) != 0) {
            request_shutdown();
            break;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            serve_one();
        }
    }

    worker_pool_.shutdown();
}

void HttpServer::request_shutdown() {
    if (shutdown_requested_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    worker_pool_.request_shutdown();
    constexpr char notification = 1;
    const ssize_t ignored =
        write(shutdown_pipe_[1].get(), &notification, sizeof(notification));
    (void)ignored;
}

} // namespace http
