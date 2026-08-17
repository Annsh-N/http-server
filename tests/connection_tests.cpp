#include "http/connection.hpp"

#include "http/fd.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write_file(const std::filesystem::path& path, const std::string& body) {
    std::ofstream file(path, std::ios::binary);
    file << body;
}

std::filesystem::path make_root() {
    const auto root =
        std::filesystem::temp_directory_path() / "systems_http_connection_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write_file(root / "index.html", "<h1>connection</h1>\n");
    write_file(root / "hello.txt", "hello\n");
    write_file(root / "large.bin", std::string(2 * 1024 * 1024, 'x'));
    return root;
}

struct SocketPair {
    http::Fd client;
    http::Fd server;
};

SocketPair make_socket_pair() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        std::exit(1);
    }

    return SocketPair{http::Fd(fds[0]), http::Fd(fds[1])};
}

void write_all_or_die(int fd, const std::string& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n =
            write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            perror("write");
            std::exit(1);
        }
        written += static_cast<std::size_t>(n);
    }
}

std::string read_until_eof(int fd) {
    std::string out;
    char buffer[1024];

    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            out.append(buffer, static_cast<std::size_t>(n));
            continue;
        }

        if (n == 0) {
            return out;
        }

        perror("read");
        std::exit(1);
    }
}

std::string read_until_contains(int fd, const std::string& expected) {
    std::string out;
    char buffer[1024];

    while (out.find(expected) == std::string::npos) {
        pollfd descriptor{fd, POLLIN, 0};
        if (poll(&descriptor, 1, 2000) <= 0) {
            return out;
        }

        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            return out;
        }
        out.append(buffer, static_cast<std::size_t>(n));
    }

    return out;
}

bool wait_for_peer_close(int fd, int timeout_ms = 2000) {
    pollfd descriptor{fd, static_cast<short>(POLLIN | POLLHUP), 0};
    if (poll(&descriptor, 1, timeout_ms) <= 0) {
        return false;
    }

    char byte;
    return read(fd, &byte, 1) == 0;
}

std::size_t count_occurrences(const std::string& input,
                              const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = input.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

struct Fixture {
    std::filesystem::path root;
    http::StaticFileHandler file_handler;

    Fixture() : root(make_root()), file_handler(root) {}

    ~Fixture() {
        std::filesystem::remove_all(root);
    }
};

std::string serve_request(const std::string& request_bytes) {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();

    std::thread server([server_fd = std::move(sockets.server), &fixture]() mutable {
        http::Connection connection(std::move(server_fd));
        connection.serve(fixture.file_handler);
    });

    write_all_or_die(sockets.client.get(), request_bytes);
    shutdown(sockets.client.get(), SHUT_WR);

    const std::string response = read_until_eof(sockets.client.get());
    server.join();
    return response;
}

void test_get_request_returns_static_file_response() {
    const std::string response =
        serve_request("GET / HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n");

    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "GET response should start with 200 status line");
    expect(response.find("Content-Type: text/html\r\n") != std::string::npos,
           "GET response should include content type");
    expect(response.find("Connection: close\r\n") != std::string::npos,
           "Connection: close request should close response");
    expect(response.find("\r\n\r\n<h1>connection</h1>\n") != std::string::npos,
           "GET response should include file body");
}

void test_head_advertises_get_length_without_body() {
    const std::string response =
        serve_request("HEAD / HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n");

    const std::size_t representation_length =
        std::string("<h1>connection</h1>\n").size();
    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "HEAD should return the corresponding GET status");
    expect(response.find("Content-Length: " +
                         std::to_string(representation_length) + "\r\n") !=
               std::string::npos,
           "HEAD should advertise the corresponding GET body length");
    expect(response.ends_with("\r\n\r\n"),
           "HEAD should transmit no bytes after response headers");
}

void test_head_error_omits_body() {
    const std::string response =
        serve_request("HEAD /missing.txt HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n");

    expect(response.find("HTTP/1.1 404 Not Found\r\n") == 0,
           "HEAD missing file should return 404");
    expect(response.find("Content-Length: 10\r\n") != std::string::npos,
           "HEAD 404 should advertise GET error-body length");
    expect(response.ends_with("\r\n\r\n"),
           "HEAD 404 should omit error body bytes");
}

void test_fragmented_request_is_reassembled() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();

    std::thread server([server_fd = std::move(sockets.server), &fixture]() mutable {
        http::Connection connection(std::move(server_fd));
        connection.serve(fixture.file_handler);
    });

    write_all_or_die(sockets.client.get(), "GET /hel");
    write_all_or_die(sockets.client.get(),
                     "lo.txt HTTP/1.1\r\nHost: example.com\r\n"
                     "Connection: close\r\n\r\n");
    shutdown(sockets.client.get(), SHUT_WR);

    const std::string response = read_until_eof(sockets.client.get());
    server.join();

    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "fragmented request should return 200");
    expect(response.find("\r\n\r\nhello\n") != std::string::npos,
           "fragmented request should serve full target");
}

void test_pipelined_keep_alive_requests() {
    const std::string response =
        serve_request("GET /hello.txt HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: keep-alive\r\n"
                      "\r\n"
                      "GET /missing.txt HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n");

    expect(response.find("HTTP/1.1 200 OK\r\n") != std::string::npos,
           "pipeline should include first 200 response");
    expect(response.find("Connection: keep-alive\r\n") != std::string::npos,
           "first response should keep connection alive");
    expect(response.find("HTTP/1.1 404 Not Found\r\n") != std::string::npos,
           "pipeline should include second 404 response");
    expect(response.find("Connection: close\r\n") != std::string::npos,
           "second response should close connection");
}

void test_parse_error_returns_bad_request() {
    const std::string response =
        serve_request("GET / HTTP/1.1\r\n"
                      "\r\n");

    expect(response.find("HTTP/1.1 400 Bad Request\r\n") == 0,
           "parse error should return 400");
    expect(response.find("Connection: close\r\n") != std::string::npos,
           "parse error should close connection");
}

void test_unsupported_transfer_encoding_returns_not_implemented() {
    const std::string response =
        serve_request("POST / HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "0\r\n\r\n");

    expect(response.find("HTTP/1.1 501 Not Implemented\r\n") == 0,
           "unsupported transfer coding should return 501");
    expect(response.find("Connection: close\r\n") != std::string::npos,
           "unsupported request framing should close connection");
}

void test_ambiguous_framing_returns_bad_request() {
    const std::string response =
        serve_request("POST / HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "0\r\n\r\n");

    expect(response.find("HTTP/1.1 400 Bad Request\r\n") == 0,
           "ambiguous request framing should return 400");
    expect(count_occurrences(response, "HTTP/1.1 ") == 1,
           "framing error bytes must never parse as a second request");
}

void test_connection_tokens_are_matched_exactly() {
    const std::string response =
        serve_request("GET /hello.txt HTTP/1.0\r\n"
                      "Connection: keep-alive-extra\r\n"
                      "\r\n");

    expect(response.find("HTTP/1.0 200 OK\r\n") == 0,
           "response version should match HTTP/1.0 request");
    expect(response.find("Connection: close\r\n") != std::string::npos,
           "partial token match must not enable HTTP/1.0 keep-alive");
}

void test_close_token_stops_a_pipeline() {
    const std::string response =
        serve_request("GET /hello.txt HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: keep-alive, CLOSE\r\n"
                      "\r\n"
                      "GET /missing.txt HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Connection: close\r\n"
                      "\r\n");

    expect(response.find("Connection: close\r\n") != std::string::npos,
           "close token should override keep-alive token");
    expect(count_occurrences(response, "HTTP/1.1 ") == 1,
           "server should not process requests after a close response");
}

void test_incomplete_request_hits_absolute_read_timeout() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    const http::ConnectionTimeouts timeouts{150ms, 1s};

    std::promise<http::ConnectionResult> result_promise;
    auto result = result_promise.get_future();
    std::thread server([server_fd = std::move(sockets.server), &fixture,
                        timeouts, &result_promise]() mutable {
        http::Connection connection(std::move(server_fd), timeouts);
        result_promise.set_value(connection.serve(fixture.file_handler));
    });

    write_all_or_die(sockets.client.get(),
                     "GET / HTTP/1.1\r\nHost: example.com\r\n");

    const std::string response = read_until_eof(sockets.client.get());
    expect(response.find("HTTP/1.1 408 Request Timeout\r\n") == 0,
           "incomplete active request should return 408 at deadline");
    server.join();
    expect(result.get().reason == http::ConnectionEndReason::ReadTimeout,
           "connection result should distinguish request read timeout");
}

void test_request_fragments_do_not_extend_read_deadline() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    const http::ConnectionTimeouts timeouts{400ms, 1s};

    std::promise<http::ConnectionResult> result_promise;
    auto result = result_promise.get_future();
    std::thread server([server_fd = std::move(sockets.server), &fixture,
                        timeouts, &result_promise]() mutable {
        http::Connection connection(std::move(server_fd), timeouts);
        result_promise.set_value(connection.serve(fixture.file_handler));
    });

    write_all_or_die(sockets.client.get(), "GET / HTTP/1.1\r\nHost:");
    std::this_thread::sleep_for(200ms);
    write_all_or_die(sockets.client.get(), " example.com");
    std::this_thread::sleep_for(250ms);

    const std::string response = read_until_eof(sockets.client.get());
    expect(response.find("HTTP/1.1 408 Request Timeout\r\n") == 0,
           "new fragments must not reset the absolute read deadline");
    server.join();
    expect(result.get().reason == http::ConnectionEndReason::ReadTimeout,
           "slow-drip request should end as read timeout");
}

void test_idle_keep_alive_connection_times_out() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    const http::ConnectionTimeouts timeouts{1s, 150ms};

    std::thread server([server_fd = std::move(sockets.server), &fixture,
                        timeouts]() mutable {
        http::Connection connection(std::move(server_fd), timeouts);
        connection.serve(fixture.file_handler);
    });

    write_all_or_die(sockets.client.get(),
                     "GET /hello.txt HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "\r\n");

    const std::string response =
        read_until_contains(sockets.client.get(), "\r\n\r\nhello\n");
    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "keep-alive request should receive its response");
    expect(response.find("Connection: keep-alive\r\n") != std::string::npos,
           "response should initially preserve keep-alive");
    expect(wait_for_peer_close(sockets.client.get()),
           "idle keep-alive connection should close at its deadline");
    server.join();
}

void test_request_limit_closes_pipeline() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    const http::ConnectionConfig config{{1s, 1s, 1s}, {2}};
    std::promise<http::ConnectionResult> result_promise;
    auto result = result_promise.get_future();

    std::thread server([server_fd = std::move(sockets.server), &fixture, config,
                        &result_promise]() mutable {
        http::Connection connection(std::move(server_fd), config);
        result_promise.set_value(connection.serve(fixture.file_handler));
    });

    write_all_or_die(sockets.client.get(),
                     "GET /hello.txt HTTP/1.1\r\nHost: example.com\r\n\r\n"
                     "GET /hello.txt HTTP/1.1\r\nHost: example.com\r\n\r\n"
                     "GET /missing.txt HTTP/1.1\r\nHost: example.com\r\n\r\n");
    shutdown(sockets.client.get(), SHUT_WR);
    const std::string response = read_until_eof(sockets.client.get());
    server.join();

    expect(count_occurrences(response, "HTTP/1.1 200 OK") == 2,
           "connection should serve exactly the configured request limit");
    expect(response.find("HTTP/1.1 404 Not Found") == std::string::npos,
           "requests after connection limit should remain unprocessed");
    expect(result.get().reason == http::ConnectionEndReason::RequestLimit,
           "connection result should report request limit closure");
}

void test_write_timeout_stops_nonreading_client() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    int send_buffer = 4096;
    if (setsockopt(sockets.server.get(), SOL_SOCKET, SO_SNDBUF, &send_buffer,
                   sizeof(send_buffer)) != 0) {
        perror("setsockopt(SO_SNDBUF)");
        std::exit(1);
    }

    const http::ConnectionConfig config{{1s, 1s, 150ms}, {10}};
    std::promise<http::ConnectionResult> result_promise;
    auto result = result_promise.get_future();
    std::thread server([server_fd = std::move(sockets.server), &fixture, config,
                        &result_promise]() mutable {
        http::Connection connection(std::move(server_fd), config);
        result_promise.set_value(connection.serve(fixture.file_handler));
    });

    write_all_or_die(sockets.client.get(),
                     "GET /large.bin HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "Connection: close\r\n\r\n");

    expect(result.wait_for(2s) == std::future_status::ready,
           "nonreading client should not hold worker beyond write deadline");
    if (result.wait_for(0ms) == std::future_status::ready) {
        expect(result.get().reason == http::ConnectionEndReason::WriteTimeout,
               "blocked response should end as write timeout");
    }
    sockets.client.reset();
    server.join();
}

} // namespace

int main() {
    test_get_request_returns_static_file_response();
    test_head_advertises_get_length_without_body();
    test_head_error_omits_body();
    test_fragmented_request_is_reassembled();
    test_pipelined_keep_alive_requests();
    test_parse_error_returns_bad_request();
    test_unsupported_transfer_encoding_returns_not_implemented();
    test_ambiguous_framing_returns_bad_request();
    test_connection_tokens_are_matched_exactly();
    test_close_token_stops_a_pipeline();
    test_incomplete_request_hits_absolute_read_timeout();
    test_request_fragments_do_not_extend_read_deadline();
    test_idle_keep_alive_connection_times_out();
    test_request_limit_closes_pipeline();
    test_write_timeout_stops_nonreading_client();

    if (failures != 0) {
        std::cerr << failures << " connection test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "connection_tests passed\n";
    return 0;
}
