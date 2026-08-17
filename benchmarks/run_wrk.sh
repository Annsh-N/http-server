#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 2 ]]; then
    echo "usage: $0 URL OUTPUT_FILE" >&2
    exit 2
fi

if ! command -v wrk >/dev/null 2>&1; then
    echo "wrk is required" >&2
    exit 1
fi

url=$1
output=$2
threads=${THREADS:-4}
connections=${CONNECTIONS:-64}
duration=${DURATION:-30s}
warmup=${WARMUP:-5s}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repository_root=$(cd -- "$script_dir/.." && pwd)
commit=${SERVER_COMMIT:-$(git -C "$repository_root" rev-parse HEAD 2>/dev/null || echo unknown)}
build_type=${SERVER_BUILD_TYPE:-unknown}
worker_count=${SERVER_WORKERS:-unknown}
queue_capacity=${SERVER_QUEUE_CAPACITY:-unknown}

mkdir -p "$(dirname -- "$output")"

wrk -t"$threads" -c"$connections" -d"$warmup" "$url" >/dev/null

{
    echo "timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "system=$(uname -a)"
    if command -v lscpu >/dev/null 2>&1; then
        echo "cpu=$(lscpu | awk -F: '/Model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}')"
    elif command -v sysctl >/dev/null 2>&1; then
        echo "cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
    fi
    echo "wrk=$(wrk --version 2>&1 | head -n 1)"
    echo "server_commit=$commit"
    echo "server_build_type=$build_type"
    echo "server_workers=$worker_count"
    echo "server_queue_capacity=$queue_capacity"
    if command -v c++ >/dev/null 2>&1; then
        echo "compiler=$(c++ --version 2>&1 | head -n 1)"
    fi
    echo "url=$url"
    echo "threads=$threads"
    echo "connections=$connections"
    echo "duration=$duration"
    echo "warmup=$warmup"
    echo "command=wrk -t$threads -c$connections -d$duration --latency -s $script_dir/wrk_latency.lua $url"
    wrk -t"$threads" -c"$connections" -d"$duration" --latency \
        -s "$script_dir/wrk_latency.lua" "$url"
} | tee "$output"
