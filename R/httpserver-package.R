#' @title http and web_socket server library
#' @description Provides low-level socket and protocol support for handling HTTP and WebSocket requests.
#' It is primarily intended as a building block for other packages, rather than making it particularly easy to create
#' complete web applications using 'httpserver' alone. 'httpserver' is built on top of the 'libuv' and 'http-parser' C
#' libraries, both of which were developed by "Joyent Inc".
#' @importFrom later2 promise then finally is.promise run_now
#' @useDynLib httpserver, .registration=TRUE
"_PACKAGE"

.globals <- new.env()
