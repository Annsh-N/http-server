#include "http/server.hpp"

#include "http/fd.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

int failures = 0;

static_assert(!std::is_copy_constructible_v<http::HttpServer>);
static_assert(!std::is_move_constructible_v<http::HttpServer>);

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Fixture {
    std::filesystem::path root;

    Fixture()
        : root(std::filesystem::temp_directory_path() /
               "systems_http_server_e2e_tests") {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::ofstream(root / "index.html", std::ios::binary)
            << "<h1>end to end</h1>\n";
    }

    ~Fixture() {
        std::filesystem::remove_all(root);
    }
};

http::Fd connect_to(std::uint16_t port) {
    http::Fd client(::socket(AF_INET, SOCK_STREAM, 0));
    if (!client.valid()) {
        perror("socket");
        std::exit(1);
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        std::cerr << "inet_pton failed\n";
        std::exit(1);
    }

    if (connect(client.get(), reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0) {
        perror("connect");
        std::exit(1);
    }

    return client;
}

void write_all_or_die(int fd, const std::string& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n =
            write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0) {
            perror("write");
            std::exit(1);
        }
        offset += static_cast<std::size_t>(n);
    }
}

std::string read_until_eof(int fd) {
    std::string response;
    char buffer[1024];

    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            response.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return response;
        }

        perror("read");
        std::exit(1);
    }
}

void test_real_tcp_request_reaches_static_file_handler() {
    Fixture fixture;
    http::HttpServer server(fixture.root, 0);

    std::thread server_thread([&server] { server.serve_one(); });

    http::Fd client = connect_to(server.port());
    write_all_or_die(client.get(),
                     "GET / HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\n"
                     "Connection: close\r\n"
                     "\r\n");
    shutdown(client.get(), SHUT_WR);

    const std::string response = read_until_eof(client.get());
    server_thread.join();

    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "real TCP request should return 200");
    expect(response.find("Content-Type: text/html\r\n") != std::string::npos,
           "static handler should set the HTML content type");
    expect(response.find("\r\n\r\n<h1>end to end</h1>\n") !=
               std::string::npos,
           "response should contain the file body");

    const http::HttpServerStats stats = server.stats();
    expect(stats.accepted_connections == 1,
           "server stats should count accepted TCP connections");
    expect(stats.workers.completed_connections == 1,
           "server stats should expose completed worker connections");
}

void test_slow_client_does_not_block_another_worker() {
    Fixture fixture;
    const http::ConnectionTimeouts timeouts{2s, 1s};
    http::HttpServer server(fixture.root, 0, 128, "127.0.0.1", timeouts,
                            2, 2);

    http::Fd slow_client = connect_to(server.port());
    write_all_or_die(slow_client.get(),
                     "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    server.serve_one();

    http::Fd fast_client = connect_to(server.port());
    write_all_or_die(fast_client.get(),
                     "GET / HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\n"
                     "Connection: close\r\n"
                     "\r\n");
    shutdown(fast_client.get(), SHUT_WR);
    server.serve_one();

    const std::string response = read_until_eof(fast_client.get());
    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "second worker should serve while first worker waits on slow client");

    slow_client.reset();
}

} // namespace

int main() {
    test_real_tcp_request_reaches_static_file_handler();
    test_slow_client_does_not_block_another_worker();

    if (failures != 0) {
        std::cerr << failures << " server test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "server_tests passed\n";
    return 0;
}
