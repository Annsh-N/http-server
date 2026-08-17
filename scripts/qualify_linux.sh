#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
    echo "Linux qualification must run on Linux" >&2
    exit 1
fi

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${BUILD_ROOT:-"$root/build-linux"}

run_build() {
    local name=$1
    shift
    local build_dir="$build_root/$name"
    cmake -S "$root" -B "$build_dir" -G Ninja "$@"
    cmake --build "$build_dir" --parallel
    ctest --test-dir "$build_dir" --output-on-failure
}

CC=gcc CXX=g++ run_build gcc-debug -DCMAKE_BUILD_TYPE=Debug
CC=gcc CXX=g++ run_build gcc-release -DCMAKE_BUILD_TYPE=Release
CC=clang CXX=clang++ run_build clang-debug -DCMAKE_BUILD_TYPE=Debug
CC=clang CXX=clang++ run_build clang-release -DCMAKE_BUILD_TYPE=Release
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
CC=clang CXX=clang++ run_build asan-ubsan \
    -DCMAKE_BUILD_TYPE=Debug -DHTTP_ENABLE_ASAN_UBSAN=ON
TSAN_OPTIONS=halt_on_error=1 CC=gcc CXX=g++ run_build tsan \
    -DCMAKE_BUILD_TYPE=Debug -DHTTP_ENABLE_TSAN=ON
