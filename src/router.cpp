#include "http/router.hpp"

namespace {

http::HttpResponse ok_response() {
    http::HttpResponse response;
    response.status_code = 200;
    response.reason_phrase = "OK";
    response.headers["Content-Type"] = "text/plain";
    response.body = "ok\n";
    return response;
}

http::HttpResponse head_response() {
    http::HttpResponse response;
    response.status_code = 200;
    response.reason_phrase = "OK";
    response.headers["Content-Type"] = "text/plain";
    return response;
}

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

HttpResponse route_request(const HttpRequest& request) {
    if (request.method == "GET") {
        return ok_response();
    }

    if (request.method == "HEAD") {
        return head_response();
    }

    return method_not_allowed();
}

} // namespace http
