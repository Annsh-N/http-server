#include "http/connection.hpp"

#include "http/fd.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

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

} // namespace

int main() {
    test_get_request_returns_static_file_response();
    test_fragmented_request_is_reassembled();
    test_pipelined_keep_alive_requests();
    test_parse_error_returns_bad_request();
    test_connection_tokens_are_matched_exactly();
    test_close_token_stops_a_pipeline();

    if (failures != 0) {
        std::cerr << failures << " connection test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "connection_tests passed\n";
    return 0;
}
