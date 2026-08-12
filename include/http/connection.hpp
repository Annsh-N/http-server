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
};

class Connection {
public:
    explicit Connection(Fd fd,
                        ConnectionTimeouts timeouts = {}) noexcept;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    void serve(const StaticFileHandler& file_handler);

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
    void queue_response(HttpResponse response, bool keep_alive);
    void begin_request_timeout();
    void begin_keep_alive_timeout();
    bool arm_receive_timeout();

    Fd fd_;
    HttpParser parser_;
    State state_;
    std::string pending_response_;
    std::size_t write_offset_ = 0;
    bool close_after_write_ = false;
    ConnectionTimeouts timeouts_;
    ReadPhase read_phase_ = ReadPhase::Request;
    std::chrono::steady_clock::time_point read_deadline_;
};

} // namespace http
