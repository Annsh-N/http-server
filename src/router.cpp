#include "http/router.hpp"

namespace {

http::HttpResponse method_not_allowed() {
    http::HttpResponse response;
    response.status_code = 405;
    response.reason_phrase = "Method Not Allowed";
    response.headers["Allow"] = "GET, HEAD";
    response.headers["Content-Type"] = "text/plain";
    response.body = "method not allowed\n";
    return response;
}

} // namespace

namespace http {

HttpResponse route_request(const HttpRequest& request,
                           const StaticFileHandler& file_handler) {
    if (request.method == "GET" || request.method == "HEAD") {
        HttpResponse response = file_handler.handle(request);
        if (request.method == "HEAD") {
            if (!response.content_length.has_value()) {
                response.content_length = response.body.size();
            }
            response.suppress_body = true;
        }
        return response;
    }

    return method_not_allowed();
}

} // namespace http
