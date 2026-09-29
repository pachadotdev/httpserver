#' find an open tcp port
#'
#' finds a random available tcp port for listening on, within a specified range
#' of ports. the default range of ports to check is 1024 to 49151, which is the
#' set of tcp user ports. this function automatically excludes some ports which
#' are considered unsafe by web browsers.
#'
#' @inheritParams run_server
#' @param min minimum port number.
#' @param max maximum port number.
#' @param n number of ports to try before giving up.
#'
#' @return a port that is available to listen on.
#'
#' @examples
#' s <- start_server("127.0.0.1", random_port(), list())
#' paste0("http://127.0.0.1:", s$get_port())
#' s$stop()
#'
#' @export
random_port <- function(min = 1024L, max = 49151L, host = "127.0.0.1", n = 20) {
  min <- max(1L, min)
  max <- min(max, 65535L)
  valid_ports <- setdiff(seq.int(min, max), unsafe_ports)

  n <- min(n, length(valid_ports))
  # try up to n ports
  try_ports <- if (n < 2) valid_ports else sample(valid_ports, n)

  for (port in try_ports) {
    if (is_port_available(port, host)) {
      return(port)
    }
  }

  error_unavailable_port()
}

is_port_available <- function(port, host = "127.0.0.1") {
  tryCatch(
    {
      s <- start_server(host, port, list(), quiet = TRUE)
      s$stop()
      TRUE
    },
    error = function(e) FALSE
  )
}

error_unavailable_port <- function(message = "cannot find an available port.") {
  stop(
    structure(
      list(message = message, call = sys.call(-1)),
      class = c("httpuv_unavailable_port", "error", "condition")
    )
  )
}

# ports that are considered unsafe by chrome
# http://superuser.com/questions/188058/which-ports-are-considered-unsafe-on-chrome
# https://github.com/rstudio/shiny/issues/1784
unsafe_ports <- c(
  1,
  7,
  9,
  11,
  13,
  15,
  17,
  19,
  20,
  21,
  22,
  23,
  25,
  37,
  42,
  43,
  53,
  77,
  79,
  87,
  95,
  101,
  102,
  103,
  104,
  109,
  110,
  111,
  113,
  115,
  117,
  119,
  123,
  135,
  139,
  143,
  179,
  389,
  427,
  465,
  512,
  513,
  514,
  515,
  526,
  530,
  531,
  532,
  540,
  548,
  556,
  563,
  587,
  601,
  636,
  993,
  995,
  2049,
  3659,
  4045,
  6000,
  6665,
  6666,
  6667,
  6668,
  6669,
  6697
)
