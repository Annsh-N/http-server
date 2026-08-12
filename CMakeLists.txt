cmake_minimum_required(VERSION 3.20)

project(systems_http_server LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(http_core
    src/connection.cpp
    src/fd.cpp
    src/parser.cpp
    src/response.cpp
    src/router.cpp
    src/server.cpp
    src/static_file_handler.cpp
    src/tcp_listener.cpp
    src/worker_pool.cpp
)

target_include_directories(http_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(http_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(http_core
    PUBLIC
        Threads::Threads
)

add_executable(http_server
    src/main.cpp
)

target_link_libraries(http_server
    PRIVATE
        http_core
)

add_executable(parser_tests
    tests/parser_tests.cpp
)

target_link_libraries(parser_tests
    PRIVATE
        http_core
)

enable_testing()
add_test(NAME parser_tests COMMAND parser_tests)

add_executable(response_tests
    tests/response_tests.cpp
)

target_link_libraries(response_tests
    PRIVATE
        http_core
)

add_test(NAME response_tests COMMAND response_tests)

add_executable(router_tests
    tests/router_tests.cpp
)

target_link_libraries(router_tests
    PRIVATE
        http_core
)

add_test(NAME router_tests COMMAND router_tests)

add_executable(static_file_handler_tests
    tests/static_file_handler_tests.cpp
)

target_link_libraries(static_file_handler_tests
    PRIVATE
        http_core
)

add_test(NAME static_file_handler_tests COMMAND static_file_handler_tests)

add_executable(fd_tests
    tests/fd_tests.cpp
)

target_link_libraries(fd_tests
    PRIVATE
        http_core
)

add_test(NAME fd_tests COMMAND fd_tests)

add_executable(tcp_listener_tests
    tests/tcp_listener_tests.cpp
)

target_link_libraries(tcp_listener_tests
    PRIVATE
        http_core
)

add_test(NAME tcp_listener_tests COMMAND tcp_listener_tests)

add_executable(connection_tests
    tests/connection_tests.cpp
)

target_link_libraries(connection_tests
    PRIVATE
        http_core
        Threads::Threads
)

add_test(NAME connection_tests COMMAND connection_tests)

add_executable(server_tests
    tests/server_tests.cpp
)

target_link_libraries(server_tests
    PRIVATE
        http_core
        Threads::Threads
)

add_test(NAME server_tests COMMAND server_tests)

add_executable(bounded_queue_tests
    tests/bounded_queue_tests.cpp
)

target_link_libraries(bounded_queue_tests
    PRIVATE
        Threads::Threads
)

target_include_directories(bounded_queue_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_test(NAME bounded_queue_tests COMMAND bounded_queue_tests)

add_executable(worker_pool_tests
    tests/worker_pool_tests.cpp
)

target_link_libraries(worker_pool_tests
    PRIVATE
        http_core
)

add_test(NAME worker_pool_tests COMMAND worker_pool_tests)
