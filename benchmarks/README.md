# Benchmark Methodology

Benchmarks use `wrk` against a release build on the loopback interface. The
scripts preserve raw output and machine metadata; they do not turn a single run
into a performance claim.

The current measured baseline and its raw outputs are under
[`results/2026-08-17-m2-pro`](results/2026-08-17-m2-pro).

## Run

Build and start the server in separate terminals:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./benchmarks/prepare_files.sh www
./build-release/http_server 8080 www 127.0.0.1 4 128
```

Run the concurrency and response-size matrix:

```sh
DURATION=30s WARMUP=5s ./benchmarks/run_matrix.sh \
  http://127.0.0.1:8080 benchmarks/results/local
```

Each measured run reports throughput, transfer rate, p50/p95/p99 latency, and
socket or HTTP errors. Repeat the matrix at least three times. Report the
median throughput and retain all tail-latency samples; do not select only the
best run.

## Controlled variables

- Record CPU, operating system, compiler, commit, build type, worker count,
  queue capacity, `wrk` threads, connections, duration, and URL.
- Run server and load generator on otherwise idle hardware. Loopback measures
  application and kernel networking costs, not physical-network latency.
- Use a warmup period before every measured run.
- Change one dimension at a time: connection concurrency, response size,
  worker count, or queue capacity.
- Treat non-2xx/3xx responses and socket errors as failures, not throughput.

The initial matrix uses 1, 16, 64, and 256 concurrent connections and the small
HTML fixture, 16 KiB, and 1 MiB files. A second pass should restart the server
with 1, 2, 4, and 8 workers to locate the point where synchronization and
scheduling costs outweigh additional parallelism.
