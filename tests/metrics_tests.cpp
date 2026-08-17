#include "http/metrics.hpp"

#include <chrono>
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

void test_shutdown_metrics_include_load_and_failure_dimensions() {
    http::HttpServerStats stats;
    stats.accepted_connections = 11;
    stats.workers.dispatched_connections = 10;
    stats.workers.rejected_dispatches = 1;
    stats.workers.queue_high_water = 7;
    stats.workers.requests_served = 23;
    stats.workers.bytes_written = 4096;
    stats.workers.read_timeouts = 2;
    stats.workers.parse_errors = 3;

    const std::string metrics =
        http::format_shutdown_metrics(stats, std::chrono::milliseconds(1250));

    expect(metrics.find("\"event\":\"server_shutdown\"") !=
               std::string::npos,
           "metrics should identify the lifecycle event");
    expect(metrics.find("\"uptime_ms\":1250") != std::string::npos,
           "metrics should include process uptime");
    expect(metrics.find("\"accepted_connections\":11") !=
               std::string::npos,
           "metrics should include acceptor load");
    expect(metrics.find("\"queue_high_water\":7") != std::string::npos,
           "metrics should expose queue pressure");
    expect(metrics.find("\"requests_served\":23") != std::string::npos,
           "metrics should include completed request work");
    expect(metrics.find("\"read_timeouts\":2") != std::string::npos,
           "metrics should classify timeout failures");
    expect(metrics.find("\"parse_errors\":3") != std::string::npos,
           "metrics should classify protocol failures");
    expect(metrics.front() == '{' && metrics.back() == '}',
           "metrics should be one complete JSON object");
}

} // namespace

int main() {
    test_shutdown_metrics_include_load_and_failure_dimensions();

    if (failures != 0) {
        std::cerr << failures << " metrics test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "metrics_tests passed\n";
    return 0;
}
