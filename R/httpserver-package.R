#' @title HTTP and WebSocket Server Library
#' @description Provides low-level socket and protocol support for handling
#'  HTTP and WebSocket requests directly from within R. It is primarily
#'  intended as a building block for other packages, rather than making it
#'  particularly easy to create complete web applications using httpuv
#'  alone.  httpuv is built on top of the libuv and http-parser C
#'  libraries, both of which were developed by Joyent, Inc. Derived from the
#'  'httpuv' package with a focus on minimal dependencies. See the LICENSE
#'  file for libuv and http-parser license information.
#' @importFrom later2 promise then finally is.promise run_now
#' @importFrom R6 R6Class
#' @useDynLib httpserver, .registration=TRUE
"_PACKAGE"
