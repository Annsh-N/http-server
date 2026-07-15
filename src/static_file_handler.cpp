#include "http/static_file_handler.hpp"

#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::string extract_path(std::string_view target) {
    const auto query = target.find('?');
    if (query == std::string_view::npos) {
        return std::string(target);
    }

    return std::string(target.substr(0, query));
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }

    return -1;
}

std::optional<std::string> percent_decode(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') {
            output.push_back(input[i]);
            continue;
        }

        if (i + 2 >= input.size()) {
            return std::nullopt;
        }

        const int high = hex_value(input[i + 1]);
        const int low = hex_value(input[i + 2]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }

        const char decoded = static_cast<char>((high << 4) | low);
        if (decoded == '\0') {
            return std::nullopt;
        }

        output.push_back(decoded);
        i += 2;
    }

    return output;
}

std::filesystem::path make_relative_path(std::string_view decoded_path) {
    while (!decoded_path.empty() && decoded_path.front() == '/') {
        decoded_path.remove_prefix(1);
    }

    return std::filesystem::path(std::string(decoded_path));
}

bool is_inside_root(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();

    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end()) {
            return false;
        }

        if (*root_it != *candidate_it) {
            return false;
        }
    }

    return true;
}

http::HttpResponse make_response(int status_code, std::string reason,
                                 std::string body) {
    http::HttpResponse response;
    response.status_code = status_code;
    response.reason_phrase = std::move(reason);
    response.headers["Content-Type"] = "text/plain";
    response.body = std::move(body);
    return response;
}

http::HttpResponse bad_request() {
    return make_response(400, "Bad Request", "bad request\n");
}

http::HttpResponse forbidden() {
    return make_response(403, "Forbidden", "forbidden\n");
}

http::HttpResponse not_found() {
    return make_response(404, "Not Found", "not found\n");
}

std::string content_type_for(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();

    if (extension == ".html" || extension == ".htm") {
        return "text/html";
    }
    if (extension == ".txt") {
        return "text/plain";
    }
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".js") {
        return "application/javascript";
    }

    return "application/octet-stream";
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream body;
    body << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return std::nullopt;
    }

    return body.str();
}

http::HttpResponse serve_file(const std::filesystem::path& path, bool is_head) {
    http::HttpResponse response;
    response.status_code = 200;
    response.reason_phrase = "OK";
    response.headers["Content-Type"] = content_type_for(path);

    if (!is_head) {
        auto body = read_file(path);
        if (!body.has_value()) {
            return forbidden();
        }

        response.body = std::move(*body);
    }

    return response;
}

} // namespace

namespace http {

StaticFileHandler::StaticFileHandler(std::filesystem::path root)
    : root_(std::filesystem::canonical(std::move(root))) {}

HttpResponse StaticFileHandler::handle(const HttpRequest& request) const {
    std::string path = extract_path(request.target);
    if (path == "/") {
        path = "/index.html";
    }

    const auto decoded = percent_decode(path);
    if (!decoded.has_value()) {
        return bad_request();
    }

    if (decoded->empty() || decoded->front() != '/') {
        return bad_request();
    }

    std::error_code ec;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root_ / make_relative_path(*decoded),
                                          ec);
    if (ec) {
        return forbidden();
    }

    if (!is_inside_root(root_, candidate)) {
        return forbidden();
    }

    if (!std::filesystem::exists(candidate, ec)) {
        if (ec) {
            return forbidden();
        }
        return not_found();
    }

    if (!std::filesystem::is_regular_file(candidate, ec)) {
        return forbidden();
    }

    return serve_file(candidate, request.method == "HEAD");
}

} // namespace http
