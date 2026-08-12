#include "http/fd.hpp"

#include <unistd.h>

#include <utility>

namespace http {

Fd::Fd(int fd) noexcept : fd_(fd) {}

Fd::~Fd() {
    reset();
}

Fd::Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = std::exchange(other.fd_, -1);
    }

    return *this;
}

int Fd::get() const noexcept {
    return fd_;
}

bool Fd::valid() const noexcept {
    return fd_ >= 0;
}

int Fd::release() noexcept {
    return std::exchange(fd_, -1);
}

void Fd::reset(int fd) noexcept {
    if (fd_ >= 0 && fd_ != fd) {
        close(fd_);
    }

    fd_ = fd;
}

} // namespace http

