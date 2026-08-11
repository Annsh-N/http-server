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

## Connection state machine

Each accepted socket owns one `Connection`. The connection also owns its HTTP
parser, buffered response, and current write offset.

```text
Reading --bytes read--> Parsing
Reading --EOF/error--> Closed

Parsing --incomplete request--> Reading
Parsing --complete request--> Writing
Parsing --malformed request--> Writing (400, then close)

Writing --partial write--> Writing
Writing --complete, keep-alive--> Parsing
Writing --complete, close--> Closed
Writing --socket error--> Closed
```

Returning from `Writing` to `Parsing` is important for HTTP pipelining: the
parser may already contain the next request, so the server checks buffered
bytes before reading from the socket again. A write offset is retained across
partial writes so every serialized response byte is sent exactly once and in
order.
