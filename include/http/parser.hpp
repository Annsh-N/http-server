#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "http/request.hpp"

namespace http {

enum class ParseStatus {
    NeedMoreData,
    RequestReady,
    Error,
};

struct ParseResult {
    ParseStatus status;
    std::optional<HttpRequest> request;
    std::string error;
};

class HttpParser {
public:
    explicit HttpParser(std::size_t max_header_bytes = 16 * 1024,
                        std::size_t max_body_bytes = 1024 * 1024);

    void feed(std::string_view bytes);
    ParseResult next();
    [[nodiscard]] std::size_t buffered_bytes() const;

private:
    std::string buffer_;
    std::size_t max_header_bytes_;
    std::size_t max_body_bytes_;
};

} // namespace http

