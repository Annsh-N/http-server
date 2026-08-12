#include "http/worker_pool.hpp"

#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

std::mutex error_output_mutex;

void log_connection_error(std::string_view message) {
    std::lock_guard lock(error_output_mutex);
    std::cerr << "connection error: " << message << '\n';
}

} // namespace

namespace http {

WorkerPool::WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
                       const StaticFileHandler& file_handler,
                       ConnectionTimeouts timeouts)
    : queue_(queue_capacity),
      file_handler_(file_handler),
      timeouts_(timeouts) {
    if (worker_count == 0) {
        throw std::invalid_argument("worker count must be greater than zero");
    }

    workers_.reserve(worker_count);
    try {
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        queue_.close();
        for (std::thread& worker : workers_) {
            worker.join();
        }
        throw;
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

bool WorkerPool::dispatch(Fd client) {
    return queue_.push(std::move(client));
}

void WorkerPool::shutdown() {
    queue_.close();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t WorkerPool::worker_count() const noexcept {
    return workers_.size();
}

std::size_t WorkerPool::pending() const {
    return queue_.size();
}

void WorkerPool::worker_loop() {
    while (std::optional<Fd> client = queue_.pop()) {
        try {
            Connection connection(std::move(*client), timeouts_);
            connection.serve(file_handler_);
        } catch (const std::exception& error) {
            log_connection_error(error.what());
        } catch (...) {
            log_connection_error("unknown error");
        }
    }
}

} // namespace http
