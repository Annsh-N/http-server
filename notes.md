# HTTP Server Notes

## Network Path: Client Socket to Server Socket

When a client sends an HTTP request, the application bytes are carried through
several network layers before the server reads them from its TCP socket.

```text
Client application
  |
  v
Client TCP socket
  |
  v
TCP segment
  |
  v
IP packet
  |
  v
Ethernet/Wi-Fi frame
  |
  v
Network links and routers
  |
  v
Ethernet/Wi-Fi frame
  |
  v
IP packet
  |
  v
TCP segment
  |
  v
Server TCP socket
  |
  v
Server application
```

### Application Layer: HTTP Bytes

HTTP defines the application-level message:

```http
GET /index.html HTTP/1.1
Host: example.com
```

HTTP adds meaning such as:

- method: `GET`, `HEAD`, `POST`;
- request target: `/index.html`;
- protocol version: `HTTP/1.1`;
- headers: `Host`, `Connection`, `Content-Length`;
- optional body bytes.

TCP does not understand these fields. To TCP, HTTP is just bytes.

### Transport Layer: TCP Segment

TCP wraps the HTTP bytes in a TCP header.

```text
[TCP header | HTTP bytes]
```

The TCP header includes fields such as:

- source port;
- destination port;
- sequence number;
- acknowledgment number;
- flags such as `SYN`, `ACK`, `FIN`, `RST`;
- receive window;
- checksum.

TCP provides a reliable, ordered, bidirectional byte stream between two
processes. It handles retransmission, ordering, flow control, and connection
shutdown.

Important server implication:

```text
One write() by the client is not guaranteed to become one read() by the server.
```

The server must parse HTTP message boundaries from a stream of bytes.

### Internet Layer: IP Packet

IP wraps the TCP segment in an IP header.

```text
[IP header | TCP header | HTTP bytes]
```

The IP header includes fields such as:

- source IP address;
- destination IP address;
- protocol, for example TCP;
- packet length;
- TTL.

IP is responsible for routing packets from one machine to another across
networks. It does not provide reliability by itself. TCP adds reliability above
IP.

### Link Layer: Ethernet or Wi-Fi Frame

Ethernet or Wi-Fi wraps the IP packet in a link-layer frame.

```text
[Ethernet/Wi-Fi header | IP header | TCP header | HTTP bytes | trailer]
```

The Ethernet or Wi-Fi header includes fields such as:

- source MAC address;
- destination MAC address;
- frame type, for example IPv4 or IPv6.

The trailer usually includes a frame check sequence used to detect corruption
on the local link.

Ethernet and Wi-Fi are hop-by-hop. A frame gets the packet from one device to
the next local device, such as from a laptop to a router. Routers remove the old
frame, inspect the IP packet, choose the next hop, and create a new frame for
the next link.

### Checksum

A checksum is a compact value computed from a block of data to help detect
corruption.

Sender:

```text
compute checksum over data
send data plus checksum
```

Receiver:

```text
recompute checksum over received data
compare against transmitted checksum
discard data if the values do not match
```

Checksums detect many transmission errors, but they are not cryptographic
proofs of integrity.

### TTL

TTL means Time To Live.

In IP, TTL is a counter that limits how many router hops a packet can take.
Each router decrements the TTL. If TTL reaches zero, the router discards the
packet.

TTL prevents packets from looping forever when routing is broken.

## Our Server Architecture

Our server lives above TCP. The kernel gives our program socket file
descriptors. Our job is to accept connections, parse HTTP requests from TCP
byte streams, dispatch bounded work, serve files safely, and write valid HTTP
responses.

```text
Client
  |
  v
TCP socket
  |
  v
Acceptor
  |
  v
Bounded queue
  |
  v
Worker pool / worker threads
  |
  v
Connection state machine
  |
  v
HTTP parser
  |
  v
Request router
  |
  v
Static file handler
  |
  v
Response writer
  |
  v
TCP socket
  |
  v
Client
```

### TCP Socket

A TCP socket is an operating-system endpoint for a TCP connection.

The server uses one listening socket to accept new connections. Each accepted
connection gets its own connected socket file descriptor.

### Acceptor

The acceptor owns the listening socket.

Its job:

```text
accept new TCP connection
configure the connected socket
submit the connection to the bounded queue
reject or close when the queue is full
```

The acceptor does not parse HTTP and does not serve files.

### Bounded Queue

The bounded queue sits between the acceptor and the worker threads.

It gives the server explicit overload behavior. Instead of creating unlimited
threads or accepting unlimited work, the server admits only a fixed amount of
pending work.

When the queue is full, the server should reject the connection or return a
`503 Service Unavailable` response and close.

### Worker Pool / Worker Threads

The worker pool owns a fixed number of long-lived threads.

Each worker repeatedly does:

```text
pop one connection from the queue
serve that connection
close it when complete, idle, errored, or timed out
```

This bounds concurrency and prevents thread explosion under load.

### Connection State Machine

A connection is not the same as a request.

One HTTP/1.1 connection may carry multiple requests:

```text
Request 1 -> Response 1
Request 2 -> Response 2
Request 3 -> Response 3
close
```

The connection state machine tracks:

- socket ownership;
- read buffer;
- write behavior;
- keep-alive decision;
- timeout state;
- number of requests served;
- whether the connection should close.

### HTTP Parser

The HTTP parser consumes bytes from the connection read buffer and tries to
produce complete HTTP requests.

It must handle:

- partial requests split across multiple reads;
- multiple requests arriving in one read;
- request line parsing;
- header parsing;
- `Content-Length`;
- malformed request detection;
- maximum request/header sizes.

The parser's job is to find HTTP message boundaries inside the TCP byte stream.

### Request Router

The request router decides which handler should process a parsed request.

Example routes:

```text
GET /file      -> static file handler
HEAD /file     -> static file handler without response body
GET /metrics   -> metrics handler
other methods  -> 405 Method Not Allowed or 501 Not Implemented
bad request    -> 400 Bad Request
missing file   -> 404 Not Found
```

### Static File Handler

The static file handler maps a request target to a file under the configured
document root.

It must prevent path traversal:

```text
/../../etc/passwd
/%2e%2e/%2e%2e/etc/passwd
```

The handler should canonicalize the requested path and verify that the final
path stays inside the server's root directory.

#### `std::filesystem` and `path`

`std::filesystem` is the C++ standard library module for working with
filesystem paths, files, and directories. It gives typed APIs such as:

```cpp
std::filesystem::exists(path);
std::filesystem::is_regular_file(path);
std::filesystem::canonical(path);
std::filesystem::weakly_canonical(path);
std::filesystem::file_size(path);
```

`std::filesystem::path` is not just a string and not exactly a vector of
strings. It is a filesystem-aware value type. It can store a native path,
join paths, split paths into components, extract filenames and extensions, and
iterate component by component.

Example:

```cpp
std::filesystem::path p = "/server/www/index.html";

p.filename();    // "index.html"
p.extension();   // ".html"
p.parent_path(); // "/server/www"
```

Joining paths should use the `/` operator instead of manual string
concatenation:

```cpp
std::filesystem::path root = "/server/www";
std::filesystem::path file = "index.html";
auto full = root / file; // /server/www/index.html
```

#### Canonicalization

Canonicalizing a path means turning a messy path into its clean, absolute,
real filesystem form.

Paths can contain navigation pieces:

```text
.   current directory
..  parent directory
```

These may all refer to the same file:

```text
/server/www/index.html
/server/www/./index.html
/server/www/images/../index.html
/server/www//index.html
```

Canonicalization normalizes them to one clean path:

```text
/server/www/index.html
```

The security reason matters more:

```text
root:      /server/www
candidate: /server/www/../../etc/passwd
```

The raw candidate string starts with `/server/www`, but after resolving `..`
it becomes:

```text
/etc/passwd
```

That path escaped the document root and must be rejected.

Canonicalization can also resolve symlinks. If:

```text
/server/www/logs -> /var/log
```

then:

```text
/server/www/logs/system.log
```

really points to:

```text
/var/log/system.log
```

Canonicalization asks the operating system/filesystem metadata about each path
component. When it encounters a symlink, it reads the symlink target, replaces
that path component with the target, and continues resolving.

`std::filesystem::canonical(path)` requires the whole path to exist.
`std::filesystem::weakly_canonical(path)` can normalize the existing prefix and
clean up the rest even if the final file is missing. That is useful because a
web server must distinguish:

```text
inside root but missing -> 404 Not Found
escaped root           -> 403 Forbidden
```

#### Percent Decoding

HTTP targets can encode characters with percent escapes:

```text
%2e = .
%2f = /
```

An attacker may send encoded traversal:

```text
/%2e%2e/%2e%2e/etc/passwd
```

Before path safety checks, the handler must percent-decode the target. If
decoding is malformed, such as `/%ZZ`, return `400 Bad Request`.

After decoding:

```text
/%2e%2e/%2e%2e/etc/passwd
```

becomes:

```text
/../../etc/passwd
```

Then canonicalization and inside-root validation can catch the escape.

#### Inside-Root Validation

`std::filesystem` does not provide a direct `is_inside(root, candidate)`
function. We implement that policy ourselves after canonicalization.

Do not use simple string prefix checks:

```text
root:      /server/www
candidate: /server/www_evil/file.txt
```

As strings, the candidate starts with `/server/www`, but as filesystem paths
`www_evil` is a sibling of `www`, not a child.

Instead, compare path components using `std::filesystem::path` iterators:

```cpp
bool is_inside_root(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();

    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end()) {
            return false;
        }

        if (*root_it != *candidate_it) {
            return false;
        }
    }

    return true;
}
```

Example:

```text
root:      /, server, www
candidate: /, server, www, images, logo.png
```

The root components match the beginning of the candidate, so the candidate is
inside root.

Rejected example:

```text
root:      /, server, www
candidate: /, server, www_evil, file.txt
```

`www` does not equal `www_evil`, so reject.

The file handler's core safety rule is:

```text
Never open or read a file until after percent decoding, canonicalization, and
inside-root validation.
```

### Response Writer

The response writer serializes the HTTP response:

```http
HTTP/1.1 200 OK
Content-Length: 1234
Content-Type: text/html
Connection: keep-alive

<body bytes>
```

It must handle partial writes because `write()` is not guaranteed to send all
bytes in one call.

### Sending Response Through TCP Socket

After the response writer produces bytes, the server writes them to the
connected TCP socket.

The kernel places those bytes into its TCP send buffer, then TCP handles
packetization, sequence numbers, acknowledgments, retransmission, flow control,
and delivery to the client.

The server still needs write deadlines so a slow or stuck client cannot hold a
worker forever.

## HTTP Correctness Checklist

1. **Message framing:** TCP is only a byte stream. The parser must consume the
   header terminator plus exactly the declared body bytes, leaving later bytes
   for the next pipelined request.
2. **Transfer-Encoding:** Chunked request bodies are outside the supported
   subset. The server returns `501` and closes instead of guessing their end.
3. **Transfer-Encoding with Content-Length:** Two competing body lengths are
   ambiguous and can enable request smuggling. The server returns `400` and
   closes.
4. **Duplicate headers:** Raw fields are retained. Repeated `Connection` list
   fields are combined; other duplicates, including `Host` and
   `Content-Length`, are conservatively rejected.
5. **HEAD:** A HEAD response advertises the same representation length as GET
   but suppresses all body bytes.
6. **Requests per connection:** Keep-alive connections have a fixed request
   limit. The final allowed response carries `Connection: close` so one client
   cannot own a worker indefinitely.
7. **Write timeout:** Each response has one absolute write deadline. Partial
   writes do not reset it, so a client that stops reading cannot retain a
   worker forever.
8. **Response size:** Static files have a configured maximum size and are read
   with a bounded buffer. Oversized files are refused before serialization.
9. **Completion reasons:** Every connection returns its end reason, request
   count, and byte counts. Worker counters distinguish normal closes, parse
   failures, request limits, timeouts, and socket errors.
10. **Failure policy:** Malformed framing gets `400`; unsupported transfer
    coding gets `501`; an active read deadline gets `408`; idle keep-alive,
    peer close, and write failure close silently because another response is
    unnecessary or cannot be delivered reliably.

The governing invariant is:

```text
After one request, know exactly where the next request starts or close the
connection.
```
