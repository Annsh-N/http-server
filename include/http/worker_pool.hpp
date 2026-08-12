#pragma once

#include "http/bounded_queue.hpp"
#include "http/connection.hpp"
#include "http/fd.hpp"
#include "http/static_file_handler.hpp"

#include <cstddef>
#include <thread>
#include <vector>

namespace http {

class WorkerPool {
public:
    WorkerPool(std::size_t worker_count, std::size_t queue_capacity,
               const StaticFileHandler& file_handler,
               ConnectionTimeouts timeouts = {});
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    bool dispatch(Fd client);
    void shutdown();

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] std::size_t pending() const;

private:
    void worker_loop();

    BoundedQueue<Fd> queue_;
    const StaticFileHandler& file_handler_;
    ConnectionTimeouts timeouts_;
    std::vector<std::thread> workers_;
};

} // namespace http
