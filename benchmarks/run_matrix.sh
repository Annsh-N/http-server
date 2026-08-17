#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 2 ]]; then
    echo "usage: $0 BASE_URL OUTPUT_DIRECTORY" >&2
    exit 2
fi

base_url=${1%/}
output_dir=$2
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
connections_list=${CONNECTIONS_LIST:-"1 16 64 256"}
paths=${PATHS:-"index.html bench-16k.bin bench-1m.bin"}

mkdir -p "$output_dir"
for path in $paths; do
    label=${path//\//_}
    for connections in $connections_list; do
        CONCURRENT_OUTPUT="$output_dir/${label}-c${connections}.txt"
        CONNECTIONS=$connections "$script_dir/run_wrk.sh" \
            "$base_url/$path" "$CONCURRENT_OUTPUT"
    done
done
