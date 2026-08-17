#pragma once

#include "http/server.hpp"

#include <chrono>
#include <string>

namespace http {

[[nodiscard]] std::string format_shutdown_metrics(
    const HttpServerStats& stats, std::chrono::milliseconds uptime);

} // namespace http
