#pragma once

#include "http/bounded_queue.hpp"
#include "http/connection.hpp"
#include "http/fd.hpp"
#include "http/static_file_handler.hpp"

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace http {

struct WorkerPoolStats {
    std::size_t dispatched_connections = 0;
    std::size_t rejected_dispatches = 0;
    std::size_t active_connections = 0;
    std::size_t queued_connections = 0;
    std::size_t queue_high_water = 0;
    std::size_t completed_connections = 0;
    std::size_t requests_served = 0;
    std::size_t bytes_read = 0;
    std::size_t bytes_written = 0;
    std::size_t peer_closes = 0;
    std::size_t explicit_closes = 0;
    std::size_t request_limit_closes = 0;
    std::size_t parse_errors = 0;
    std::size_t read_timeouts = 0;
    std::size_t keep_alive_timeouts = 0;
    std::size_t write_timeouts = 0;
    std::size_t read_errors = 0;
    std::size_t write_errors = 0;
    std::size_t worker_errors = 0;
};

class WorkerPool {
public:
    WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
               const StaticFileHandler& file_handler,
               ConnectionConfig config = {});
    WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
               const StaticFileHandler& file_handler,
               ConnectionTimeouts timeouts);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    bool dispatch(Fd client);
    void shutdown();

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] WorkerPoolStats stats() const;

private:
    void worker_loop();
    void record_result(const ConnectionResult& result);

    BoundedQueue<Fd> queue_;
    const StaticFileHandler& file_handler_;
    ConnectionConfig config_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> dispatched_connections_{0};
    std::atomic<std::size_t> rejected_dispatches_{0};
    std::atomic<std::size_t> active_connections_{0};
    std::atomic<std::size_t> completed_connections_{0};
    std::atomic<std::size_t> requests_served_{0};
    std::atomic<std::size_t> bytes_read_{0};
    std::atomic<std::size_t> bytes_written_{0};
    std::atomic<std::size_t> peer_closes_{0};
    std::atomic<std::size_t> explicit_closes_{0};
    std::atomic<std::size_t> request_limit_closes_{0};
    std::atomic<std::size_t> parse_errors_{0};
    std::atomic<std::size_t> read_timeouts_{0};
    std::atomic<std::size_t> keep_alive_timeouts_{0};
    std::atomic<std::size_t> write_timeouts_{0};
    std::atomic<std::size_t> read_errors_{0};
    std::atomic<std::size_t> write_errors_{0};
    std::atomic<std::size_t> worker_errors_{0};
};

} // namespace http
