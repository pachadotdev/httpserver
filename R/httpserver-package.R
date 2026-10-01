#' @title HTTP and WebSocket server library
#' @description Low-level socket and protocol support for handling HTTP and
#' WebSocket requests. It is primarily intended as a building block for other R
#' packages.
#' @importFrom later2 promise then finally is.promise run_now
#' @useDynLib httpserver, .registration=TRUE
"_PACKAGE"

.globals <- new.env()
