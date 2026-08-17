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
                       ConnectionConfig config)
    : queue_(queue_capacity),
      file_handler_(file_handler),
      config_(config) {
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

WorkerPool::WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
                       const StaticFileHandler& file_handler,
                       ConnectionTimeouts timeouts)
    : WorkerPool(worker_count, queue_capacity, file_handler,
                 ConnectionConfig{timeouts, {}}) {}

WorkerPool::~WorkerPool() {
    shutdown();
}

bool WorkerPool::dispatch(Fd client) {
    if (!queue_.push(std::move(client))) {
        rejected_dispatches_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    dispatched_connections_.fetch_add(1, std::memory_order_relaxed);
    return true;
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

WorkerPoolStats WorkerPool::stats() const {
    WorkerPoolStats snapshot;
    snapshot.dispatched_connections =
        dispatched_connections_.load(std::memory_order_relaxed);
    snapshot.rejected_dispatches =
        rejected_dispatches_.load(std::memory_order_relaxed);
    snapshot.active_connections =
        active_connections_.load(std::memory_order_relaxed);
    snapshot.queued_connections = queue_.size();
    snapshot.queue_high_water = queue_.high_water_mark();
    snapshot.completed_connections =
        completed_connections_.load(std::memory_order_relaxed);
    snapshot.requests_served = requests_served_.load(std::memory_order_relaxed);
    snapshot.bytes_read = bytes_read_.load(std::memory_order_relaxed);
    snapshot.bytes_written = bytes_written_.load(std::memory_order_relaxed);
    snapshot.peer_closes = peer_closes_.load(std::memory_order_relaxed);
    snapshot.explicit_closes = explicit_closes_.load(std::memory_order_relaxed);
    snapshot.request_limit_closes =
        request_limit_closes_.load(std::memory_order_relaxed);
    snapshot.parse_errors = parse_errors_.load(std::memory_order_relaxed);
    snapshot.read_timeouts = read_timeouts_.load(std::memory_order_relaxed);
    snapshot.keep_alive_timeouts =
        keep_alive_timeouts_.load(std::memory_order_relaxed);
    snapshot.write_timeouts = write_timeouts_.load(std::memory_order_relaxed);
    snapshot.read_errors = read_errors_.load(std::memory_order_relaxed);
    snapshot.write_errors = write_errors_.load(std::memory_order_relaxed);
    snapshot.worker_errors = worker_errors_.load(std::memory_order_relaxed);
    return snapshot;
}

void WorkerPool::worker_loop() {
    while (std::optional<Fd> client = queue_.pop()) {
        active_connections_.fetch_add(1, std::memory_order_relaxed);
        try {
            Connection connection(std::move(*client), config_);
            record_result(connection.serve(file_handler_));
        } catch (const std::exception& error) {
            worker_errors_.fetch_add(1, std::memory_order_relaxed);
            log_connection_error(error.what());
        } catch (...) {
            worker_errors_.fetch_add(1, std::memory_order_relaxed);
            log_connection_error("unknown error");
        }
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void WorkerPool::record_result(const ConnectionResult& result) {
    completed_connections_.fetch_add(1, std::memory_order_relaxed);
    requests_served_.fetch_add(result.requests_served,
                               std::memory_order_relaxed);
    bytes_read_.fetch_add(result.bytes_read, std::memory_order_relaxed);
    bytes_written_.fetch_add(result.bytes_written, std::memory_order_relaxed);

    std::atomic<std::size_t>* counter = nullptr;
    switch (result.reason) {
    case ConnectionEndReason::PeerClosed:
        counter = &peer_closes_;
        break;
    case ConnectionEndReason::ConnectionClose:
        counter = &explicit_closes_;
        break;
    case ConnectionEndReason::RequestLimit:
        counter = &request_limit_closes_;
        break;
    case ConnectionEndReason::ParseError:
        counter = &parse_errors_;
        break;
    case ConnectionEndReason::ReadTimeout:
        counter = &read_timeouts_;
        break;
    case ConnectionEndReason::KeepAliveTimeout:
        counter = &keep_alive_timeouts_;
        break;
    case ConnectionEndReason::WriteTimeout:
        counter = &write_timeouts_;
        break;
    case ConnectionEndReason::ReadError:
        counter = &read_errors_;
        break;
    case ConnectionEndReason::WriteError:
        counter = &write_errors_;
        break;
    }
    counter->fetch_add(1, std::memory_order_relaxed);
}

} // namespace http
