#include "http/server.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint16_t parse_port(std::string_view input) {
    unsigned int port = 0;
    const char* begin = input.data();
    const char* end = input.data() + input.size();
    const auto result = std::from_chars(begin, end, port);

    if (result.ec != std::errc{} || result.ptr != end || port == 0 ||
        port > 65535) {
        throw std::runtime_error("port must be an integer from 1 to 65535");
    }

    return static_cast<std::uint16_t>(port);
}

std::size_t parse_positive_size(std::string_view input,
                                std::string_view name) {
    std::size_t value = 0;
    const char* begin = input.data();
    const char* end = input.data() + input.size();
    const auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc{} || result.ptr != end || value == 0) {
        throw std::runtime_error(std::string(name) +
                                 " must be a positive integer");
    }

    return value;
}

void print_usage(const char* program) {
    std::cerr << "usage: " << program
              << " [port] [document-root] [bind-address] [workers]"
                 " [queue-capacity]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 6) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        const std::uint16_t port = argc >= 2 ? parse_port(argv[1]) : 8080;
        const std::filesystem::path document_root =
            argc >= 3 ? argv[2] : "www";
        const std::string bind_address = argc >= 4 ? argv[3] : "127.0.0.1";
        const std::size_t worker_count =
            argc >= 5 ? parse_positive_size(argv[4], "worker count") : 4;
        const std::size_t queue_capacity =
            argc >= 6 ? parse_positive_size(argv[5], "queue capacity") : 128;

        http::HttpServer server(document_root, port, 128, bind_address,
                                http::ConnectionConfig{},
                                worker_count, queue_capacity);
        std::cout << "listening on http://" << bind_address << ':'
                  << server.port() << " serving "
                  << std::filesystem::canonical(document_root) << " with "
                  << worker_count << " workers and queue capacity "
                  << queue_capacity << '\n';
        server.serve_forever();
    } catch (const std::exception& error) {
        std::cerr << "http_server: " << error.what() << '\n';
        return 1;
    }
}
