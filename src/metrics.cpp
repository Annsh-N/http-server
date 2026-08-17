#include "http/metrics.hpp"

#include <sstream>

namespace http {

std::string format_shutdown_metrics(const HttpServerStats& stats,
                                    std::chrono::milliseconds uptime) {
    const WorkerPoolStats& workers = stats.workers;
    std::ostringstream output;
    output << "{\"event\":\"server_shutdown\""
           << ",\"uptime_ms\":" << uptime.count()
           << ",\"accepted_connections\":" << stats.accepted_connections
           << ",\"dispatched_connections\":"
           << workers.dispatched_connections
           << ",\"rejected_dispatches\":" << workers.rejected_dispatches
           << ",\"active_connections\":" << workers.active_connections
           << ",\"queued_connections\":" << workers.queued_connections
           << ",\"queue_high_water\":" << workers.queue_high_water
           << ",\"completed_connections\":"
           << workers.completed_connections
           << ",\"requests_served\":" << workers.requests_served
           << ",\"bytes_read\":" << workers.bytes_read
           << ",\"bytes_written\":" << workers.bytes_written
           << ",\"peer_closes\":" << workers.peer_closes
           << ",\"explicit_closes\":" << workers.explicit_closes
           << ",\"request_limit_closes\":"
           << workers.request_limit_closes
           << ",\"parse_errors\":" << workers.parse_errors
           << ",\"read_timeouts\":" << workers.read_timeouts
           << ",\"keep_alive_timeouts\":"
           << workers.keep_alive_timeouts
           << ",\"write_timeouts\":" << workers.write_timeouts
           << ",\"read_errors\":" << workers.read_errors
           << ",\"write_errors\":" << workers.write_errors
           << ",\"worker_errors\":" << workers.worker_errors << '}';
    return output.str();
}

} // namespace http
