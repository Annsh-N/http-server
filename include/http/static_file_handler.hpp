#pragma once

#include "http/request.hpp"
#include "http/response.hpp"

#include <filesystem>

namespace http {

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::filesystem::path root);

    HttpResponse handle(const HttpRequest& request) const;

private:
    std::filesystem::path root_;
};

} // namespace http

