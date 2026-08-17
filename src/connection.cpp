#include "http/connection.hpp"

#include "http/parser.hpp"
#include "http/response.hpp"
#include "http/router.hpp"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <stdexcept>
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

http::HttpResponse not_implemented_response() {
    http::HttpResponse response;
    response.status_code = 501;
    response.reason_phrase = "Not Implemented";
    response.headers["Content-Type"] = "text/plain";
    response.body = "transfer encoding is not supported\n";
    return response;
}

http::HttpResponse request_timeout_response() {
    http::HttpResponse response;
    response.status_code = 408;
    response.reason_phrase = "Request Timeout";
    response.headers["Content-Type"] = "text/plain";
    response.body = "request timeout\n";
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

Connection::Connection(Fd fd, ConnectionConfig config)
    : fd_(std::move(fd)),
      state_(fd_.valid() ? State::Reading : State::Closed),
      config_(config),
      read_deadline_(std::chrono::steady_clock::now() +
                     config_.timeouts.read_timeout),
      write_deadline_(std::chrono::steady_clock::now()) {
    if (config_.timeouts.read_timeout <= std::chrono::milliseconds::zero() ||
        config_.timeouts.keep_alive_timeout <=
            std::chrono::milliseconds::zero() ||
        config_.timeouts.write_timeout <= std::chrono::milliseconds::zero() ||
        config_.limits.max_requests == 0) {
        throw std::invalid_argument(
            "connection timeouts and request limit must be positive");
    }

    if (!fd_.valid()) {
        result_.reason = ConnectionEndReason::ReadError;
    }

#ifdef SO_NOSIGPIPE
    if (fd_.valid()) {
        const int enabled = 1;
        (void)setsockopt(fd_.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                         sizeof(enabled));
    }
#endif
}

Connection::Connection(Fd fd, ConnectionTimeouts timeouts)
    : Connection(std::move(fd), ConnectionConfig{timeouts, {}}) {}

ConnectionResult Connection::serve(const StaticFileHandler& file_handler) {
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

    return result_;
}

void Connection::read_from_socket() {
    if (!arm_socket_timeout(SO_RCVTIMEO, read_deadline_)) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            finish(ConnectionEndReason::ReadError);
            return;
        }
        if (read_phase_ == ReadPhase::KeepAlive) {
            finish(ConnectionEndReason::KeepAliveTimeout);
        } else {
            queue_response(request_timeout_response(), false,
                           ConnectionEndReason::ReadTimeout);
        }
        return;
    }

    char buffer[4096];
    const ssize_t n = read(fd_.get(), buffer, sizeof(buffer));

    if (n > 0) {
        result_.bytes_read += static_cast<std::size_t>(n);
        if (read_phase_ == ReadPhase::KeepAlive) {
            begin_request_timeout();
        }
        parser_.feed(std::string_view(buffer, static_cast<std::size_t>(n)));
        state_ = State::Parsing;
        return;
    }

    if (n < 0) {
        if (errno == EINTR) {
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (read_phase_ == ReadPhase::KeepAlive) {
                finish(ConnectionEndReason::KeepAliveTimeout);
            } else {
                queue_response(request_timeout_response(), false,
                               ConnectionEndReason::ReadTimeout);
            }
            return;
        }

        finish(ConnectionEndReason::ReadError);
        return;
    }

    finish(ConnectionEndReason::PeerClosed);
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
        HttpResponse response =
            result.error_kind == ParseErrorKind::UnsupportedTransferEncoding
                ? not_implemented_response()
                : bad_request_response();
        queue_response(std::move(response), false,
                       ConnectionEndReason::ParseError);
        return;
    }

    const HttpRequest& request = *result.request;
    ++result_.requests_served;
    const bool request_limit_reached =
        result_.requests_served >= config_.limits.max_requests;
    const bool keep_alive =
        should_keep_alive(request) && !request_limit_reached;
    HttpResponse response = route_request(request, file_handler);
    response.version = request.version;
    queue_response(std::move(response), keep_alive,
                   request_limit_reached
                       ? ConnectionEndReason::RequestLimit
                       : ConnectionEndReason::ConnectionClose);
}

void Connection::write_to_socket() {
    if (!arm_socket_timeout(SO_SNDTIMEO, write_deadline_)) {
        finish(errno == EAGAIN || errno == EWOULDBLOCK
                   ? ConnectionEndReason::WriteTimeout
                   : ConnectionEndReason::WriteError);
        return;
    }

    const char* data = pending_response_.data() + write_offset_;
    const std::size_t remaining = pending_response_.size() - write_offset_;
    const ssize_t n = send_without_sigpipe(fd_.get(), data, remaining);

    if (n > 0) {
        write_offset_ += static_cast<std::size_t>(n);
        result_.bytes_written += static_cast<std::size_t>(n);
        if (write_offset_ == pending_response_.size()) {
            pending_response_.clear();
            write_offset_ = 0;
            if (close_after_write_) {
                finish(close_reason_after_write_);
            } else {
                begin_keep_alive_timeout();
                state_ = State::Parsing;
            }
        }
        return;
    }

    if (n < 0) {
        if (errno == EINTR) {
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            finish(ConnectionEndReason::WriteTimeout);
            return;
        }
    }

    finish(ConnectionEndReason::WriteError);
}

void Connection::queue_response(HttpResponse response, bool keep_alive,
                                ConnectionEndReason close_reason) {
    response.headers["Connection"] = keep_alive ? "keep-alive" : "close";
    pending_response_ = serialize_response(response);
    write_offset_ = 0;
    close_after_write_ = !keep_alive;
    close_reason_after_write_ = close_reason;
    write_deadline_ = std::chrono::steady_clock::now() +
                      config_.timeouts.write_timeout;
    state_ = State::Writing;
}

void Connection::begin_request_timeout() {
    read_phase_ = ReadPhase::Request;
    read_deadline_ =
        std::chrono::steady_clock::now() + config_.timeouts.read_timeout;
}

void Connection::begin_keep_alive_timeout() {
    read_phase_ = ReadPhase::KeepAlive;
    read_deadline_ =
        std::chrono::steady_clock::now() +
        config_.timeouts.keep_alive_timeout;
}

bool Connection::arm_socket_timeout(
    int option, std::chrono::steady_clock::time_point deadline) {
    using namespace std::chrono;

    const auto remaining = deadline - steady_clock::now();
    if (remaining <= steady_clock::duration::zero()) {
        errno = EAGAIN;
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

    return setsockopt(fd_.get(), SOL_SOCKET, option, &value, sizeof(value)) ==
           0;
}

void Connection::finish(ConnectionEndReason reason) {
    result_.reason = reason;
    state_ = State::Closed;
}

} // namespace http
