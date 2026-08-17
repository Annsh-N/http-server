#include "http/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_empty_parser_needs_more_data() {
    http::HttpParser parser;
    auto result = parser.next();
    expect(result.status == http::ParseStatus::NeedMoreData,
           "empty parser should need more data");
}

void test_complete_get_request() {
    http::HttpParser parser;

    parser.feed("GET /index.html HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Connection: keep-alive\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::RequestReady,
           "complete GET should produce one request");
    expect(result.request.has_value(),
           "complete GET result should contain request");

    if (!result.request.has_value()) {
        return;
    }

    const auto& request = *result.request;
    expect(request.method == "GET", "method should be GET");
    expect(request.target == "/index.html", "target should be /index.html");
    expect(request.version == "HTTP/1.1", "version should be HTTP/1.1");
    expect(request.headers.at("host") == "example.com",
           "Host header should be parsed case-insensitively");
    expect(request.headers.at("connection") == "keep-alive",
           "Connection header should be parsed");
    expect(request.body.empty(), "GET request body should be empty");
    expect(parser.buffered_bytes() == 0,
           "complete single request should leave no buffered bytes");
}

void test_fragmented_request() {
    http::HttpParser parser;

    parser.feed("GET /ind");
    auto first = parser.next();
    expect(first.status == http::ParseStatus::NeedMoreData,
           "fragmented request should need more data before CRLFCRLF");

    parser.feed("ex.html HTTP/1.1\r\nHost: example.com\r\n\r\n");
    auto second = parser.next();
    expect(second.status == http::ParseStatus::RequestReady,
           "fragmented request should parse after remaining bytes arrive");

    if (second.request.has_value()) {
        expect(second.request->target == "/index.html",
               "fragmented request target should be reconstructed");
    }
}

void test_pipelined_requests() {
    http::HttpParser parser;

    parser.feed("GET /a HTTP/1.1\r\nHost: example.com\r\n\r\n"
                "GET /b HTTP/1.1\r\nHost: example.com\r\n\r\n");

    auto first = parser.next();
    expect(first.status == http::ParseStatus::RequestReady,
           "first pipelined request should be ready");
    if (first.request.has_value()) {
        expect(first.request->target == "/a",
               "first pipelined request should target /a");
    }

    auto second = parser.next();
    expect(second.status == http::ParseStatus::RequestReady,
           "second pipelined request should be ready");
    if (second.request.has_value()) {
        expect(second.request->target == "/b",
               "second pipelined request should target /b");
    }

    auto third = parser.next();
    expect(third.status == http::ParseStatus::NeedMoreData,
           "parser should need more data after consuming pipeline");
}

void test_incomplete_body_needs_more_data() {
    http::HttpParser parser;

    parser.feed("POST /upload HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "he");

    auto first = parser.next();
    expect(first.status == http::ParseStatus::NeedMoreData,
           "parser should wait until Content-Length bytes are buffered");

    parser.feed("llo");
    auto second = parser.next();
    expect(second.status == http::ParseStatus::RequestReady,
           "parser should emit request after complete body arrives");
    if (second.request.has_value()) {
        expect(second.request->body == "hello",
               "request body should contain exactly Content-Length bytes");
    }
}

void test_body_uses_exact_content_length() {
    http::HttpParser parser;

    parser.feed("POST /upload HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "helloGET /next HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "\r\n");

    auto first = parser.next();
    expect(first.status == http::ParseStatus::RequestReady,
           "complete body request should be ready");
    if (first.request.has_value()) {
        expect(first.request->target == "/upload",
               "body request target should be /upload");
        expect(first.request->body == "hello",
               "body should contain exactly five bytes");
    }

    auto second = parser.next();
    expect(second.status == http::ParseStatus::RequestReady,
           "bytes after body should remain available as next request");
    if (second.request.has_value()) {
        expect(second.request->target == "/next",
               "extra bytes after body should parse as /next request");
        expect(second.request->body.empty(),
               "next GET request should not inherit previous body bytes");
    }
}

void test_invalid_content_length_errors() {
    http::HttpParser parser;

    parser.feed("POST /upload HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Content-Length: five\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "non-numeric Content-Length should be a parse error");
    expect(!result.error.empty(),
           "invalid Content-Length error should include diagnostic text");
}

void test_duplicate_content_length_errors() {
    http::HttpParser parser;

    parser.feed("POST /upload HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Content-Length: 5\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "hello");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "duplicate Content-Length should be rejected");
    expect(!result.error.empty(),
           "duplicate Content-Length error should include diagnostic text");
}

void test_duplicate_host_errors() {
    http::HttpParser parser;
    parser.feed("GET / HTTP/1.1\r\n"
                "Host: first.example\r\n"
                "Host: second.example\r\n"
                "\r\n");

    const auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "duplicate Host should be rejected");
}

void test_unknown_duplicate_header_is_conservatively_rejected() {
    http::HttpParser parser;
    parser.feed("GET / HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "X-Example: one\r\n"
                "X-Example: two\r\n"
                "\r\n");

    const auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "unsupported duplicate-header semantics should be rejected");
}

void test_duplicate_connection_fields_are_combined() {
    http::HttpParser parser;
    parser.feed("GET / HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Connection: keep-alive\r\n"
                "Connection: close\r\n"
                "\r\n");

    const auto result = parser.next();
    expect(result.status == http::ParseStatus::RequestReady,
           "duplicate Connection list fields should parse");
    if (result.request.has_value()) {
        expect(result.request->headers.at("connection") ==
                   "keep-alive, close",
               "Connection values should be combined in arrival order");
        expect(result.request->header_fields.size() == 3,
               "raw header fields should preserve repeated entries");
    }
}

void test_transfer_encoding_and_content_length_errors() {
    http::HttpParser parser;
    parser.feed("POST / HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "0\r\n\r\n");

    const auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "ambiguous body framing should be rejected");
    expect(result.error_kind == http::ParseErrorKind::BadRequest,
           "ambiguous framing should be classified as bad request");
}

void test_unsupported_transfer_encoding_is_classified() {
    http::HttpParser parser;
    parser.feed("POST / HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "0\r\n\r\n");

    const auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "unsupported transfer coding should be rejected");
    expect(result.error_kind ==
               http::ParseErrorKind::UnsupportedTransferEncoding,
           "unsupported transfer coding should retain error classification");
}

void test_body_limit_errors() {
    http::HttpParser parser(16 * 1024, 4);

    parser.feed("POST /upload HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "hello");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "body larger than configured limit should be rejected");
    expect(!result.error.empty(),
           "body limit error should include diagnostic text");
}

void test_http11_requires_host_header() {
    http::HttpParser parser;

    parser.feed("GET / HTTP/1.1\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "HTTP/1.1 request without Host should be rejected");
    expect(!result.error.empty(),
           "missing Host error should include diagnostic text");
}

void test_http10_allows_missing_host_header() {
    http::HttpParser parser;

    parser.feed("GET / HTTP/1.0\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::RequestReady,
           "HTTP/1.0 request may omit Host");
    if (result.request.has_value()) {
        expect(result.request->target == "/",
               "HTTP/1.0 request target should parse");
    }
}

void test_invalid_method_token_errors() {
    http::HttpParser parser;

    parser.feed("G@T / HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "method with invalid token character should be rejected");
}

void test_unknown_valid_method_parses() {
    http::HttpParser parser;

    parser.feed("DELETE /file HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::RequestReady,
           "valid but unsupported method should parse successfully");
    if (result.request.has_value()) {
        expect(result.request->method == "DELETE",
               "parser should preserve valid method for router policy");
    }
}

void test_invalid_target_errors() {
    http::HttpParser parser;

    parser.feed("GET index.html HTTP/1.1\r\n"
                "Host: example.com\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "origin-form target should begin with slash");
}

void test_invalid_header_name_errors() {
    http::HttpParser parser;

    parser.feed("GET / HTTP/1.1\r\n"
                "Bad Header: value\r\n"
                "Host: example.com\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "header name with invalid token character should be rejected");
}

void test_whitespace_before_header_colon_errors() {
    http::HttpParser parser;

    parser.feed("GET / HTTP/1.1\r\n"
                "Host : example.com\r\n"
                "\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "whitespace before header colon should be rejected");
}

void test_malformed_request_line_errors() {
    http::HttpParser parser;

    parser.feed("GET /missing-version\r\nHost: example.com\r\n\r\n");

    auto result = parser.next();
    expect(result.status == http::ParseStatus::Error,
           "malformed request line should be a parse error");
    expect(!result.error.empty(),
           "parse error should include diagnostic text");
}

} // namespace

int main() {
    test_empty_parser_needs_more_data();
    test_complete_get_request();
    test_fragmented_request();
    test_pipelined_requests();
    test_incomplete_body_needs_more_data();
    test_body_uses_exact_content_length();
    test_invalid_content_length_errors();
    test_duplicate_content_length_errors();
    test_duplicate_host_errors();
    test_unknown_duplicate_header_is_conservatively_rejected();
    test_duplicate_connection_fields_are_combined();
    test_transfer_encoding_and_content_length_errors();
    test_unsupported_transfer_encoding_is_classified();
    test_body_limit_errors();
    test_http11_requires_host_header();
    test_http10_allows_missing_host_header();
    test_invalid_method_token_errors();
    test_unknown_valid_method_parses();
    test_invalid_target_errors();
    test_invalid_header_name_errors();
    test_whitespace_before_header_colon_errors();
    test_malformed_request_line_errors();

    if (failures != 0) {
        std::cerr << failures << " parser test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "parser_tests passed\n";
    return 0;
}
