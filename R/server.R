# Server objects are environments with closures over a private state
# environment. This provides reference semantics without a class dependency.

# build the set of methods shared by web_server and pipe_server. `private` is
# an environment that must already contain (or will later contain) the
# fields `app_wrapper`, `handle`, and `running`. returns the `self`
# environment; callers add their own additional fields/methods and a class
# attribute (the class vector should always include "server").
new_server <- function(private) {
  self <- new.env(parent = emptyenv())

  # stop a running server
  self$stop <- function() {
    if (!private$running) {
      return(invisible())
    }

    stop_server_(private$handle)
    private$running <- FALSE
    deregister_server(self)
    invisible()
  }

  # check if the server is running.
  # this doesn't map exactly to whether the app is running, since the
  # server's uv_loop runs on the background thread. this could be changed
  # to something that queries the c++ side about what's running.
  self$is_running <- function() {
    private$running
  }

  # get the static paths for the server, as a list of static_path() objects
  self$get_static_paths <- function() {
    if (!private$running) {
      return(NULL)
    }

    get_static_paths_(private$handle)
  }

  # set a static path for the server. `...`/`.list` are named arguments
  # where each name is the name of the static path and the value is the
  # path to the directory to serve. if there already exists a static path
  # with the same name, it will be replaced.
  self$set_static_path <- function(..., .list = NULL) {
    if (!private$running) {
      return(invisible())
    }

    paths <- c(list(...), .list)
    paths <- normalize_static_paths(paths)
    invisible(set_static_paths_(private$handle, paths))
  }

  # remove a static path by name
  self$remove_static_path <- function(path) {
    if (!private$running) {
      return(invisible())
    }

    path <- as.character(path)
    invisible(remove_static_paths_(private$handle, path))
  }

  # get the static path options for the server: a list of default
  # `static_path_options` for the current server. each static path will use
  # these options by default, but they can be overridden for each static
  # path.
  self$get_static_path_options <- function() {
    if (!private$running) {
      return(NULL)
    }

    get_static_path_options_(private$handle)
  }

  # set one or more static path options. `...`/`.list` are named arguments
  # where each name is the name of the static path option and the value is
  # the value to set for that option.
  self$set_static_path_option <- function(..., .list = NULL) {
    if (!private$running) {
      return(invisible())
    }

    opts <- c(list(...), .list)
    opts <- drop_duplicate_names(opts)
    opts <- normalize_static_path_options(opts)

    unknown_opt_idx <- !(names(opts) %in% names(formals(static_path_options)))
    if (any(unknown_opt_idx)) {
      stop("unknown options: ", paste(names(opts)[unknown_opt_idx], ", "))
    }

    invisible(set_static_path_options_(private$handle, opts))
  }

  self
}

# this represents a web server running one application. multiple servers
# can be running at the same time.
#
# `host` is the host name or ip address to bind the server to. `port` is
# the port number to bind the server to. `app` is an httpserver application
# object as described in start_server(). `quiet`, if TRUE, suppresses output
# from the server.
web_server <- function(host, port, app, quiet = FALSE) {
  private <- new.env(parent = emptyenv())
  private$app_wrapper <- NULL
  private$handle <- NULL
  private$running <- FALSE
  private$host <- host
  private$port <- port

  self <- new_server(private)

  private$app_wrapper <- app_wrapper(app)

  private$handle <- make_tcp_server(
    host,
    port,
    private$app_wrapper$on_headers,
    private$app_wrapper$on_body_data,
    private$app_wrapper$call,
    private$app_wrapper$on_wsopen,
    private$app_wrapper$on_wsmessage,
    private$app_wrapper$on_wsclose,
    private$app_wrapper$static_paths,
    private$app_wrapper$static_path_options,
    quiet
  )

  if (is.null(private$handle)) {
    stop("failed to create server")
  }

  private$running <- TRUE

  # get the host name or ip address of the server
  self$get_host <- function() {
    private$host
  }

  # get the port number of the server
  self$get_port <- function() {
    private$port
  }

  class(self) <- c("web_server", "server")
  register_server(self)

  self
}

# this represents a server running one application that listens on a named
# pipe.
#
# `name` is the name of the named pipe to bind the server to. `mask` is the
# mask for the named pipe (if NULL, it defaults to -1). `app` is an
# httpserver application object as described in start_server(). `quiet`, if
# TRUE, suppresses output from the server.
pipe_server <- function(name, mask, app, quiet = FALSE) {
  if (is.null(mask)) {
    mask <- -1
  }

  private <- new.env(parent = emptyenv())
  private$app_wrapper <- NULL
  private$handle <- NULL
  private$running <- FALSE
  private$name <- NULL
  private$mask <- mask

  self <- new_server(private)

  private$app_wrapper <- app_wrapper(app)

  private$handle <- make_pipe_server(
    name,
    mask,
    private$app_wrapper$on_headers,
    private$app_wrapper$on_body_data,
    private$app_wrapper$call,
    private$app_wrapper$on_wsopen,
    private$app_wrapper$on_wsmessage,
    private$app_wrapper$on_wsclose,
    private$app_wrapper$static_paths,
    private$app_wrapper$static_path_options,
    quiet
  )

  # save the full path. normalizePath must be called after make_pipe_server
  if (is.null(private$handle)) {
    stop("failed to create server")
  }

  private$name <- normalizePath(name)
  private$running <- TRUE

  # get the name of the named pipe
  self$get_name <- function() {
    private$name
  }

  # get the mask for the named pipe
  self$get_mask <- function() {
    private$mask
  }

  class(self) <- c("pipe_server", "server")
  register_server(self)

  self
}


#' stop a server
#'
#' given a server object that was returned from a previous invocation of
#' [start_server()] or [start_pipe_server()], this closes all
#' open connections for that server and unbinds the port.
#'
#' @param server a server object that was previously returned from
#'   [start_server()] or [start_pipe_server()].
#'
#' @return no return value, called for its side effect of stopping the server.
#'
#' @seealso [stop_all_servers()] to stop all servers.
#'
#' @export
stop_server <- function(server) {
  if (!inherits(server, "server")) {
    stop("object must be an object of class server.")
  }
  server$stop()
}


#' stop all servers
#'
#' this will stop all applications which were created by
#' [start_server()] or [start_pipe_server()].
#'
#' @return no return value, called for its side effect of stopping all running
#'   servers.
#'
#' @seealso [stop_server()] to stop a specific server.
#'
#' @export
stop_all_servers <- function() {
  lapply(.globals$servers, function(server) {
    server$stop()
  })
  invisible()
}


.globals$servers <- list()

#' list all running httpserver servers
#'
#' this returns a list of all running httpserver server applications.
#'
#' @return a list of currently running server objects. the list is empty when
#'   no servers are running.
#'
#' @export
list_servers <- function() {
  .globals$servers
}

register_server <- function(server) {
  .globals$servers[[length(.globals$servers) + 1]] <- server
}

deregister_server <- function(server) {
  for (i in seq_along(.globals$servers)) {
    if (identical(server, .globals$servers[[i]])) {
      .globals$servers[[i]] <- NULL
      return()
    }
  }

  warning(
    "unable to deregister server: server not found in list of running servers."
  )
}
