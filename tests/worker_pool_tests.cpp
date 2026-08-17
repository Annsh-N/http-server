#include "http/worker_pool.hpp"

#include "http/fd.hpp"
#include "http/static_file_handler.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

int failures = 0;

static_assert(!std::is_copy_constructible_v<http::WorkerPool>);
static_assert(!std::is_move_constructible_v<http::WorkerPool>);

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Fixture {
    std::filesystem::path root;
    http::StaticFileHandler file_handler;

    Fixture()
        : root(std::filesystem::temp_directory_path() /
               "systems_http_worker_pool_tests"),
          file_handler(make_root(root)) {}

    ~Fixture() {
        std::filesystem::remove_all(root);
    }

private:
    static std::filesystem::path make_root(
        const std::filesystem::path& root) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::ofstream(root / "index.html", std::ios::binary)
            << "worker response\n";
        return root;
    }
};

struct SocketPair {
    http::Fd client;
    http::Fd server;
};

SocketPair make_socket_pair() {
    int descriptors[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        perror("socketpair");
        std::exit(1);
    }
    return SocketPair{http::Fd(descriptors[0]), http::Fd(descriptors[1])};
}

void write_all_or_die(int fd, const std::string& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n =
            write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0) {
            perror("write");
            std::exit(1);
        }
        offset += static_cast<std::size_t>(n);
    }
}

std::string read_until_eof(int fd) {
    std::string response;
    char buffer[1024];
    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            response.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return response;
        }

        perror("read");
        std::exit(1);
    }
}

void test_worker_count_must_be_positive() {
    Fixture fixture;
    bool threw = false;
    try {
        http::WorkerPool pool(0, 1, fixture.file_handler);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero-worker pool should be rejected");
}

void test_shutdown_drains_dispatched_connection() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    http::WorkerPool pool(1, 1, fixture.file_handler);

    write_all_or_die(sockets.client.get(),
                     "GET / HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "Connection: close\r\n"
                     "\r\n");
    shutdown(sockets.client.get(), SHUT_WR);
    expect(pool.dispatch(std::move(sockets.server)),
           "open pool should accept connection ownership");

    pool.shutdown();
    const std::string response = read_until_eof(sockets.client.get());
    expect(response.find("HTTP/1.1 200 OK\r\n") == 0,
           "shutdown should drain queued connections before joining");
    expect(response.find("\r\n\r\nworker response\n") != std::string::npos,
           "worker should run the complete connection pipeline");

    const http::WorkerPoolStats stats = pool.stats();
    expect(stats.dispatched_connections == 1,
           "worker stats should count dispatched connections");
    expect(stats.completed_connections == 1,
           "worker stats should count completed connections");
    expect(stats.requests_served == 1,
           "worker stats should aggregate served requests");
    expect(stats.explicit_closes == 1,
           "worker stats should classify Connection close outcome");
    expect(stats.bytes_read > 0 && stats.bytes_written > 0,
           "worker stats should aggregate transferred bytes");
}

void test_dispatch_fails_after_shutdown() {
    Fixture fixture;
    SocketPair sockets = make_socket_pair();
    http::WorkerPool pool(1, 1, fixture.file_handler);
    pool.shutdown();

    expect(!pool.dispatch(std::move(sockets.server)),
           "closed pool should reject new connection ownership");
    expect(pool.stats().rejected_dispatches == 1,
           "worker stats should count rejected dispatches");
    char byte;
    expect(read(sockets.client.get(), &byte, 1) == 0,
           "rejected connection descriptor should be closed by RAII");
}

void test_full_dispatch_queue_blocks_acceptor() {
    Fixture fixture;
    const http::ConnectionTimeouts timeouts{2s, 1s};
    http::WorkerPool pool(1, 1, fixture.file_handler, timeouts);

    SocketPair slow = make_socket_pair();
    write_all_or_die(slow.client.get(), "GET / HTTP/1.1\r\nHost:");
    expect(pool.dispatch(std::move(slow.server)),
           "first connection should dispatch to worker");

    const auto wait_deadline = std::chrono::steady_clock::now() + 1s;
    while (pool.pending() != 0 &&
           std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    expect(pool.pending() == 0,
           "worker should remove slow connection from queue");

    SocketPair queued = make_socket_pair();
    write_all_or_die(queued.client.get(),
                     "GET / HTTP/1.1\r\nHost: example.com\r\n"
                     "Connection: close\r\n\r\n");
    shutdown(queued.client.get(), SHUT_WR);
    expect(pool.dispatch(std::move(queued.server)),
           "second connection should occupy queue slot");

    SocketPair blocked = make_socket_pair();
    write_all_or_die(blocked.client.get(),
                     "GET / HTTP/1.1\r\nHost: example.com\r\n"
                     "Connection: close\r\n\r\n");
    shutdown(blocked.client.get(), SHUT_WR);
    std::promise<void> dispatch_started;
    auto started = dispatch_started.get_future();
    std::promise<bool> dispatch_finished;
    auto finished = dispatch_finished.get_future();
    std::thread producer([&] {
        dispatch_started.set_value();
        dispatch_finished.set_value(pool.dispatch(std::move(blocked.server)));
    });

    started.wait();
    expect(finished.wait_for(50ms) == std::future_status::timeout,
           "third dispatch should block while bounded queue is full");

    slow.client.reset();
    expect(finished.wait_for(1s) == std::future_status::ready,
           "dispatch should resume when worker frees queue capacity");
    expect(finished.get(), "unblocked dispatch should transfer ownership");
    producer.join();
    pool.shutdown();
}

} // namespace

int main() {
    test_worker_count_must_be_positive();
    test_shutdown_drains_dispatched_connection();
    test_dispatch_fails_after_shutdown();
    test_full_dispatch_queue_blocks_acceptor();

    if (failures != 0) {
        std::cerr << failures << " worker pool test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "worker_pool_tests passed\n";
    return 0;
}
