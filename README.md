# HTTP/1.1 Static File Server

[![Linux](https://github.com/Annsh-N/http-server/actions/workflows/linux.yml/badge.svg)](https://github.com/Annsh-N/http-server/actions/workflows/linux.yml)

A C++20 static file server built directly on POSIX sockets. It implements an
incremental HTTP/1.1 parser, persistent connections, bounded worker dispatch,
absolute I/O deadlines, path-safe file resolution, graceful shutdown, and
structured runtime metrics.

The project keeps protocol parsing, connection state, routing, filesystem
access, and concurrency as separate components. That separation makes the
request lifecycle explicit and allows parser behavior, socket behavior,
ownership transfer, overload handling, and shutdown to be tested independently.

## Architecture

```text
                         bounded FIFO                 one connection per worker
TCP listener -> acceptor -------------> worker pool ----------------------------+
                         queue<Fd>                                             |
                                                                               v
TCP socket <- response writer <- static file handler <- router <- HTTP parser <-+
```

`HttpServer` owns the listener, immutable static-file configuration, worker
pool, and shutdown pipe. The acceptor moves each accepted descriptor into a
fixed-capacity queue. A worker then moves that descriptor into a stack-owned
`Connection` and serves its complete keep-alive lifecycle.

The main request path is:

1. `TcpListener` accepts a TCP connection into a move-only `Fd`.
2. `BoundedQueue<Fd>` transfers ownership from the acceptor to one worker.
3. `Connection` reads arbitrary TCP fragments into an incremental parser.
4. `HttpParser` identifies one complete request and leaves pipelined bytes
   buffered for the next parse.
5. The router dispatches `GET` and `HEAD` to the static-file handler.
6. The file handler resolves and validates the target beneath the configured
   document root.
7. The response serializer emits one correctly framed HTTP response.
8. The connection either parses the next buffered request or waits for another
   request until its keep-alive deadline.

The detailed state transitions and ownership boundaries are documented in
[`docs/architecture.md`](docs/architecture.md).

### Bounded concurrency and backpressure

The server uses a fixed worker count `W` and queue capacity `Q`. At most these
application-owned client descriptors can exist at once:

```text
W active + Q queued + 1 held by a blocked acceptor
```

When the queue is full, dispatch blocks instead of allocating an unbounded work
list. Pressure propagates to `accept`, then to the kernel listen backlog. Queue
closure wakes blocked producers and consumers, which gives shutdown a defined
close-and-drain contract.

### Ownership and thread safety

- `Fd` is a move-only RAII type; exactly one object owns and closes each socket.
- The acceptor relinquishes descriptor ownership when queue insertion succeeds.
- Each worker exclusively owns its active `Connection`, parser, response buffer,
  and write offset.
- Workers share only immutable file-handler configuration and relaxed atomic
  metrics counters.
- The pool and server are non-copyable and non-movable because worker threads
  capture the pool's address.

## HTTP behavior

| Area | Behavior |
| --- | --- |
| Protocol | HTTP/1.0 and HTTP/1.1 origin-form requests |
| Methods | `GET` and `HEAD`; other methods receive `405` with `Allow: GET, HEAD` |
| Request framing | One validated `Content-Length`, with an exact body boundary |
| Persistence | HTTP/1.1 keep-alive by default; HTTP/1.0 keep-alive by token |
| Pipelining | Remaining bytes stay buffered and are parsed before another read |
| Response framing | Exactly one generated `Content-Length`; partial writes resume from a stored offset |
| HEAD | Same representation length as `GET`, with the response body suppressed |
| Limits | 16 KiB headers, 1 MiB request bodies, 16 MiB static files, 100 requests per connection |
| Deadlines | 10 s request read, 5 s keep-alive idle, and 10 s response write |

Header names are normalized for lookup while arrival-order fields remain
available to the parser. Repeated `Connection` fields are combined as token
lists. Ambiguous `Transfer-Encoding` plus `Content-Length` framing receives
`400` and closes the connection; a transfer coding outside the server's framing
policy receives `501` and closes the connection. These policies prevent a
connection from being reused when the next request boundary is uncertain.

## Static file safety

Request targets pass through a fixed validation sequence:

1. remove the query component;
2. percent-decode the path and reject malformed escapes or decoded NUL bytes;
3. resolve `/` to `/index.html`;
4. join the relative path to a canonical document root;
5. weakly canonicalize the candidate, including symlink resolution;
6. compare path components to prove the candidate remains under the root;
7. serve only an existing regular file within the configured size bound.

This component-wise containment check avoids string-prefix errors such as
mistaking `/srv/site-backup` for a child of `/srv/site`.

## Build and run

Requirements are CMake 3.20 or newer, a C++20 compiler, and POSIX threads.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/http_server 8080 www 127.0.0.1 4 128
```

Arguments are positional:

| Position | Setting | Default |
| --- | --- | --- |
| 1 | TCP port | `8080` |
| 2 | Document root | `www` |
| 3 | IPv4 bind address | `127.0.0.1` |
| 4 | Worker count | `4` |
| 5 | Dispatch queue capacity | `128` |

For a public deployment, the server can remain bound to `127.0.0.1` behind a
TLS-terminating reverse proxy on ports 80 and 443.

## Correctness and qualification

CTest runs 11 test executables covering:

- fragmented, incomplete, malformed, body-bearing, and pipelined requests;
- HTTP/1.0 and HTTP/1.1 persistence and connection-token handling;
- duplicate headers and ambiguous request framing;
- response serialization and `HEAD` representation lengths;
- percent decoding, traversal attempts, symlink escapes, and file-size bounds;
- RAII descriptor closure and real loopback TCP accept behavior;
- absolute read, keep-alive, and write deadlines;
- bounded FIFO ordering, blocking backpressure, close/wakeup, and drain behavior;
- worker ownership transfer, connection outcome counters, and graceful shutdown;
- an end-to-end TCP request through parsing, routing, file serving, and response.

GitHub Actions builds Debug and Release configurations on Ubuntu with both GCC
and Clang. Separate jobs run ASan/UBSan and TSan because those instrumentation
modes require independent binaries. The same matrix can be reproduced on a
Linux machine with:

```sh
./scripts/qualify_linux.sh
```

See [`docs/linux-qualification.md`](docs/linux-qualification.md) for the exact
qualification gates.

## Benchmarks

The current baseline was measured on an Apple M2 Pro using loopback TCP, a
Release build of commit `a47b1cc`, four server workers, and queue capacity 128.
Each row is the median of three independent 10-second `wrk` runs after a
3-second warmup. The latency columns are medians of each run's percentile, and
all displayed workloads completed with zero `wrk` socket, timeout, or HTTP
status errors.

| Response | Connections | Requests/s | Transfer/s | p50 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 221 B HTML | 1 | 9,360 | 2.77 MB/s | 0.101 ms | 0.118 ms | 0.137 ms |
| 221 B HTML | 16 | 24,412 | 7.22 MB/s | 0.186 ms | 43.525 ms | 48.152 ms |
| 221 B HTML | 64 | 24,425 | 7.22 MB/s | 0.222 ms | 220.510 ms | 241.065 ms |
| 221 B HTML | 256 | 24,279 | 7.18 MB/s | 0.228 ms | 923.877 ms | 1,013.476 ms |
| 16 KiB binary | 64 | 20,514 | 322.61 MB/s | 0.265 ms | 262.178 ms | 286.685 ms |

Small-response throughput reaches approximately 24.4K requests/s at 16
connections and remains stable as concurrency increases. Above the four-worker
execution width, additional offered concurrency appears primarily as tail
queueing rather than additional throughput. The 16 KiB workload sustains
approximately 322.6 MB/s while exercising the same parser, router, file read,
serialization, and socket-write path.

Raw outputs, commands, environment metadata, and the aggregation method are in
[`benchmarks/results/2026-08-17-m2-pro`](benchmarks/results/2026-08-17-m2-pro)
and [`benchmarks/README.md`](benchmarks/README.md).

## Shutdown and observability

`SIGINT` and `SIGTERM` are converted into a byte written to a nonblocking
self-pipe. `write` is async-signal-safe, so the signal handler does not call
into C++ synchronization or object-lifetime code. The accept loop observes the
pipe, stops dispatch, closes the queue, drains accepted work, and joins every
worker.

At shutdown the process emits one JSON record containing:

- accepted, dispatched, rejected, active, queued, and completed connections;
- queue high-water mark;
- requests served and bytes read/written;
- peer, explicit, and request-limit closes;
- parse failures, read/keep-alive/write timeouts, I/O failures, and worker
  exception counts.

Keeping aggregation in relaxed atomics and reporting once at shutdown avoids
adding per-request logging synchronization to the serving path.

## Repository layout

```text
include/http/   public component interfaces
src/            parser, networking, routing, serving, and metrics implementation
tests/          unit, concurrency, timeout, and loopback integration tests
docs/           architecture and Linux qualification details
benchmarks/     wrk harness, workload preparation, raw results, and methodology
www/            default document root
legacy/         historical course-server implementation
```
