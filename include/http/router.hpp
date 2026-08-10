#pragma once

#include "http/request.hpp"
#include "http/response.hpp"
#include "http/static_file_handler.hpp"

namespace http {

HttpResponse route_request(const HttpRequest& request,
                           const StaticFileHandler& file_handler);

} // namespace http
