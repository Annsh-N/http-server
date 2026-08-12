#pragma once

#include "http/fd.hpp"

#include <cstdint>
#include <string>

namespace http {

class TcpListener {
public:
    static TcpListener bind_ipv4(std::uint16_t port, int backlog = 128,
                                 const std::string& address = "127.0.0.1");

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    TcpListener(TcpListener&&) noexcept = default;
    TcpListener& operator=(TcpListener&&) noexcept = default;

    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;

    Fd accept() const;

private:
    TcpListener(Fd fd, std::uint16_t port) noexcept;

    Fd fd_;
    std::uint16_t port_;
};

} // namespace http

