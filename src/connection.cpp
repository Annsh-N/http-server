#include "http/connection.hpp"

#include "http/parser.hpp"
#include "http/response.hpp"
#include "http/router.hpp"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

std::string_view trim_ows(std::string_view input) {
    while (!input.empty() && (input.front() == ' ' || input.front() == '\t')) {
        input.remove_prefix(1);
    }

    while (!input.empty() && (input.back() == ' ' || input.back() == '\t')) {
        input.remove_suffix(1);
    }

    return input;
}

bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto left_char = static_cast<unsigned char>(left[i]);
        const auto right_char = static_cast<unsigned char>(right[i]);
        if (std::tolower(left_char) != std::tolower(right_char)) {
            return false;
        }
    }

    return true;
}

bool has_connection_token(const http::HttpRequest& request,
                          std::string_view token) {
    const auto it = request.headers.find("connection");
    if (it == request.headers.end()) {
        return false;
    }

    std::string_view remaining = it->second;
    while (true) {
        const std::size_t comma = remaining.find(',');
        const std::string_view candidate = trim_ows(remaining.substr(0, comma));
        if (ascii_iequals(candidate, token)) {
            return true;
        }

        if (comma == std::string_view::npos) {
            return false;
        }

        remaining.remove_prefix(comma + 1);
    }
}

bool should_keep_alive(const http::HttpRequest& request) {
    if (has_connection_token(request, "close")) {
        return false;
    }

    if (request.version == "HTTP/1.1") {
        return true;
    }

    return request.version == "HTTP/1.0" &&
           has_connection_token(request, "keep-alive");
}

http::HttpResponse bad_request_response() {
    http::HttpResponse response;
    response.status_code = 400;
    response.reason_phrase = "Bad Request";
    response.headers["Content-Type"] = "text/plain";
    response.body = "bad request\n";
    return response;
}

ssize_t send_without_sigpipe(int fd, const char* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    return send(fd, data, size, MSG_NOSIGNAL);
#else
    return send(fd, data, size, 0);
#endif
}

} // namespace

namespace http {

Connection::Connection(Fd fd, ConnectionTimeouts timeouts) noexcept
    : fd_(std::move(fd)),
      state_(fd_.valid() ? State::Reading : State::Closed),
      timeouts_(timeouts),
      read_deadline_(std::chrono::steady_clock::now() +
                     timeouts_.read_timeout) {
#ifdef SO_NOSIGPIPE
    if (fd_.valid()) {
        const int enabled = 1;
        (void)setsockopt(fd_.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                         sizeof(enabled));
    }
#endif
}

void Connection::serve(const StaticFileHandler& file_handler) {
    while (state_ != State::Closed) {
        switch (state_) {
        case State::Reading:
            read_from_socket();
            break;
        case State::Parsing:
            parse_request(file_handler);
            break;
        case State::Writing:
            write_to_socket();
            break;
        case State::Closed:
            break;
        }
    }
}

void Connection::read_from_socket() {
    if (!arm_receive_timeout()) {
        state_ = State::Closed;
        return;
    }

    char buffer[4096];
    const ssize_t n = read(fd_.get(), buffer, sizeof(buffer));

    if (n > 0) {
        if (read_phase_ == ReadPhase::KeepAlive) {
            begin_request_timeout();
        }
        parser_.feed(std::string_view(buffer, static_cast<std::size_t>(n)));
        state_ = State::Parsing;
        return;
    }

    if (n < 0 && errno == EINTR) {
        return;
    }

    state_ = State::Closed;
}

void Connection::parse_request(const StaticFileHandler& file_handler) {
    if (read_phase_ == ReadPhase::KeepAlive && parser_.buffered_bytes() > 0) {
        begin_request_timeout();
    }

    ParseResult result = parser_.next();

    if (result.status == ParseStatus::NeedMoreData) {
        state_ = State::Reading;
        return;
    }

    if (result.status == ParseStatus::Error || !result.request.has_value()) {
        queue_response(bad_request_response(), false);
        return;
    }

    const HttpRequest& request = *result.request;
    const bool keep_alive = should_keep_alive(request);
    HttpResponse response = route_request(request, file_handler);
    response.version = request.version;
    queue_response(std::move(response), keep_alive);
}

void Connection::write_to_socket() {
    const char* data = pending_response_.data() + write_offset_;
    const std::size_t remaining = pending_response_.size() - write_offset_;
    const ssize_t n = send_without_sigpipe(fd_.get(), data, remaining);

    if (n > 0) {
        write_offset_ += static_cast<std::size_t>(n);
        if (write_offset_ == pending_response_.size()) {
            pending_response_.clear();
            write_offset_ = 0;
            if (close_after_write_) {
                state_ = State::Closed;
            } else {
                begin_keep_alive_timeout();
                state_ = State::Parsing;
            }
        }
        return;
    }

    if (n < 0 && errno == EINTR) {
        return;
    }

    state_ = State::Closed;
}

void Connection::queue_response(HttpResponse response, bool keep_alive) {
    response.headers["Connection"] = keep_alive ? "keep-alive" : "close";
    pending_response_ = serialize_response(response);
    write_offset_ = 0;
    close_after_write_ = !keep_alive;
    state_ = State::Writing;
}

void Connection::begin_request_timeout() {
    read_phase_ = ReadPhase::Request;
    read_deadline_ =
        std::chrono::steady_clock::now() + timeouts_.read_timeout;
}

void Connection::begin_keep_alive_timeout() {
    read_phase_ = ReadPhase::KeepAlive;
    read_deadline_ =
        std::chrono::steady_clock::now() + timeouts_.keep_alive_timeout;
}

bool Connection::arm_receive_timeout() {
    using namespace std::chrono;

    const auto remaining = read_deadline_ - steady_clock::now();
    if (remaining <= steady_clock::duration::zero()) {
        return false;
    }

    auto timeout = duration_cast<microseconds>(remaining);
    if (timeout <= microseconds::zero()) {
        timeout = microseconds(1);
    }

    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(
        timeout.count() / microseconds::period::den);
    value.tv_usec = static_cast<decltype(value.tv_usec)>(
        timeout.count() % microseconds::period::den);

    return setsockopt(fd_.get(), SOL_SOCKET, SO_RCVTIMEO, &value,
                      sizeof(value)) == 0;
}

} // namespace http
