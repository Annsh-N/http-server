# Architecture

The server is built as a pipeline:

```text
TCP socket
  -> Acceptor
  -> Bounded queue
  -> Worker pool
  -> Connection state machine
  -> HTTP parser
  -> Request router
  -> Static file handler
  -> Response writer
  -> TCP socket
```

Each layer owns one responsibility. This keeps protocol parsing, network I/O,
dispatch, file serving, and observability separate enough to test and defend.

