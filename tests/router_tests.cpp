#include "http/router.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::filesystem::path make_root() {
    const auto root = std::filesystem::temp_directory_path() /
                      "systems_http_router_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& body) {
    std::ofstream file(path, std::ios::binary);
    file << body;
}

struct Fixture {
    std::filesystem::path root;
    http::StaticFileHandler file_handler;

    Fixture() : root(make_root()), file_handler(root) {
        write_file(root / "index.html", "<h1>router</h1>\n");
        write_file(root / "hello.txt", "hello\n");
    }

    ~Fixture() {
        std::filesystem::remove_all(root);
    }
};

void test_get_root_routes_to_static_file_handler() {
    Fixture fixture;

    const auto response =
        http::route_request(make_request("GET", "/"), fixture.file_handler);

    expect(response.status_code == 200, "GET / should return 200");
    expect(response.reason_phrase == "OK", "GET / should return OK");
    expect(response.body == "<h1>router</h1>\n",
           "GET / should return index.html body");
    expect_header(response, "Content-Type", "text/html",
                  "GET / should preserve file handler content type");
}

void test_head_root_routes_to_static_file_handler_without_body() {
    Fixture fixture;

    const auto response =
        http::route_request(make_request("HEAD", "/"), fixture.file_handler);

    expect(response.status_code == 200, "HEAD / should return 200");
    expect(response.reason_phrase == "OK", "HEAD / should return OK");
    expect(response.body.empty(), "HEAD response should not include a body");
    expect_header(response, "Content-Type", "text/html",
                  "HEAD / should preserve file handler content type");
}

void test_missing_file_routes_to_static_file_handler() {
    Fixture fixture;

    const auto response = http::route_request(make_request("GET", "/missing"),
                                              fixture.file_handler);

    expect(response.status_code == 404, "missing file should return 404");
    expect(response.reason_phrase == "Not Found",
           "missing file should return Not Found");
}

void test_post_returns_method_not_allowed() {
    Fixture fixture;

    const auto response =
        http::route_request(make_request("POST", "/"), fixture.file_handler);

    expect(response.status_code == 405, "POST should return 405");
    expect(response.reason_phrase == "Method Not Allowed",
           "POST should return Method Not Allowed");
    expect_header(response, "Allow", "GET, HEAD",
                  "405 response should include Allow header");
}

void test_delete_returns_method_not_allowed() {
    Fixture fixture;

    const auto response = http::route_request(make_request("DELETE", "/file"),
                                              fixture.file_handler);

    expect(response.status_code == 405, "DELETE should return 405");
    expect(response.reason_phrase == "Method Not Allowed",
           "DELETE should return Method Not Allowed");
    expect_header(response, "Allow", "GET, HEAD",
                  "405 response should include Allow header");
}

void test_method_policy_is_case_sensitive() {
    Fixture fixture;

    const auto response =
        http::route_request(make_request("get", "/"), fixture.file_handler);

    expect(response.status_code == 405,
           "lowercase method should not be treated as GET");
    expect_header(response, "Allow", "GET, HEAD",
                  "case-sensitive method rejection should include Allow header");
}

} // namespace

int main() {
    test_get_root_routes_to_static_file_handler();
    test_head_root_routes_to_static_file_handler_without_body();
    test_missing_file_routes_to_static_file_handler();
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
