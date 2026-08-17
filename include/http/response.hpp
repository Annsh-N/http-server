#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace http {

struct HttpResponse {
    std::string version = "HTTP/1.1";
    int status_code = 200;
    std::string reason_phrase = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::optional<std::size_t> content_length;
    bool suppress_body = false;
};

std::string serialize_response(const HttpResponse& response);

} // namespace http
