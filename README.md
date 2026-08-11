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

The arguments are optional and ordered as port, document root, IPv4 bind
address, worker count, and dispatch-queue capacity. The acceptor blocks when
the queue is full, bounding the number of accepted connections waiting for a
worker.
