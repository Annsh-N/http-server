cmake_minimum_required(VERSION 3.20)

project(systems_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(http_core
    src/parser.cpp
    src/response.cpp
    src/router.cpp
    src/static_file_handler.cpp
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
