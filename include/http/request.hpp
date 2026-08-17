#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace http {

struct HeaderField {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::vector<HeaderField> header_fields;
    std::string body;
};

} // namespace http
