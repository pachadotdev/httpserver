[![BuyMeACoffee](https://raw.githubusercontent.com/pachadotdev/buymeacoffee-badges/main/bmc-yellow.svg)](https://buymeacoffee.com/pacha)

# httpserver: HTTP and WebSocket server library for R

`httpserver` provides low-level HTTP, WebSocket, and socket support for R
applications. Network I/O runs on a background thread while application
callbacks are scheduled on R's event loop. Static files are served directly
from the I/O thread, so they do not wait for R callbacks.

The package is intended as a building block for other packages. It uses
`libuv`, `http-parser`, `cpp4r`, and `later2`.

## Installing

The development version can be installed with **pak**:

```r
pak::pak("pachadotdev/httpserver")
```

`httpserver` bundles the required `libuv` sources when a suitable system
library is not available.

## Basic usage

```r
library(httpserver)

s <- start_server(
  host = "127.0.0.1",
  port = 8080,
  app = list(
    call = function(req) {
      list(
        status = 200L,
        headers = list("content-type" = "text/plain"),
        body = paste0("Path requested: ", req$path_info)
      )
    }
  )
)

s$stop()
```

Use `host = "0.0.0.0"` to listen on all IPv4 interfaces. Applications return
an HTTP response containing `status`, `headers`, and `body`. A response body
may be a character string or a raw vector.

`run_server()` is the blocking convenience interface. For a background server,
use `start_server()` and later call `$stop()`, `stop_server()`, or
`stop_all_servers()`.

## Static files

Static paths are served in the I/O thread and can be configured with
`static_path()` and `static_path_options()`:

```r
s <- start_server(
  "127.0.0.1",
  8080,
  app = list(
    static_paths = list(
      "/" = static_path("www"),
      "/assets" = static_path("public/assets", index_html = FALSE)
    ),
    static_path_options = static_path_options(
      headers = list("cache-control" = "public, max-age=3600")
    )
  )
)
```

Use `exclude_static_path()` to route a subpath back to the R application.
`run_static_server()` provides a shorter interface for serving one directory.

## WebSockets

Register `on_wsopen` to receive a `web_socket` object. Messages can be sent
with `$send()` and callbacks can be registered with `$on_message()` and
`$on_close()`:

```r
s <- start_server(
  "127.0.0.1",
  8080,
  app = list(
    on_wsopen = function(ws) {
      ws$on_message(function(binary, message) {
        ws$send(message)
      })
    }
  )
)
```

## Development

Run `make update-libuv VERSION=1.53.0` to update the bundled `libuv` sources.
The full test suite uses `tinytest`; HTTP integration tests additionally use
`curl`.
