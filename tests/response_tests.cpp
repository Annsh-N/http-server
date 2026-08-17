#include "http/response.hpp"

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

void expect_contains(const std::string& text, const std::string& needle,
                     const char* message) {
    expect(text.find(needle) != std::string::npos, message);
}

void test_basic_response_serialization() {
    http::HttpResponse response;
    response.status_code = 200;
    response.reason_phrase = "OK";
    response.headers["Content-Type"] = "text/plain";
    response.headers["Connection"] = "close";
    response.body = "hello";

    const std::string serialized = http::serialize_response(response);

    expect(serialized.starts_with("HTTP/1.1 200 OK\r\n"),
           "response should start with status line");
    expect_contains(serialized, "Content-Type: text/plain\r\n",
                    "response should serialize Content-Type header");
    expect_contains(serialized, "Connection: close\r\n",
                    "response should serialize Connection header");
    expect_contains(serialized, "\r\n\r\nhello",
                    "blank line should separate headers from body");
}

void test_content_length_is_generated_from_body_size() {
    http::HttpResponse response;
    response.body = "hello";

    const std::string serialized = http::serialize_response(response);

    expect_contains(serialized, "Content-Length: 5\r\n",
                    "Content-Length should match body size");
}

void test_empty_body_has_zero_content_length() {
    http::HttpResponse response;
    response.status_code = 204;
    response.reason_phrase = "No Content";

    const std::string serialized = http::serialize_response(response);

    expect(serialized.starts_with("HTTP/1.1 204 No Content\r\n"),
           "empty response should preserve status line");
    expect_contains(serialized, "Content-Length: 0\r\n",
                    "empty body should generate Content-Length: 0");
    expect(serialized.ends_with("\r\n\r\n"),
           "empty body response should end after header terminator");
}

void test_explicit_content_length_is_overwritten() {
    http::HttpResponse response;
    response.headers["Content-Length"] = "999";
    response.body = "hello";

    const std::string serialized = http::serialize_response(response);

    expect_contains(serialized, "Content-Length: 5\r\n",
                    "serializer should compute Content-Length itself");
    expect(serialized.find("Content-Length: 999\r\n") == std::string::npos,
           "serializer should not preserve stale Content-Length");
}

void test_connection_keep_alive_header() {
    http::HttpResponse response;
    response.headers["Connection"] = "keep-alive";
    response.body = "ok";

    const std::string serialized = http::serialize_response(response);

    expect_contains(serialized, "Connection: keep-alive\r\n",
                    "serializer should preserve keep-alive header");
}

void test_suppressed_body_preserves_representation_length() {
    http::HttpResponse response;
    response.content_length = 221;
    response.suppress_body = true;
    response.body = "this must not be transmitted";

    const std::string serialized = http::serialize_response(response);
    expect_contains(serialized, "Content-Length: 221\r\n",
                    "HEAD response should advertise representation length");
    expect(serialized.ends_with("\r\n\r\n"),
           "suppressed response should end after headers");
    expect(serialized.find("this must not be transmitted") ==
               std::string::npos,
           "suppressed response should omit body bytes");
}

} // namespace

int main() {
    test_basic_response_serialization();
    test_content_length_is_generated_from_body_size();
    test_empty_body_has_zero_content_length();
    test_explicit_content_length_is_overwritten();
    test_connection_keep_alive_header();
    test_suppressed_body_preserves_representation_length();

    if (failures != 0) {
        std::cerr << failures << " response test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "response_tests passed\n";
    return 0;
}
