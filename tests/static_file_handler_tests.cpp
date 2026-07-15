#include "http/static_file_handler.hpp"

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

http::HttpRequest make_request(std::string method, std::string target) {
    http::HttpRequest request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.version = "HTTP/1.1";
    request.headers["host"] = "example.com";
    return request;
}

std::filesystem::path make_root() {
    const auto root =
        std::filesystem::temp_directory_path() /
        "systems_http_static_file_handler_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& body) {
    std::ofstream file(path, std::ios::binary);
    file << body;
}

struct Fixture {
    std::filesystem::path root;
    http::StaticFileHandler handler;

    Fixture() : root(make_root()), handler(root) {
        write_file(root / "index.html", "<h1>Hello</h1>\n");
        write_file(root / "hello.txt", "hello text\n");
        write_file(root / "assets" / "app.js", "console.log('ok');\n");
    }

    ~Fixture() {
        std::filesystem::remove_all(root);
    }
};

void test_get_existing_file_returns_body() {
    Fixture fixture;

    const auto response =
        fixture.handler.handle(make_request("GET", "/index.html"));

    expect(response.status_code == 200, "GET existing file should return 200");
    expect(response.reason_phrase == "OK",
           "GET existing file should return OK");
    expect(response.body == "<h1>Hello</h1>\n",
           "GET existing file should return file body");
    expect_header(response, "Content-Type", "text/html",
                  "html file should use text/html content type");
}

void test_get_root_maps_to_index() {
    Fixture fixture;

    const auto response = fixture.handler.handle(make_request("GET", "/"));

    expect(response.status_code == 200, "GET / should return 200");
    expect(response.body == "<h1>Hello</h1>\n",
           "GET / should serve index.html");
}

void test_head_existing_file_has_no_body() {
    Fixture fixture;

    const auto response =
        fixture.handler.handle(make_request("HEAD", "/index.html"));

    expect(response.status_code == 200, "HEAD existing file should return 200");
    expect(response.body.empty(), "HEAD response should not include body");
    expect_header(response, "Content-Type", "text/html",
                  "HEAD response should include content type");
}

void test_missing_file_returns_not_found() {
    Fixture fixture;

    const auto response =
        fixture.handler.handle(make_request("GET", "/missing.html"));

    expect(response.status_code == 404, "missing file should return 404");
    expect(response.reason_phrase == "Not Found",
           "missing file should return Not Found");
}

void test_plain_path_traversal_returns_forbidden() {
    Fixture fixture;

    const auto response =
        fixture.handler.handle(make_request("GET", "/../../etc/passwd"));

    expect(response.status_code == 403, "path traversal should return 403");
    expect(response.reason_phrase == "Forbidden",
           "path traversal should return Forbidden");
}

void test_encoded_path_traversal_returns_forbidden() {
    Fixture fixture;

    const auto response = fixture.handler.handle(
        make_request("GET", "/%2e%2e/%2e%2e/etc/passwd"));

    expect(response.status_code == 403,
           "encoded path traversal should return 403");
    expect(response.reason_phrase == "Forbidden",
           "encoded path traversal should return Forbidden");
}

void test_directory_request_returns_forbidden() {
    Fixture fixture;

    const auto response =
        fixture.handler.handle(make_request("GET", "/assets"));

    expect(response.status_code == 403,
           "directory request should return 403");
    expect(response.reason_phrase == "Forbidden",
           "directory request should return Forbidden");
}

void test_content_type_from_extension() {
    Fixture fixture;

    const auto text_response =
        fixture.handler.handle(make_request("GET", "/hello.txt"));
    expect_header(text_response, "Content-Type", "text/plain",
                  "txt file should use text/plain content type");

    const auto js_response =
        fixture.handler.handle(make_request("GET", "/assets/app.js"));
    expect_header(js_response, "Content-Type", "application/javascript",
                  "js file should use application/javascript content type");
}

} // namespace

int main() {
    test_get_existing_file_returns_body();
    test_get_root_maps_to_index();
    test_head_existing_file_has_no_body();
    test_missing_file_returns_not_found();
    test_plain_path_traversal_returns_forbidden();
    test_encoded_path_traversal_returns_forbidden();
    test_directory_request_returns_forbidden();
    test_content_type_from_extension();

    if (failures != 0) {
        std::cerr << failures << " static file handler test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "static_file_handler_tests passed\n";
    return 0;
}

