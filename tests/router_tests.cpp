#include "http/router.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_header(const http::HttpResponse& response, const std::string& name,
                   const std::string& expected, const char* message) {
    const auto it = response.headers.find(name);
    if (it == response.headers.end()) {
        std::cerr << "FAIL: missing header " << name << '\n';
        ++failures;
        return;
    }

    expect(it->second == expected, message);
}

http::HttpRequest make_request(std::string method, std::string target = "/") {
    http::HttpRequest request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.version = "HTTP/1.1";
    request.headers["host"] = "example.com";
    return request;
}

void test_get_root_returns_ok_placeholder() {
    const auto response = http::route_request(make_request("GET", "/"));

    expect(response.status_code == 200, "GET / should return 200");
    expect(response.reason_phrase == "OK", "GET / should return OK");
    expect(!response.body.empty(),
           "GET placeholder response should include a body");
}

void test_head_root_returns_ok_without_body() {
    const auto response = http::route_request(make_request("HEAD", "/"));

    expect(response.status_code == 200, "HEAD / should return 200");
    expect(response.reason_phrase == "OK", "HEAD / should return OK");
    expect(response.body.empty(), "HEAD response should not include a body");
}

void test_post_returns_method_not_allowed() {
    const auto response = http::route_request(make_request("POST", "/"));

    expect(response.status_code == 405, "POST should return 405");
    expect(response.reason_phrase == "Method Not Allowed",
           "POST should return Method Not Allowed");
    expect_header(response, "Allow", "GET, HEAD",
                  "405 response should include Allow header");
}

void test_delete_returns_method_not_allowed() {
    const auto response = http::route_request(make_request("DELETE", "/file"));

    expect(response.status_code == 405, "DELETE should return 405");
    expect(response.reason_phrase == "Method Not Allowed",
           "DELETE should return Method Not Allowed");
    expect_header(response, "Allow", "GET, HEAD",
                  "405 response should include Allow header");
}

void test_method_policy_is_case_sensitive() {
    const auto response = http::route_request(make_request("get", "/"));

    expect(response.status_code == 405,
           "lowercase method should not be treated as GET");
    expect_header(response, "Allow", "GET, HEAD",
                  "case-sensitive method rejection should include Allow header");
}

} // namespace

int main() {
    test_get_root_returns_ok_placeholder();
    test_head_root_returns_ok_without_body();
    test_post_returns_method_not_allowed();
    test_delete_returns_method_not_allowed();
    test_method_policy_is_case_sensitive();

    if (failures != 0) {
        std::cerr << failures << " router test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "router_tests passed\n";
    return 0;
}
