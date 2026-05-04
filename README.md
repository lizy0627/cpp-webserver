# CppWebServer

A small C++17 HTTP server project.

## Static Files

Static files are served from the `www` directory.

- `GET /` returns `www/index.html`
- `GET /index.html` returns `www/index.html`
- Missing files return `404 Not Found`
- Requests containing unsafe path segments such as `..` are rejected

Supported content types:

- `.html` -> `text/html; charset=utf-8`
- `.css` -> `text/css; charset=utf-8`
- `.js` -> `application/javascript; charset=utf-8`
- `.txt` -> `text/plain; charset=utf-8`
- other files -> `application/octet-stream`

## Epoll I/O Model

The server uses Linux `epoll` for I/O multiplexing.

- The listening socket is non-blocking.
- `epoll_wait` drives the main event loop.
- When the listening socket is readable, the server accepts all pending clients.
- Client sockets are non-blocking and registered with epoll.
- Readable client sockets are passed to the existing HTTP request handling logic.
- `EAGAIN` and `EWOULDBLOCK` are handled without treating them as bad requests.
