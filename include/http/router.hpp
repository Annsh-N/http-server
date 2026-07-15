#pragma once

#include "http/request.hpp"
#include "http/response.hpp"

namespace http {

HttpResponse route_request(const HttpRequest& request);

} // namespace http

