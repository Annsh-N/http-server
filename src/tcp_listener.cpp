#include "http/tcp_listener.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace {

std::runtime_error syscall_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

sockaddr_in make_ipv4_address(const std::string& address, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + address);
    }

    return addr;
}

std::uint16_t bound_port(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        throw syscall_error("getsockname");
    }

    return ntohs(addr.sin_port);
}

} // namespace

namespace http {

TcpListener::TcpListener(Fd fd, std::uint16_t port) noexcept
    : fd_(std::move(fd)), port_(port) {}

TcpListener TcpListener::bind_ipv4(std::uint16_t port, int backlog,
                                   const std::string& address) {
    Fd listener(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener.valid()) {
        throw syscall_error("socket");
    }

    int reuse = 1;
    if (setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(reuse)) != 0) {
        throw syscall_error("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in addr = make_ipv4_address(address, port);
    if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) != 0) {
        throw syscall_error("bind");
    }

    if (listen(listener.get(), backlog) != 0) {
        throw syscall_error("listen");
    }

    const std::uint16_t assigned_port = bound_port(listener.get());
    return TcpListener(std::move(listener), assigned_port);
}

int TcpListener::fd() const noexcept {
    return fd_.get();
}

std::uint16_t TcpListener::port() const noexcept {
    return port_;
}

Fd TcpListener::accept() const {
    while (true) {
        const int client = ::accept(fd_.get(), nullptr, nullptr);
        if (client >= 0) {
            return Fd(client);
        }

        if (errno != EINTR) {
            throw syscall_error("accept");
        }
    }
}

} // namespace http
