#include "http/response.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string to_lower_ascii(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (unsigned char ch : input) {
        output.push_back(static_cast<char>(std::tolower(ch)));
    }

    return output;
}

bool is_content_length(std::string_view name) {
    return to_lower_ascii(name) == "content-length";
}

} // namespace

namespace http {

std::string serialize_response(const HttpResponse& response) {
    std::ostringstream out;

    out << response.version << ' ' << response.status_code << ' '
        << response.reason_phrase << "\r\n";

    for (const auto& [name, value] : response.headers) {
        if (is_content_length(name)) {
            continue;
        }

        out << name << ": " << value << "\r\n";
    }

    const std::size_t content_length =
        response.content_length.value_or(response.body.size());
    out << "Content-Length: " << content_length << "\r\n";
    out << "\r\n";
    if (!response.suppress_body) {
        out << response.body;
    }

    return out.str();
}

} // namespace http
