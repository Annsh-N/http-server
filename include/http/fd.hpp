#pragma once

namespace http {

class Fd {
public:
    explicit Fd(int fd = -1) noexcept;
    ~Fd();

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;

    [[nodiscard]] int get() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    int release() noexcept;
    void reset(int fd = -1) noexcept;

private:
    int fd_;
};

} // namespace http

