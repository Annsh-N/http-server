#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 DOCUMENT_ROOT" >&2
    exit 2
fi

document_root=$1
mkdir -p "$document_root"
dd if=/dev/zero of="$document_root/bench-16k.bin" bs=16384 count=1 status=none
dd if=/dev/zero of="$document_root/bench-1m.bin" bs=1048576 count=1 status=none
