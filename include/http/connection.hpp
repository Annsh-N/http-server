#pragma once

#include "http/fd.hpp"
#include "http/parser.hpp"
#include "http/static_file_handler.hpp"

#include <cstddef>
#include <string>

namespace http {

struct HttpResponse;

class Connection {
public:
    explicit Connection(Fd fd) noexcept;

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

    void read_from_socket();
    void parse_request(const StaticFileHandler& file_handler);
    void write_to_socket();
    void queue_response(HttpResponse response, bool keep_alive);

    Fd fd_;
    HttpParser parser_;
    State state_;
    std::string pending_response_;
    std::size_t write_offset_ = 0;
    bool close_after_write_ = false;
};

} // namespace http
