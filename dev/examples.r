library(httpserver)

# random_port.R ----

s <- startServer("127.0.0.1", randomPort(), list())
paste0("http://127.0.0.1:", s$getPort())
s$stop()
 
# socket.r

# A WebSocket echo server that listens on port 8080
s <- startServer(
  "0.0.0.0", 8080,
  list(
    onHeaders = function(req) {
      # Print connection headers
      cat(capture.output(str(as.list(req))), sep = "\n")
    },
    onWSOpen = function(ws) {
      cat("Connection opened.\n")

      ws$onMessage(function(binary, message) {
        cat("Server received message:", message, "\n")
        ws$send(message)
      })
      ws$onClose(function() {
        cat("Connection closed.\n")
      })
    }
  )
)

s$stop()

# A very basic application
s <- startServer(
  "0.0.0.0", 5000,
  list(
    call = function(req) {
      list(
        status = 200L,
        headers = list(
          "Content-Type" = "text/html"
        ),
        body = "Hello world!"
      )
    }
  )
)

s$stop()

# An application that serves static assets at the URL paths /assets and /lib
s <- startServer(
  "0.0.0.0", 5000,
  list(
    call = function(req) {
      list(
        status = 200L,
        headers = list(
          "Content-Type" = "text/html"
        ),
        body = "Hello world!"
      )
    },
    staticPaths = list(
      "/assets" = "content/assets/",
      "/lib" = staticPath(
        "content/lib",
        indexhtml = FALSE
      ),
      # This subdirectory of /lib should always be handled by the R code path
      "/lib/dynamic" = excludeStaticPath()
    ),
    staticPathOptions = staticPathOptions(
      indexhtml = TRUE
    )
  )
)

s$stop()

service(1)

# A very basic application
s <- runServer(
  "0.0.0.0", 5000,
  list(
    call = function(req) {
      list(
        status = 200L,
        headers = list(
          "Content-Type" = "text/html"
        ),
        body = "Hello world!"
      )
    }
  )
)

s$stop()

set.seed(100)
result <- rawToBase64(as.raw(runif(19, min = 0, max = 256)))
stopifnot(identical(result, "TkGNDnd7z16LK5/hR2bDqzRbXA=="))

# staticServer.r

if (interactive()) {
 website_dir <- system.file("example-static-site", package = "httpserver")
 runStaticServer(dir = website_dir)
}
