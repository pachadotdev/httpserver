# Rook-compatible request input and error streams.
input_stream <- function(conn, length) {
  private <- new.env(parent = emptyenv())
  private$conn <- conn
  private$length <- length
  seek(private$conn, 0)

  self <- new.env(parent = emptyenv())

  self$read_lines <- function(n = -1L) {
    readLines(private$conn, n, warn = FALSE)
  }

  self$read <- function(l = -1L) {
    # l < 0 means read all remaining bytes
    if (l < 0) {
      l <- private$length - seek(private$conn)
    }

    if (l == 0) {
      return(raw())
    } else {
      return(readBin(private$conn, raw(), l))
    }
  }

  self$rewind <- function() {
    seek(private$conn, 0)
  }

  class(self) <- "input_stream"
  self
}

null_input_stream <- function() {
  self <- new.env(parent = emptyenv())

  self$read_lines <- function(n = -1L) {
    character()
  }
  self$read <- function(l = -1L) {
    raw()
  }
  self$rewind <- function() invisible()
  self$close <- function() invisible()

  class(self) <- "null_input_stream"
  self
}
null_input_stream <- null_input_stream()

# implementation of rook error stream
error_stream <- function() {
  self <- new.env(parent = emptyenv())

  self$cat <- function(..., sep = " ", fill = FALSE, labels = NULL) {
    base::cat(..., sep = sep, fill = fill, labels = labels, file = stderr())
  }
  self$flush <- function() {
    base::flush(stderr())
  }

  class(self) <- "error_stream"
  self
}
std_err_stream <- error_stream()

rook_call <- function(func, req, data = NULL, data_length = -1L) {
  # break the processing into two parts: first, the computation with func();
  # second, the preparation of the response object.
  compute <- function() {
    input_stream <- if (is.null(data)) {
      null_input_stream
    } else {
      input_stream(data, data_length)
    }

    req$rook.input <- input_stream

    req$rook.errors <- std_err_stream

    req$httpserver.version <- httpserver_version()

    # these appear to be required for rook multipart parsing to work
    if (!is.null(req$http_content_type)) {
      req$content_type <- req$http_content_type
    }
    if (!is.null(req$http_content_length)) {
      req$content_length <- req$http_content_length
    }

    # func() may return a regular value or a promise.
    func(req)
  }

  prepare_response <- function(resp) {
    if (is.null(resp) || length(resp) == 0) {
      return(NULL)
    }

    # if headers is an empty unnamed list, convert to named list so that
    # the c++ code won't error.
    if (
      is.null(resp$headers) ||
        (length(resp$headers) == 0 && is.null(names(resp$headers)))
    ) {
      resp$headers <- named_list()
    }

    if (!is.list(resp$headers) || is.null(names(resp$headers)) ||
        any(!nzchar(names(resp$headers)))) {
      stop("response headers must be a named list.")
    }

    # Reject response splitting before values reach the HTTP writer.
    if (any(grepl("[\r\n]", names(resp$headers), fixed = FALSE)) ||
        any(vapply(resp$headers, function(x) {
          any(grepl("[\r\n]", paste(x), fixed = FALSE))
        }, logical(1)))) {
      stop("response headers must not contain carriage returns or line feeds.")
    }

    # Coerce all headers to character after validating their names and values.
    resp$headers <- lapply(resp$headers, paste)

    if ("file" %in% names(resp$body)) {
      filename <- resp$body[["file"]]
      if (!is.character(filename) || length(filename) != 1L ||
          is.na(filename) || !nzchar(filename)) {
        stop("response body `file` must be a non-empty path.")
      }
      owned <- FALSE
      if ("owned" %in% names(resp$body)) {
        owned <- resp$body$owned
        if (!is.logical(owned) || length(owned) != 1L || is.na(owned)) {
          stop("response body `owned` must be TRUE or FALSE.")
        }
      }

      resp$body <- NULL
      resp$body_file <- filename
      resp$body_file_owned <- owned
    }
    resp
  }

  on_error <- function(e) {
    list(
      status = 500L,
      headers = list(
        "content-type" = "text/plain; charset=utf-8"
      ),
      body = charToRaw(enc2utf8(
        paste("error:", conditionMessage(e), collapse = "\n")
      ))
    )
  }

  # first, run the compute function. if it errored, return error response.
  # then check if it returned a promise. if so, promisify the next step.
  # if not, run the next step immediately.
  compute_error <- NULL
  response <- tryCatch(
    compute(),
    error = function(e) compute_error <<- e
  )
  if (!is.null(compute_error)) {
    return(on_error(compute_error))
  }

  if (is.promise(response)) {
    then(response, onFulfilled = prepare_response, onRejected = on_error)
  } else {
    tryCatch(prepare_response(response), error = on_error)
  }
}

app_wrapper <- function(app) {
  private <- new.env(parent = emptyenv())
  private$app <- NULL # list defining app
  private$wsconns <- NULL # an environment containing websocket connections
  private$supports_on_headers <- NULL # logical

  self <- new.env(parent = emptyenv())
  self$static_paths <- NULL # list of static paths
  self$static_path_options <- NULL # static_path_options object

  if (is.function(app)) {
    private$app <- list(call = app)
  } else {
    private$app <- app
  }

  # private$app$on_headers can error (e.g. if private$app is a reference class)
  private$supports_on_headers <- isTRUE(try(
    !is.null(private$app$on_headers),
    silent = TRUE
  ))

  # static_paths are saved in a field on this object, because they are read
  # from the app object only during initialization. this is the only time
  # it makes sense to read them from the app object, since they're
  # subsequently used on the background thread, and for performance
  # reasons it can't call back into r. note that if the app object is a
  # reference object and app$static_paths is changed later, it will have no
  # effect on the behavior of the application.
  #
  # if private$app is a reference class, accessing private$app$static_paths
  # can error if not present. saving here in a separate var because r cmd
  # check complains if you compare class(x) with a string.
  try_obj_class <- class(try(private$app$static_paths, silent = TRUE))
  if (try_obj_class == "try-error" || is.null(private$app$static_paths)) {
    self$static_paths <- list()
  } else {
    self$static_paths <- normalize_static_paths(private$app$static_paths)
  }

  try_obj_class <- class(try(private$app$static_path_options, silent = TRUE))
  if (
    try_obj_class == "try-error" || is.null(private$app$static_path_options)
  ) {
    # use defaults
    self$static_path_options <- static_path_options()
  } else if (inherits(private$app$static_path_options, "static_path_options")) {
    self$static_path_options <- normalize_static_path_options(
      private$app$static_path_options
    )
  } else {
    stop("static_path_options must be an object of class static_path_options.")
  }

  private$wsconns <- new.env(parent = emptyenv())

  self$on_headers <- function(req) {
    if (!private$supports_on_headers) {
      return(NULL)
    }

    rook_call(private$app$on_headers, req)
  }

  self$on_body_data <- function(req, bytes) {
    if (is.null(req$.body_data)) {
      req$.body_data <- file(open = "w+b", encoding = "utf-8")
    }
    writeBin(bytes, req$.body_data)
  }

  self$call <- function(req, cpp_callback) {
    # the cpp_callback is an external pointer to a c++ function that writes
    # the response.

    resp <- if (is.null(private$app$call)) {
      list(
        status = 404L,
        headers = list(
          "content-type" = "text/plain"
        ),
        body = "404 not found\n"
      )
    } else {
      rook_call(private$app$call, req, req$.body_data, seek(req$.body_data))
    }
    # note: rook_call() should never throw error because all the work is
    # wrapped in tryCatch().

    clean_up <- function() {
      if (!is.null(req$.body_data)) {
        close(req$.body_data)
      }
      req$.body_data <- NULL
    }

    if (is.promise(resp)) {
      # slower path if resp is a promise
      resp <- then(resp, function(value) invoke_cpp_callback(value, cpp_callback))
      finally(resp, clean_up)
    } else {
      # fast path if resp is a regular value
      on.exit(clean_up())
      invoke_cpp_callback(resp, cpp_callback)
    }

    invisible()
  }

  self$on_wsopen <- function(handle, req) {
    ws <- web_socket(handle, req)
    private$wsconns[[wsconn_address(handle)]] <- ws
    result <- try(private$app$on_wsopen(ws))

    # An unexpected callback error cannot be returned to the native event
    # loop, so close the connection with an internal-error status.
    if (inherits(result, "try-error")) {
      ws$close(1011, "error in on_wsopen")
    }
  }

  self$on_wsmessage <- function(handle, binary, message) {
    for (handler in private$wsconns[[wsconn_address(
      handle
    )]]$message_callbacks) {
      result <- try(handler(binary, message))
      if (inherits(result, "try-error")) {
        private$wsconns[[wsconn_address(handle)]]$close(
          1011,
          "error executing on_wsmessage"
        )
        return()
      }
    }
  }

  self$on_wsclose <- function(handle) {
    ws <- private$wsconns[[wsconn_address(handle)]]
    ws$handle <- NULL
    rm(list = wsconn_address(handle), envir = private$wsconns)

    for (handler in ws$close_callbacks) {
      handler()
    }
  }

  class(self) <- "app_wrapper"
  self
}

#' @title web_socket class
#' @description
#' a `web_socket` object represents a single web_socket connection. the
#' object can be used to send messages and close the connection, and to receive
#' notifications when messages are received or the connection is closed.
#'
#' @details
#' This server-side class is different from the client-side connection
#' provided by the package named `websocket`.
#'
#' web_socket objects should never be created directly. they are obtained by
#' passing an `on_wsopen` function to [start_server()].
#'
#' @return an environment of class `web_socket` containing the connection
#'   handle, request information, callback registries, and methods for sending
#'   messages or closing the connection.
#'
#' @export
#' @examples
#' # a web_socket echo server that listens on port 8080
#' s <- start_server(
#'   "0.0.0.0", 8080,
#'   list(
#'     on_headers = function(req) {
#'       # print connection headers
#'       cat(capture.output(str(as.list(req))), sep = "\n")
#'     },
#'     on_wsopen = function(ws) {
#'       cat("connection opened.\n")
#'
#'       ws$on_message(function(binary, message) {
#'         cat("server received message:", message, "\n")
#'         ws$send(message)
#'       })
#'       ws$on_close(function() {
#'         cat("connection closed.\n")
#'       })
#'     }
#'   )
#' )
#'
#' s$stop()
#'
#' @param handle an c++ web_socket handle.
#' @param req the rook request environment that opened the connection.
#'
#' @details
#' the returned object has the following fields and methods:
#' * `handle`: the server handle.
#' * `request`: the rook request environment that opened the connection.
#'   this can be used to inspect http headers, for example.
#' * `message_callbacks`: a list of callback functions that will be invoked
#'   when a message is received on this connection.
#' * `close_callbacks`: a list of callback functions that will be invoked when
#'   the connection is closed.
#' * `on_message(func)`: registers a callback function that will be invoked
#'   whenever a message is received on this connection. the callback
#'   function will be invoked with two arguments: the first is `TRUE` if the
#'   message is binary and `FALSE` if it is text; the second is either a raw
#'   vector (if the message is binary) or a character vector.
#' * `on_close(func)`: registers a callback function that will be invoked when
#'   the connection is closed.
#' * `send(message)`: begins sending the given message over the websocket.
#'   `message` is either a raw vector, or a single-element character vector
#'   that is encoded in utf-8.
#' * `close(code = 1000L, reason = "")`: closes the websocket connection.
#'   `code` is an integer that indicates the web_socket close code
#'   (https://developer.mozilla.org/en-us/docs/web/api/web_socket/close#code).
#'   `reason` is a concise human-readable prose explanation for the closure
#'   (https://developer.mozilla.org/en-us/docs/web/api/web_socket/close#reason).
#' @export
web_socket <- function(handle, req) {
  self <- new.env(parent = emptyenv())

  self$handle <- handle
  self$request <- req
  self$message_callbacks <- list()
  self$close_callbacks <- list()

  self$on_message <- function(func) {
    self$message_callbacks <- c(self$message_callbacks, func)
  }

  self$on_close <- function(func) {
    self$close_callbacks <- c(self$close_callbacks, func)
  }

  self$send <- function(message) {
    if (is.null(self$handle)) {
      return()
    }

    if (is.raw(message)) {
      send_ws_message(self$handle, TRUE, message)
      return(invisible())
    }

    if (!is.character(message) || length(message) != 1L || is.na(message)) {
      stop("`message` must be a raw vector or a single non-NA character string.")
    }

    send_ws_message(self$handle, FALSE, enc2utf8(message))
    invisible()
  }

  self$close <- function(code = 1000L, reason = "") {
    if (is.null(self$handle)) {
      return()
    }

    # Keep the close code within the unsigned 16-bit wire representation.
    if (
      !is.numeric(code) ||
        length(code) != 1L ||
        is.na(code) ||
        !is.finite(code)
    ) {
      stop("`code` must be a single non-NA number.")
    }
    code <- as.integer(code)
    if (code < 0 || code > 2^16 - 1) {
      warning("invalid websocket error code: ", code)
      code <- 1001L
    }
    if (!is.character(reason) || length(reason) != 1L || is.na(reason)) {
      stop("`reason` must be a single non-NA character string.")
    }
    reason <- iconv(reason, to = "utf-8")
    if (is.na(reason)) {
      stop("`reason` must be valid UTF-8.")
    }
    if (nchar(reason, type = "bytes") > 123L) {
      stop("`reason` must be at most 123 bytes in UTF-8.")
    }

    close_ws(self$handle, code, reason)
    self$handle <- NULL
  }

  class(self) <- "web_socket"
  self
}

#' create an http/web_socket server
#'
#' creates an http/web_socket server on the specified host and port.
#'
#' @param host a string that is a valid ipv4 address that is owned by this
#'   server, or `"0.0.0.0"` to listen on all ip addresses.
#' @param port a number or integer that indicates the server port that should be
#'   listened on. note that on most unix-like systems including linux and mac_os,
#'   port numbers smaller than 1024 require root privileges.
#' @param app a collection of functions that define your application. see
#'   details.
#' @param quiet if `TRUE`, suppress error messages from starting app.
#' @return a handle for this server that can be passed to
#'   [stop_server()] to shut the server down.
#'
#' @details `start_server` binds the specified port and listens for
#'   connections on an thread running in the background. this background thread
#'   handles the i/o, and when it receives a http request, it will schedule a
#'   call to the user-defined r functions in `app` to handle the request.
#'   this scheduling is done with [later2::later()]. when the r call
#'   stack is empty -- in other words, when an interactive r session is sitting
#'   idle at the command prompt -- r will automatically run the scheduled calls.
#'   however, if the call stack is not empty -- if r is evaluating other r code
#'   -- then the callbacks will not execute until either the call stack is
#'   empty, or the [later2::run_now()] function is called. this
#'   function tells r to execute any callbacks that have been scheduled by
#'   [later2::later()]. the [service()] function is
#'   essentially a wrapper for [later2::run_now()].
#'
#'   if the port cannot be bound (most likely due to permissions or because it
#'   is already bound), an error is raised.
#'
#'   the application can also specify paths on the filesystem which will be
#'   served from the background thread, without invoking `$call()` or
#'   `$on_headers()`. files served this way will be only use a c++ code,
#'   which is faster than going through r, and will not be blocked when r code
#'   is executing. this can greatly improve performance when serving static
#'   assets.
#'
#'   the `app` parameter is where your application logic will be provided
#'   to the server. this can be a list, environment, or reference class that
#'   contains the following methods and fields:
#'
#'   \describe{
#'     \item{`call(req)`}{process the given http request, and return an
#'     http response (see response values). this method should be implemented in
#'     accordance with the [rook](https://github.com/jeffreyhorner/rook/)
#'     specification. note that httpserver augments `req` with an additional
#'     item, `req$headers`, which is a named character vector of request
#'     headers.}
#'     \item{`on_headers(req)`}{optional. similar to `call`, but occurs
#'     when headers are received. return `NULL` to continue normal
#'     processing of the request, or a rook response to send that response,
#'     stop processing the request, and ask the client to close the connection.
#'     (this can be used to implement upload size limits, for example.)}
#'     \item{`on_wsopen(ws)`}{called back when a web_socket connection is established.
#'     the given object can be used to be notified when a message is received from
#'     the client, to send messages to the client, etc. see [web_socket()].}
#'     \item{`static_paths`}{
#'       a named list of paths that will be served without invoking
#'       `call()` or `on_headers`. the name of each one is the url
#'       path, and the value is either a string referring to a local path, or an
#'       object created by the [static_path()] function.
#'     }
#'     \item{`static_path_options`}{
#'       a set of default options to use when serving static paths. if
#'       not set or `NULL`, then it will use the result from calling
#'       [static_path_options()] with no arguments.
#'     }
#'   }
#'
#'   the `start_pipe_server` variant can be used instead of
#'   `start_server` to listen on a unix domain socket or named pipe rather
#'   than a tcp socket (this is not common).
#'
#' @section response values:
#'
#' the `call` function is expected to return a list containing the
#' following, which are converted to an http response and sent to the client:
#'
#' \describe{
#'   \item{`status`}{a numeric http status code, e.g. `200` or
#'     `404L`.}
#'
#'   \item{`headers`}{a named list of http headers and their values, as
#'     strings. this can also be missing, an empty list, or `NULL`, in which
#'     case no headers (other than the `date` and `content-length`
#'     headers, as required) will be added.}
#'
#'   \item{`body`}{a string (or `raw` vector) to be sent as the body
#'     of the http response. this can also be omitted or set to `NULL` to
#'     avoid sending any body, which is useful for http `1xx`, `204`,
#'     and `304` responses, as well as responses to `head` requests.}
#' }
#'
#' @return a [web_server()] or [pipe_server()] object.
#'
#' @seealso [stop_server()], [run_server()],
#'   [list_servers()], [stop_all_servers()].
#' @aliases start_pipe_server
#'
#' @examples
#' s <- start_server(
#'   "0.0.0.0", 5000,
#'   list(
#'     call = function(req) {
#'       list(
#'         status = 200L,
#'         headers = list(
#'           "content-type" = "text/html"
#'         ),
#'         body = "hello world!"
#'       )
#'     }
#'   )
#' )
#'
#' s$stop()
#'
#' # an application that serves static assets at the url paths /assets and /lib
#' content_dir <- tempfile("httpserver-content-")
#' dir.create(file.path(content_dir, "assets"), recursive = TRUE)
#' dir.create(file.path(content_dir, "lib"), recursive = TRUE)
#' on.exit(unlink(content_dir, recursive = TRUE), add = TRUE)
#'
#' s <- start_server(
#'   "0.0.0.0", random_port(),
#'   list(
#'     call = function(req) {
#'       list(
#'         status = 200L,
#'         headers = list(
#'           "content-type" = "text/html"
#'         ),
#'         body = "hello world!"
#'       )
#'     },
#'     static_paths = list(
#'       "/assets" = file.path(content_dir, "assets"),
#'       "/lib" = static_path(
#'         file.path(content_dir, "lib"),
#'         index_html = FALSE
#'       ),
#'       # this subdirectory of /lib should always be handled by the r code path
#'       "/lib/dynamic" = exclude_static_path()
#'     ),
#'     static_path_options = static_path_options(
#'       index_html = TRUE
#'     )
#'   )
#' )
#'
#' s$stop()
#'
#' @export
start_server <- function(host, port, app, quiet = FALSE) {
  web_server(host, port, app, quiet)
}

#' @param name a string that indicates the path for the domain socket (on
#'   unix-like systems) or the name of the named pipe (on windows).
#' @param mask if non-`NULL` and non-negative, this numeric value is used
#'   to temporarily modify the process's umask while the domain socket is being
#'   created. to ensure that only root can access the domain socket, use
#'   `strtoi("777", 8)`; or to allow owner and group read/write access, use
#'   `strtoi("117", 8)`. if the value is `NULL` then the process's
#'   umask is left unchanged. (this parameter has no effect on windows.)
#' @rdname start_server
#' @export
start_pipe_server <- function(name, mask, app, quiet = FALSE) {
  pipe_server(name, mask, app, quiet)
}

#' process requests
#'
#' process http requests and web_socket messages. if there is nothing on r's call
#' stack -- if r is sitting idle at the command prompt -- it is not necessary to
#' call this function, because requests will be handled automatically. however,
#' if r is executing code, then requests will not be handled until either the
#' call stack is empty, or this function is called (or alternatively,
#' [later2::run_now()] is called).
#'
#' this function simply calls [later2::run_now()], so if your
#' application schedules any [later2::later()] callbacks, they will be
#' invoked.
#'
#' @return the logical value `TRUE`, invisibly, after processing requests for
#'   the requested interval.
#'
#' @param timeout_ms approximate number of milliseconds to run before returning.
#'   it will return this duration has elapsed. if 0 or inf, then the function
#'   will continually process requests without returning unless an error occurs.
#'   if na, performs a non-blocking run without waiting.
#'
#' @examples
#' service(1)
#'
#' @export
service <- function(timeout_ms = ifelse(interactive(), 100, 1000)) {
  # in all cases, call `run_now` with `all = FALSE` so that if there is a lot of
  # incoming traffic (relative to the time it takes to process it) we give the
  # owning event loop opportunities to do housekeeping in between httpserver related
  # callbacks.

  if (is.na(timeout_ms)) {
    # na means to run non-blocking
    run_now(0, all = FALSE)
  } else if (timeout_ms == 0 || timeout_ms == Inf) {
    .globals$paused <- FALSE
    # in interactive sessions, wait for a max of 0.1 seconds for better
    # responsiveness when the user sends an interrupt (like esc in rstudio.)
    check_time <- if (interactive()) 0.1 else Inf
    while (!.globals$paused) {
      run_now(check_time, all = FALSE)
    }
  } else {
    # no need to check for .globals$paused because if run_now() executes
    # anything, it will return immediately.
    run_now(timeout_ms / 1000, all = FALSE)
  }

  # some code expects service() to return TRUE (#123)
  TRUE
}

#' run a server
#'
#' this is a convenience function that provides a simple way to call
#' [start_server()], [service()], and
#' [stop_server()] in the correct sequence. it does not return unless
#' interrupted or an error occurs.
#'
#' if you have multiple hosts and/or ports to listen on, call the individual
#' functions instead of `run_server`.
#'
#' @param host a string that is a valid ipv4 or ipv6 address that is owned by
#'   this server, which the application will listen on. `"0.0.0.0"`
#'   represents all ipv4 addresses and `"::/0"` represents all ipv6
#'   addresses.
#' @param port a number or integer that indicates the server port that should be
#'   listened on. note that on most unix-like systems including linux and mac_os,
#'   port numbers smaller than 1024 require root privileges.
#' @param app a collection of functions that define your application. see
#'   [start_server()].
#'
#' @return normally does not return; after interruption, returns the logical
#'   value `TRUE` from [service()]. the server is stopped as an exit side effect.
#'
#' @seealso [start_server()], [service()],
#'   [stop_server()]
#'
#' @examples
#' # a very basic application. run_server() blocks until interrupted, so
#' # schedule an interrupt() call to let this example return.
#' later2::later(interrupt, delay = 1)
#' run_server(
#'   "0.0.0.0", 5000,
#'   list(
#'     call = function(req) {
#'       list(
#'         status = 200L,
#'         headers = list(
#'           "content-type" = "text/html"
#'         ),
#'         body = "hello world!"
#'       )
#'     }
#'   )
#' )
#'
#' @export
run_server <- function(host, port, app) {
  server <- start_server(host, port, app)
  on.exit(stop_server(server))

  service(0)
}

#' interrupt httpserver runloop
#'
#' interrupts the currently running httpserver runloop, meaning
#' [run_server()] or [service()] will return control back to
#' the caller and no further tasks will be processed until those methods are
#' called again. note that this may cause in-process uploads or downloads to be
#' interrupted in mid-request.
#'
#' @return no return value, called for its side effect of pausing the request
#'   processing loop.
#'
#' @export
interrupt <- function() {
  .globals$paused <- TRUE
}

#' convert raw vector to base64-encoded string
#'
#' converts a raw vector to its base64 encoding as a single-element character
#' vector.
#'
#' @param x a raw vector.
#'
#' @return a single-element character vector containing the base64 encoding of
#'   `x`.
#'
#' @examples
#' set.seed(100)
#' result <- raw_to_base64(as.raw(runif(19, min = 0, max = 256)))
#' stopifnot(identical(result, "TkGNDnd7z16LK5/hR2bDqzRbXA=="))
#'
#' @export
raw_to_base64 <- function(x) {
  base64_encode(x)
}
