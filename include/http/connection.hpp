#pragma once

#include "http/fd.hpp"
#include "http/parser.hpp"
#include "http/static_file_handler.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace http {

struct HttpResponse;

struct ConnectionTimeouts {
    std::chrono::milliseconds read_timeout{10000};
    std::chrono::milliseconds keep_alive_timeout{5000};
    std::chrono::milliseconds write_timeout{10000};
};

struct ConnectionLimits {
    std::size_t max_requests{100};
};

struct ConnectionConfig {
    ConnectionTimeouts timeouts;
    ConnectionLimits limits;
};

enum class ConnectionEndReason {
    PeerClosed,
    ConnectionClose,
    RequestLimit,
    ParseError,
    ReadTimeout,
    KeepAliveTimeout,
    WriteTimeout,
    ReadError,
    WriteError,
};

struct ConnectionResult {
    ConnectionEndReason reason = ConnectionEndReason::PeerClosed;
    std::size_t requests_served = 0;
    std::size_t bytes_read = 0;
    std::size_t bytes_written = 0;
};

class Connection {
public:
    explicit Connection(Fd fd, ConnectionConfig config = {});
    Connection(Fd fd, ConnectionTimeouts timeouts);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    ConnectionResult serve(const StaticFileHandler& file_handler);

private:
    enum class State {
        Reading,
        Parsing,
        Writing,
        Closed,
    };

    enum class ReadPhase {
        Request,
        KeepAlive,
    };

    void read_from_socket();
    void parse_request(const StaticFileHandler& file_handler);
    void write_to_socket();
    void queue_response(HttpResponse response, bool keep_alive,
                        ConnectionEndReason close_reason =
                            ConnectionEndReason::ConnectionClose);
    void begin_request_timeout();
    void begin_keep_alive_timeout();
    bool arm_socket_timeout(int option,
                            std::chrono::steady_clock::time_point deadline);
    void finish(ConnectionEndReason reason);

    Fd fd_;
    HttpParser parser_;
    State state_;
    std::string pending_response_;
    std::size_t write_offset_ = 0;
    bool close_after_write_ = false;
    ConnectionEndReason close_reason_after_write_ =
        ConnectionEndReason::ConnectionClose;
    ConnectionConfig config_;
    ReadPhase read_phase_ = ReadPhase::Request;
    std::chrono::steady_clock::time_point read_deadline_;
    std::chrono::steady_clock::time_point write_deadline_;
    ConnectionResult result_;
};

} // namespace http
