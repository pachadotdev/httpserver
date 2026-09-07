# Note that the methods listed for the base server object, WebServer, and
# PipeServer were copied and pasted among all three, with a few additional
# methods added to WebServer and PipeServer. When changes are made in the
# future, make sure that they're duplicated among all three.
#
# These server objects used to be implemented with R6, but are now plain
# environments used as reference-semantics objects, to avoid the R6
# dependency (and its Suggests, e.g. testthat). Each constructor creates a
# `private` environment to hold internal state, and a `self` environment
# whose elements are closures over `private` (and, for methods that need to
# refer back to the object itself, over `self`). This mirrors what R6 does
# internally, just without the extra package.

# Build the set of methods shared by WebServer and PipeServer. `private` is
# an environment that must already contain (or will later contain) the
# fields `appWrapper`, `handle`, and `running`. Returns the `self`
# environment; callers add their own additional fields/methods and a class
# attribute (the class vector should always include "Server").
new_server <- function(private) {
  self <- new.env(parent = emptyenv())

  # Stop a running server
  self$stop <- function() {
    if (!private$running) {
      return(invisible())
    }

    stopServer_(private$handle)
    private$running <- FALSE
    deregisterServer(self)
    invisible()
  }

  # Check if the server is running.
  # This doesn't map exactly to whether the app is running, since the
  # server's uv_loop runs on the background thread. This could be changed
  # to something that queries the C++ side about what's running.
  self$isRunning <- function() {
    private$running
  }

  # Get the static paths for the server, as a list of staticPath() objects
  self$getStaticPaths <- function() {
    if (!private$running) {
      return(NULL)
    }

    getStaticPaths_(private$handle)
  }

  # Set a static path for the server. `...`/`.list` are named arguments
  # where each name is the name of the static path and the value is the
  # path to the directory to serve. If there already exists a static path
  # with the same name, it will be replaced.
  self$setStaticPath <- function(..., .list = NULL) {
    if (!private$running) {
      return(invisible())
    }

    paths <- c(list(...), .list)
    paths <- normalizeStaticPaths(paths)
    invisible(setStaticPaths_(private$handle, paths))
  }

  # Remove a static path by name
  self$removeStaticPath <- function(path) {
    if (!private$running) {
      return(invisible())
    }

    path <- as.character(path)
    invisible(removeStaticPaths_(private$handle, path))
  }

  # Get the static path options for the server: a list of default
  # `staticPathOptions` for the current server. Each static path will use
  # these options by default, but they can be overridden for each static
  # path.
  self$getStaticPathOptions <- function() {
    if (!private$running) {
      return(NULL)
    }

    getStaticPathOptions_(private$handle)
  }

  # Set one or more static path options. `...`/`.list` are named arguments
  # where each name is the name of the static path option and the value is
  # the value to set for that option.
  self$setStaticPathOption <- function(..., .list = NULL) {
    if (!private$running) {
      return(invisible())
    }

    opts <- c(list(...), .list)
    opts <- drop_duplicate_names(opts)
    opts <- normalizeStaticPathOptions(opts)

    unknown_opt_idx <- !(names(opts) %in% names(formals(staticPathOptions)))
    if (any(unknown_opt_idx)) {
      stop("Unknown options: ", paste(names(opts)[unknown_opt_idx], ", "))
    }

    invisible(setStaticPathOptions_(private$handle, opts))
  }

  self
}

# This represents a web server running one application. Multiple servers
# can be running at the same time.
#
# `host` is the host name or IP address to bind the server to. `port` is
# the port number to bind the server to. `app` is an httpserver application
# object as described in startServer(). `quiet`, if TRUE, suppresses output
# from the server.
WebServer <- function(host, port, app, quiet = FALSE) {
  private <- new.env(parent = emptyenv())
  private$appWrapper <- NULL
  private$handle <- NULL
  private$running <- FALSE
  private$host <- host
  private$port <- port

  self <- new_server(private)

  private$appWrapper <- AppWrapper(app)

  private$handle <- makeTcpServer(
    host,
    port,
    private$appWrapper$onHeaders,
    private$appWrapper$onBodyData,
    private$appWrapper$call,
    private$appWrapper$onWSOpen,
    private$appWrapper$onWSMessage,
    private$appWrapper$onWSClose,
    private$appWrapper$staticPaths,
    private$appWrapper$staticPathOptions,
    quiet
  )

  if (is.null(private$handle)) {
    stop("Failed to create server")
  }

  private$running <- TRUE

  # Get the host name or IP address of the server
  self$getHost <- function() {
    private$host
  }

  # Get the port number of the server
  self$getPort <- function() {
    private$port
  }

  class(self) <- c("WebServer", "Server")
  registerServer(self)

  self
}

# This represents a server running one application that listens on a named
# pipe.
#
# `name` is the name of the named pipe to bind the server to. `mask` is the
# mask for the named pipe (if NULL, it defaults to -1). `app` is an
# httpserver application object as described in startServer(). `quiet`, if
# TRUE, suppresses output from the server.
PipeServer <- function(name, mask, app, quiet = FALSE) {
  if (is.null(mask)) {
    mask <- -1
  }

  private <- new.env(parent = emptyenv())
  private$appWrapper <- NULL
  private$handle <- NULL
  private$running <- FALSE
  private$name <- NULL
  private$mask <- mask

  self <- new_server(private)

  private$appWrapper <- AppWrapper(app)

  private$handle <- makePipeServer(
    name,
    mask,
    private$appWrapper$onHeaders,
    private$appWrapper$onBodyData,
    private$appWrapper$call,
    private$appWrapper$onWSOpen,
    private$appWrapper$onWSMessage,
    private$appWrapper$onWSClose,
    private$appWrapper$staticPaths,
    private$appWrapper$staticPathOptions,
    quiet
  )

  # Save the full path. normalizePath must be called after makePipeServer
  private$name <- normalizePath(name)

  if (is.null(private$handle)) {
    stop("Failed to create server")
  }

  # Get the name of the named pipe
  self$getName <- function() {
    private$name
  }

  # Get the mask for the named pipe
  self$getMask <- function() {
    private$mask
  }

  class(self) <- c("PipeServer", "Server")

  self
}


#' Stop a server
#'
#' Given a server object that was returned from a previous invocation of
#' [startServer()] or [startPipeServer()], this closes all
#' open connections for that server and unbinds the port.
#'
#' @param server A server object that was previously returned from
#'   [startServer()] or [startPipeServer()].
#'
#' @seealso [stopAllServers()] to stop all servers.
#'
#' @export
stopServer <- function(server) {
  if (!inherits(server, "Server")) {
    stop("Object must be an object of class Server.")
  }
  server$stop()
}


#' Stop all servers
#'
#' This will stop all applications which were created by
#' [startServer()] or [startPipeServer()].
#'
#' @seealso [stopServer()] to stop a specific server.
#'
#' @export
stopAllServers <- function() {
  lapply(.globals$servers, function(server) {
    server$stop()
  })
  invisible()
}


.globals$servers <- list()

#' List all running httpserver servers
#'
#' This returns a list of all running httpserver server applications.
#'
#' @export
listServers <- function() {
  .globals$servers
}

registerServer <- function(server) {
  .globals$servers[[length(.globals$servers) + 1]] <- server
}

deregisterServer <- function(server) {
  for (i in seq_along(.globals$servers)) {
    if (identical(server, .globals$servers[[i]])) {
      .globals$servers[[i]] <- NULL
      return()
    }
  }

  warning(
    "Unable to deregister server: server not found in list of running servers."
  )
}


#' Stop a running daemonized server in Unix environments (deprecated)
#'
#' This function will be removed in a future release of httpuv. Instead, use
#' [stopServer()].
#'
#' @inheritParams stopServer
#'
#' @export
stopDaemonizedServer <- stopServer
