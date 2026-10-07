*This project has been created as part of the 42 curriculum by marregi- and davmoren.*

# webserv

## Description

**webserv** is an HTTP/1.1 server written from scratch in **C++98**, with no external libraries. The goal of the project is to understand how the HTTP protocol works under the hood: how a browser and a server talk over TCP sockets, how requests are parsed, how responses and status codes are built, and how a server can handle many clients at once without blocking.

The server reads an NGINX-inspired configuration file and then:

- listens on a configurable `host:port`,
- handles every client from a **single, non-blocking event loop** built on `epoll`,
- serves static websites (HTML, CSS, JS, images, …) with the right `Content-Type`,
- accepts the `GET`, `POST`, `DELETE` and `HEAD` methods, restricted per route (`405 Method Not Allowed` otherwise),
- runs **CGI** scripts based on file extension (`fork` + `execve`, with the script run from its own directory),
- decodes `Transfer-Encoding: chunked` request bodies,
- enforces a maximum request body size (`413 Payload Too Large`),
- uses custom error pages, or built-in default ones when none are configured,
- generates directory listings (`autoindex`) and serves index files,
- handles HTTP redirections per route,
- closes idle clients after a configurable read timeout, so no request hangs forever.

### Project structure

```
.
├── makefile
├── confile.conf              # default configuration file
├── web/                      # sample static website used for testing
└── src/
    ├── main.cpp              # entry point: loads the config and starts the server
    ├── sockets/              # listening socket, epoll event loop, routing
    ├── request/              # HTTP request parser (headers, body, chunked)
    ├── response/             # HTTP response builder, MIME types
    ├── cgi/                  # CGI environment setup and execution
    └── confile/              # configuration file parser
```

## Instructions

### Requirements

- Linux (the event loop uses `epoll`)
- A C++ compiler (`c++`) supporting `-std=c++98`
- `make`

### Compilation

```sh
make          # builds the ./webserv executable
make clean    # removes object files
make fclean   # removes object files and the executable
make re       # full rebuild
make debug    # rebuild with -g and AddressSanitizer
```

### Execution

```sh
./webserv [configuration_file]
```

If no file is given, the server loads `confile.conf` from the current directory.

Then open a browser at `http://localhost:<port>/` (port `8321` with the default configuration), or test from a terminal:

```sh
curl -v http://localhost:8321/
curl -v -X POST --data "hello" http://localhost:8321/post_body
curl -v -H "Transfer-Encoding: chunked" --data-binary @file.txt http://localhost:8321/post_body
telnet localhost 8321
```

> The paths inside `confile.conf` (`root`, `alias`, `cgi_path`) are absolute. Edit them to match the location of the repository on your machine before running the server.

### Configuration file

The syntax is inspired by the `server` block of NGINX. Example:

```nginx
server {
  listen 8321;
  host 127.0.0.1;
  server_name test;
  root /path/to/webserv/web/;
  index index.html;
  error_page 404 /404.html;
  client_max_body_size 10M;
  client_read_timeout 30;          # seconds, 0 = no timeout

  location /directory {
    alias /path/to/webserv/web/;
    autoindex on;
    allow_methods GET POST;
    cgi_path /usr/bin/python3;
    cgi_ext .py;
  }

  location /post_body {
    allow_methods POST;
    client_max_body_size 100;
  }

  location /old {
    return /new;
  }
}
```

**Server directives**

| Directive | Description |
|---|---|
| `listen <port>` | Port to listen on |
| `host <ip>` | Interface to bind (default `0.0.0.0`) |
| `server_name <name>` | Name of the server |
| `root <path>` | Document root |
| `index <file>` | Default file served for directories (default `index.html`) |
| `error_page <code> <path>` | Custom page for an error status code |
| `client_max_body_size <size>` | Max request body size; accepts `K`, `M`, `G` suffixes |
| `client_read_timeout <sec>` | Idle client timeout in seconds (default `30`, `0` disables it) |

**Location directives**

| Directive | Description |
|---|---|
| `root <path>` / `alias <path>` | Where to look for files for this route |
| `index <file>` | Default file for directories in this route |
| `autoindex on\|off` | Enable or disable directory listing |
| `allow_methods <m> ...` | Accepted methods (default `GET` only) |
| `return <url>` | HTTP redirection (`302 Found`) |
| `cgi_path <interpreter>` | Program used to run CGI scripts |
| `cgi_ext <.ext>` | File extension that triggers the CGI |
| `client_max_body_size <size>` | Body size limit for this route |

## Technical choices

- **One `epoll` instance for all socket I/O.** The listening socket and every client socket are registered in the same `epoll` set. Each socket is watched for reading and switched to writing once its response is ready. No `read`/`recv` or `write`/`send` on a socket happens without a readiness notification first, and `errno` is never checked after an I/O call to decide what to do next.
- **Non-blocking sockets** set with `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- **Per-client sessions** keep a buffer for each connection, so requests that arrive in several TCP packets are put back together before being parsed.
- **`SIGPIPE` is ignored**, so a client that disconnects in the middle of a response cannot crash the server.
- **CGI**: the server builds a standard CGI environment (`REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_LENGTH`, `CONTENT_TYPE`, `SCRIPT_FILENAME`, …), sends the un-chunked body to the script through a pipe, and reads its output until EOF. `fork` is only used for CGI.

## Resources

### HTTP and networking

- [RFC 9110 – HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112 – HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 1945 – HTTP/1.0](https://www.rfc-editor.org/rfc/rfc1945)
- [RFC 3875 – The Common Gateway Interface (CGI) 1.1](https://www.rfc-editor.org/rfc/rfc3875)
- [MDN – HTTP overview](https://developer.mozilla.org/en-US/docs/Web/HTTP/Overview)
- [MDN – HTTP response status codes](https://developer.mozilla.org/en-US/docs/Web/HTTP/Status)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [`epoll(7)` man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [NGINX documentation – `server` and `location` blocks](https://nginx.org/en/docs/http/ngx_http_core_module.html)

### How AI was used

We used AI tools (OpenCode together with the OpenSpec workflow in `openspec/`) as an assistant, not as a replacement for our own work:

- **Research and explanations**: asking about parts of the HTTP RFCs, CGI environment variables, and how `epoll` behaves with non-blocking sockets.
- **Planning**: writing change proposals and specs (OpenSpec) before implementing features such as the configuration parser and the read timeout.
- **Refactoring and review**: splitting long functions into smaller ones and reviewing code for edge cases.
- **Testing**: coming up with test cases (`curl`, `telnet`, chunked bodies, oversized bodies, browser tests).
- **Documentation**: drafting this README.

Every piece of AI-generated content was reviewed, tested, and discussed between the two of us before going into the project, and we can explain all of it.
