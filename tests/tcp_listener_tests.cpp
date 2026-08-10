#include "http/tcp_listener.hpp"

#include "http/fd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

http::Fd connect_to(std::uint16_t port) {
    http::Fd client(::socket(AF_INET, SOCK_STREAM, 0));
    if (!client.valid()) {
        perror("socket");
        std::exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed\n";
        std::exit(1);
    }

    if (connect(client.get(), reinterpret_cast<const sockaddr*>(&addr),
                sizeof(addr)) != 0) {
        perror("connect");
        std::exit(1);
    }

    return client;
}

void test_bind_ephemeral_port() {
    const auto listener = http::TcpListener::bind_ipv4(0);

    expect(listener.fd() >= 0, "listener fd should be valid");
    expect(listener.port() != 0, "ephemeral bind should report assigned port");
}

void test_accept_loopback_connection() {
    const auto listener = http::TcpListener::bind_ipv4(0);
    auto client = connect_to(listener.port());
    auto accepted = listener.accept();

    expect(client.valid(), "client fd should be valid");
    expect(accepted.valid(), "accepted fd should be valid");

    constexpr char message[] = "ping";
    if (write(client.get(), message, sizeof(message) - 1) !=
        static_cast<ssize_t>(sizeof(message) - 1)) {
        perror("write");
        std::exit(1);
    }

    char buffer[sizeof(message)]{};
    const ssize_t n = read(accepted.get(), buffer, sizeof(message) - 1);
    if (n < 0) {
        perror("read");
        std::exit(1);
    }

    expect(std::string(buffer, static_cast<std::size_t>(n)) == "ping",
           "accepted socket should receive client bytes");
}

void test_invalid_address_throws() {
    bool threw = false;
    try {
        (void)http::TcpListener::bind_ipv4(0, 128, "not-an-ip");
    } catch (const std::exception&) {
        threw = true;
    }

    expect(threw, "invalid bind address should throw");
}

static_assert(!std::is_copy_constructible_v<http::TcpListener>);
static_assert(!std::is_copy_assignable_v<http::TcpListener>);
static_assert(std::is_move_constructible_v<http::TcpListener>);
static_assert(std::is_move_assignable_v<http::TcpListener>);

} // namespace

int main() {
    test_bind_ephemeral_port();
    test_accept_loopback_connection();
    test_invalid_address_throws();

    if (failures != 0) {
        std::cerr << failures << " tcp listener test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "tcp_listener_tests passed\n";
    return 0;
}

