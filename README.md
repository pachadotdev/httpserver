# tinywebsocket: HTTP and WebSocket server library for R

  <!-- badges: start -->
  [![BuyMeACoffee](https://raw.githubusercontent.com/pachadotdev/buymeacoffee-badges/main/bmc-yellow.svg)](https://www.buymeacoffee.com/pacha)
  <!-- badges: end -->

tinyhttpserver provides low-level socket and protocol support for handling HTTP and WebSocket requests directly from within R. It uses a multithreaded architecture, where I/O is handled on one thread, and the R callbacks are handled on another. This is derived from [httpuv](https://github.com/rstudio/httpuv) with a focus on reducing dependencies and streamlining the build process.

It is primarily intended as a building block for other packages, rather than making it particularly easy to create complete web applications using tinyhttpserver alone. tinyhttpserver is built on top of the [libuv](https://github.com/libuv/libuv) and [http-parser](https://github.com/nodejs/http-parser) libraries, both of which were developed by Joyent, Inc.

## Installing

You can install the development version using **pak**. It is not on CRAN at the present time.

```r
# or if you want to test the development version here
pak::pak("pachadotdev/tinyhttpserver")
```

tinyhttpserver may optionally be built using a `libuv` system package, which you can install prior to installing the R package. It goes by different names on different package managers: `libuv1-dev` (deb), `libuv-devel` (rpm), `libuv` (brew). Version 1.43 or greater is required. If `libuv` is not found on the system, it will be built from source along with the R package.

## Basic Usage

This is a basic web server that listens on port 8080 and responds to HTTP requests with a web page containing the current system time and the path of the request:

```R
library(tinyhttpserver)

s <- startServer(host = "0.0.0.0", port = 8080,
  app = list(
    call = function(req) {
      body <- paste0("Time: ", Sys.time(), "<br>Path requested: ", req$PATH_INFO)
      list(
        status = 200L,
        headers = list('Content-Type' = 'text/html'),
        body = body
      )
    }
  )
)
```

Note that when `host` is 0.0.0.0, it listens on all network interfaces. If `host` is 127.0.0.1, it will only listen to connections from the local host.

The `startServer()` function takes an _app object_, which is a named list with functions that are invoked in response to certain events. In the example above, the list contains a function `call`. This function is invoked when a complete HTTP request is received by the server, and it is passed an environment object `req`, which contains information about HTTP request. `req$PATH_INFO` is the path requested (if the request was for http://127.0.0.1:8080/foo, it would be `"/foo"`).

The `call` function is expected to return a list containing `status`, `headers`, and `body`. That list will be transformed into a HTTP response and sent to the client.

To stop the server:

```R
s$stop()
```

Or, to stop all running tinyhttpserver servers:

```R
stopAllServers()
```

### Static paths

A tinyhttpserver server application can serve up files on disk. This happens entirely within the I/O thread, so doing so will not block or be blocked by activity in the main R thread.

To serve a path, use `staticPaths` in the app. This will serve the `www/` subdirectory of the current directory (from when `startServer` is called) as the root of the web path:

```R
s <- startServer("0.0.0.0", 8080,
  app = list(
    staticPaths = list("/" = "www/")
  )
)
```

By default, if a file named `index.html` exists in the directory, it will be served when `/` is requested.

`staticPaths` can be combined with `call`. In this example, the web paths `/assets` and `/lib` are served from disk, but requests for any other paths go through the `call` function.

```R
s <- startServer("0.0.0.0", 8080,
  list(
    call = function(req) {
      list(
        status = 200L,
        headers = list(
          'Content-Type' = 'text/html'
        ),
        body = "Hello world!"
      )
    },
    staticPaths = list(
      "/assets" = "content/assets/",
      # Don't use index.html for /lib
      "/lib" = staticPath("content/lib", indexhtml = FALSE)
    )
  )
)
```

### WebSocket server

tinyhttpserver also can handle WebSocket connections. For example, this app acts as a WebSocket echo server:

```R
s <- startServer("127.0.0.1", 8080,
  list(
    onWSOpen = function(ws) {
      # The ws object is a WebSocket object
      cat("Server connection opened.\n")

      ws$onMessage(function(binary, message) {
        cat("Server received message:", message, "\n")
        ws$send(message)
      })
      ws$onClose(function() {
        cat("Server connection closed.\n")
      })
    }
  )
)
```

## Debugging builds

tinyhttpserver can be built with debugging options enabled. This can be done by uncommenting these lines in src/Makevars, and then installing. The first one enables thread assertions, to ensure that code is running on the correct thread; if not. The second one enables tracing statements: tinyhttpserver will print lots of messages when various events occur.

```
PKG_CPPFLAGS += -DDEBUG_THREAD -UNDEBUG
PKG_CPPFLAGS += -DDEBUG_TRACE
```
