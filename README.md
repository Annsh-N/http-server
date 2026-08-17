# Systems HTTP Server

A C++20 HTTP/1.1 static file server built as a systems engineering project.

The implementation is intentionally developed in small, testable increments:

1. incremental HTTP parser;
2. RAII socket and file-descriptor ownership;
3. TCP acceptor;
4. bounded dispatch queue;
5. fixed worker pool;
6. connection state machine with keep-alive;
7. static file serving with path safety;
8. timeouts, metrics, and benchmark methodology.

The legacy course implementation remains in the repository for comparison while
the production-oriented implementation is built under `src/`, `include/`, and
`tests/`.

## Run

From the repository root:

```sh
cmake -S . -B build
cmake --build build
./build/http_server 8080 www 127.0.0.1 4 128
```

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure
```

The arguments are optional and ordered as port, document root, IPv4 bind
address, worker count, and dispatch-queue capacity. The acceptor blocks when
the queue is full, bounding the number of accepted connections waiting for a
worker.

## Supported HTTP subset

- HTTP/1.0 and HTTP/1.1 origin-form requests;
- `GET` and `HEAD` static-file responses;
- `Content-Length` request framing with bounded bodies;
- persistent connections and pipelined requests;
- absolute request, keep-alive, and response-write deadlines;
- a default limit of 100 requests per connection;
- a default static-file limit of 16 MiB.

Chunked request bodies are not implemented. Requests containing
`Transfer-Encoding` are rejected and the connection is closed; requests that
also contain `Content-Length` are treated as ambiguous framing and return
`400 Bad Request`.

## Shutdown and metrics

`SIGINT` and `SIGTERM` stop acceptance, close the dispatch queue, drain queued
and active connections, and join the worker threads. The process prints one
JSON shutdown record containing connection, request, byte, queue-pressure,
timeout, protocol-error, and I/O-error counters.

## Benchmarks

The reproducible `wrk` harness and workload controls are documented in
[`benchmarks/README.md`](benchmarks/README.md). Benchmark results are meaningful
only when accompanied by the commit, release configuration, machine details,
load parameters, errors, and repeated p50/p95/p99 measurements.

## Linux qualification

Ubuntu CI builds Debug and Release configurations with GCC and Clang, then
runs separate ASan/UBSan and TSan jobs. The gates and local reproduction script
are documented in [`docs/linux-qualification.md`](docs/linux-qualification.md).
