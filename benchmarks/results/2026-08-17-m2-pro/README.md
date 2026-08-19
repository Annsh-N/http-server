# Apple M2 Pro Baseline

This baseline measures the complete static-file request path over loopback TCP:
accept, bounded dispatch, incremental parsing, routing, file loading, response
serialization, and socket writes.

## Environment

| Setting | Value |
| --- | --- |
| Server commit | `a47b1cc` |
| Build | CMake Release, Apple Clang 21.0.0 |
| Host | Apple M2 Pro, arm64, Darwin 25.5.0 |
| Load generator | `wrk` 4.2.0 using `kqueue` |
| Server | 4 workers, queue capacity 128 |
| Transport | Loopback TCP on `127.0.0.1` |
| Measured duration | 10 seconds per run |
| Warmup | 3 seconds before each run |
| Repetitions | 3 per workload |

`wrk` used one load-generator thread for the single-connection workload and
four threads for every other workload. The server configuration remained fixed.

## Results

Each value is the median of three runs. Latency values are medians of the
corresponding per-run percentile.

| Response | Connections | Requests/s | Transfer/s | p50 | p95 | p99 | Errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 221 B HTML | 1 | 9,360 | 2.77 MB/s | 0.101 ms | 0.118 ms | 0.137 ms | 0 |
| 221 B HTML | 16 | 24,412 | 7.22 MB/s | 0.186 ms | 43.525 ms | 48.152 ms | 0 |
| 221 B HTML | 64 | 24,425 | 7.22 MB/s | 0.222 ms | 220.510 ms | 241.065 ms | 0 |
| 221 B HTML | 256 | 24,279 | 7.18 MB/s | 0.228 ms | 923.877 ms | 1,013.476 ms | 0 |
| 16 KiB binary | 64 | 20,514 | 322.61 MB/s | 0.265 ms | 262.178 ms | 286.685 ms | 0 |

The raw files retain the exact command, timestamp, host details, aggregate
throughput, latency histogram, custom p50/p95/p99 values, and every `wrk` error
counter. Files are named by response, connection count, and repetition; for
example, `index-c64-r2.txt` is the second 64-connection HTML run.
