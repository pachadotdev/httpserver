#' serve a directory
#'
#' `run_static_server()` provides a convenient interface to start a server to host
#' a single static directory, either in the foreground or the background.
#'
#' @examples
#' website_dir <- system.file("example-static-site", package = "httpserver")
#' s <- run_static_server(dir = website_dir, background = TRUE, browse = FALSE)
#' s$stop()
#'
#' @param dir the directory to serve. defaults to the current working directory.
#' @inheritParams start_server
#' @param ... additional arguments passed to [static_path()], such as
#'   `index_html`, `fallthrough`, `html_charset`, `headers`, and `validation`.
#' @param background whether to run the server in the background. by default,
#'   the server runs in the foreground and blocks the r console. you can stop
#'   the server by interrupting it with `ctrl + c`.
#'
#'   when `background = TRUE`, the server will run in the background and will
#'   process requests when the r console is idle. to stop a background server,
#'   call [stop_all_servers()] or call [stop_server()] on the server object
#'   returned (invisibly) by this function.
#' @param browse whether to automatically open the served directory in a web
#'   browser. defaults to `TRUE` when running interactively.
#'
#' @returns starts a server on the specified host and port. by default the
#'   server runs in the foreground and is accessible at `http://127.0.0.1:7446`.
#'   when `background = TRUE`, the `server` object is returned invisibly.
#'
#' @seealso [run_server()] provides a similar interface for running a dynamic
#'   app server. both `run_static_server()` and [run_server()] are built on top of
#'   [start_server()], [service()] and [stop_server()]. learn more about httpserver
#'   servers in [start_server()].
#'
#' @export
run_static_server <- function(
  dir = getwd(),
  host = "127.0.0.1",
  port = NULL,
  ...,
  background = FALSE,
  browse = interactive()
) {
  root <- static_path(dir, ...)

  if (is.null(port)) {
    port <- if (is_port_available(7446, host)) 7446 else random_port(host = host)
  } else {
    stopifnot(
      "`port` must be an integer" = is.numeric(port),
      "`port` must be an integer" = port == as.integer(port),
      "`port` must be a single integer" = length(port) == 1,
      "`port` must be a positive integer" = port > 0,
      "`port` must be less than 65536" = port < 65536
    )

    if (!is_port_available(port, host)) {
      error_unavailable_port(
        paste("port", port, "is not available on host", host)
      )
    }
  }

  server <- start_server(
    app = list(static_paths = list("/" = root)),
    host = host,
    port = port
  )

  message("serving: '", dir, "'")
  message("view at: http://", host, ":", port, sep = "")

  if (isTRUE(browse)) {
    tryCatch(
      utils::browseURL(paste0("http://", host, ":", port)),
      error = function(err) {
        message(
          "could not open browser due to error in `utils::browseURL()`: ",
          conditionMessage(err)
        )
      }
    )
  }

  if (isTRUE(background)) {
    return(invisible(server))
  }

  on.exit(stop_server(server))
  message("press esc or ctrl + c to stop the server")
  service(0)
}
