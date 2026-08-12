# Architecture

The concurrent server is built as a pipeline:

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

The acceptor and workers have separate responsibilities:

```text
HttpServer::serve_forever
  -> TcpListener::accept
  -> BoundedQueue<Fd>::push
  -> WorkerPool::worker_loop
  -> BoundedQueue<Fd>::pop
  -> Connection::serve
  -> HTTP parser
  -> request router
  -> static file handler
  -> response serializer
  -> accepted TCP socket
  -> worker waits for next descriptor
```

`HttpServer` owns the listener and shared immutable file-handler configuration.
It accepts each client and moves the descriptor into the bounded queue. A fixed
worker removes the descriptor, moves it into a stack-allocated `Connection`,
and serves the complete keep-alive lifecycle. The acceptor blocks when the
queue is full, so accepted descriptors cannot grow without a configured bound.

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

Reading --request deadline--> Closed
Reading --keep-alive deadline--> Closed
```

Returning from `Writing` to `Parsing` is important for HTTP pipelining: the
parser may already contain the next request, so the server checks buffered
bytes before reading from the socket again. A write offset is retained across
partial writes so every serialized response byte is sent exactly once and in
order.

The request deadline is absolute: receiving another fragment does not extend
it. This bounds clients that slowly drip an incomplete request. Once a response
is fully written with keep-alive enabled, the connection switches to a separate
idle deadline while waiting for the next request.

## Bounded dispatch queue

`BoundedQueue<T>` is a fixed-capacity FIFO for transferring move-only accepted
descriptors from an acceptor to workers. A producer blocks while the queue is
full, propagating pressure back to the accept loop instead of allowing
unbounded memory and descriptor growth. Consumers block while it is empty.

With `W` workers and queue capacity `Q`, the application owns at most `W`
actively served descriptors, `Q` queued descriptors, and one descriptor held
by an acceptor blocked in `push`. Additional completed TCP handshakes remain in
the kernel listen backlog rather than consuming an unbounded application queue.

Closing the queue wakes all blocked producers and consumers. New pushes fail,
but consumers drain items that were already queued before `pop` returns an
empty optional. Callers must close the queue and join waiting threads before
destroying it.

## Worker pool

`WorkerPool` creates a fixed number of threads. Each thread repeatedly pops one
descriptor, moves it into a `Connection`, and serves that connection until it
closes or times out. An exception boundary prevents one failed connection from
terminating the process or permanently reducing worker capacity.

Shutdown closes the queue, drains descriptors accepted before closure, waits
for active connections to finish, and joins every thread. The pool and server
are non-movable because worker threads capture the pool's address.
