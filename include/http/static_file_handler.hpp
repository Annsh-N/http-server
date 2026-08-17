#pragma once

#include "http/request.hpp"
#include "http/response.hpp"

#include <cstddef>
#include <filesystem>

namespace http {

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::filesystem::path root,
                               std::size_t max_file_bytes = 16 * 1024 * 1024);

    HttpResponse handle(const HttpRequest& request) const;

private:
    std::filesystem::path root_;
    std::size_t max_file_bytes_;
};

} // namespace http
