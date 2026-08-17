#include "http/parser.hpp"

#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct RequestLine {
    std::string method;
    std::string target;
    std::string version;
};

std::string to_lower_ascii(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (unsigned char ch : input) {
        output.push_back(static_cast<char>(std::tolower(ch)));
    }

    return output;
}

std::string_view trim_ows(std::string_view input) {
    std::size_t begin = 0;
    while (begin < input.size() &&
           (input[begin] == ' ' || input[begin] == '\t')) {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin && (input[end - 1] == ' ' || input[end - 1] == '\t')) {
        --end;
    }

    return input.substr(begin, end - begin);
}

bool is_tchar(unsigned char ch) {
    if (std::isalnum(ch)) {
        return true;
    }

    switch (ch) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

bool is_token(std::string_view input) {
    if (input.empty()) {
        return false;
    }

    for (unsigned char ch : input) {
        if (!is_tchar(ch)) {
            return false;
        }
    }

    return true;
}

bool parse_request_line(std::string_view line, RequestLine& out,
                        std::string& error) {
    const auto first_space = line.find(' ');
    if (first_space == std::string_view::npos || first_space == 0) {
        error = "request line is missing method separator";
        return false;
    }

    const auto second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos ||
        second_space == first_space + 1) {
        error = "request line is missing target or version";
        return false;
    }

    const auto extra_space = line.find(' ', second_space + 1);
    if (extra_space != std::string_view::npos) {
        error = "request line has too many fields";
        return false;
    }

    out.method = std::string(line.substr(0, first_space));
    out.target = std::string(
        line.substr(first_space + 1, second_space - first_space - 1));
    out.version = std::string(line.substr(second_space + 1));

    if (!is_token(out.method)) {
        error = "invalid method token";
        return false;
    }

    if (out.target.empty() || out.target.front() != '/') {
        error = "request target must use origin-form beginning with slash";
        return false;
    }

    if (out.version != "HTTP/1.1" && out.version != "HTTP/1.0") {
        error = "unsupported HTTP version";
        return false;
    }

    return true;
}

bool parse_header_line(std::string_view line, std::string& name,
                       std::string& value, std::string& error) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        error = "malformed header line";
        return false;
    }

    if (line[colon - 1] == ' ' || line[colon - 1] == '\t') {
        error = "whitespace before header colon";
        return false;
    }

    const std::string_view raw_name = line.substr(0, colon);
    if (!is_token(raw_name)) {
        error = "invalid header name";
        return false;
    }

    name = to_lower_ascii(raw_name);
    value = std::string(trim_ows(line.substr(colon + 1)));

    return true;
}

bool parse_content_length(std::string_view value, std::size_t& length,
                          std::string& error) {
    value = trim_ows(value);
    if (value.empty()) {
        error = "empty Content-Length";
        return false;
    }

    std::size_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end) {
        error = "invalid Content-Length";
        return false;
    }

    length = parsed;
    return true;
}

http::ParseResult parse_error(
    std::string error,
    http::ParseErrorKind kind = http::ParseErrorKind::BadRequest) {
    return http::ParseResult{http::ParseStatus::Error, std::nullopt,
                             std::move(error), kind};
}

} // namespace

namespace http {

HttpParser::HttpParser(std::size_t max_header_bytes,
                       std::size_t max_body_bytes)
    : max_header_bytes_(max_header_bytes),
      max_body_bytes_(max_body_bytes) {}

void HttpParser::feed(std::string_view bytes) {
    buffer_.append(bytes);
}

ParseResult HttpParser::next() {
    const auto header_end = buffer_.find("\r\n\r\n");

    if (header_end == std::string::npos) {
        if (buffer_.size() > max_header_bytes_) {
            return parse_error("headers exceed configured parser limit");
        }

        return ParseResult{ParseStatus::NeedMoreData, std::nullopt, ""};
    }

    if (header_end > max_header_bytes_) {
        return parse_error("headers exceed configured parser limit");
    }

    const std::string_view header_block(buffer_.data(), header_end);
    const auto request_line_end = header_block.find("\r\n");
    const std::string_view request_line =
        request_line_end == std::string_view::npos
            ? header_block
            : header_block.substr(0, request_line_end);

    RequestLine parsed_line;
    std::string error;
    if (!parse_request_line(request_line, parsed_line, error)) {
        return parse_error(std::move(error));
    }

    HttpRequest request;
    request.method = std::move(parsed_line.method);
    request.target = std::move(parsed_line.target);
    request.version = std::move(parsed_line.version);

    if (request_line_end != std::string_view::npos) {
        std::size_t line_start = request_line_end + 2;
        while (line_start < header_block.size()) {
            const auto line_end = header_block.find("\r\n", line_start);
            const std::string_view line =
                line_end == std::string_view::npos
                    ? header_block.substr(line_start)
                    : header_block.substr(line_start, line_end - line_start);

            std::string name;
            std::string value;
            if (!parse_header_line(line, name, value, error)) {
                return parse_error(std::move(error));
            }

            const auto existing = request.headers.find(name);
            if (existing != request.headers.end()) {
                if (name == "connection") {
                    existing->second.append(", ");
                    existing->second.append(value);
                    request.header_fields.push_back(
                        HeaderField{std::move(name), std::move(value)});
                    if (line_end == std::string_view::npos) {
                        break;
                    }
                    line_start = line_end + 2;
                    continue;
                }

                return parse_error("duplicate header field: " + name);
            }

            request.header_fields.push_back(HeaderField{name, value});
            request.headers.emplace(std::move(name), std::move(value));

            if (line_end == std::string_view::npos) {
                break;
            }
            line_start = line_end + 2;
        }
    }

    if (request.version == "HTTP/1.1") {
        const auto host = request.headers.find("host");
        if (host == request.headers.end() || trim_ows(host->second).empty()) {
            return parse_error("HTTP/1.1 request requires Host header");
        }
    }

    const auto transfer_encoding = request.headers.find("transfer-encoding");
    const auto content_length_header = request.headers.find("content-length");
    if (transfer_encoding != request.headers.end() &&
        content_length_header != request.headers.end()) {
        return parse_error(
            "request contains both Transfer-Encoding and Content-Length");
    }

    if (transfer_encoding != request.headers.end()) {
        return parse_error("Transfer-Encoding is not supported",
                           ParseErrorKind::UnsupportedTransferEncoding);
    }

    std::size_t content_length = 0;
    if (content_length_header != request.headers.end()) {
        if (!parse_content_length(content_length_header->second, content_length,
                                  error)) {
            return parse_error(std::move(error));
        }

        if (content_length > max_body_bytes_) {
            return parse_error("body exceeds configured parser limit");
        }
    }

    const std::size_t body_start = header_end + 4;
    if (buffer_.size() - body_start < content_length) {
        return ParseResult{ParseStatus::NeedMoreData, std::nullopt, ""};
    }

    request.body = buffer_.substr(body_start, content_length);

    const std::size_t consumed = body_start + content_length;
    buffer_.erase(0, consumed);

    return ParseResult{ParseStatus::RequestReady, std::move(request), ""};
}

std::size_t HttpParser::buffered_bytes() const {
    return buffer_.size();
}

} // namespace http
