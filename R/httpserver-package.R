#' @title http and web_socket server library
#' @description Low-level socket and protocol support for handling HTTP and WebSocket requests. It is primarily intended as
#' a building block for other packages such as 'tabler' and hopefully future packages in the 'CRAN Task View: Web
#' Technologies and Services'.
#' @importFrom later2 promise then finally is.promise run_now
#' @useDynLib httpserver, .registration=TRUE
"_PACKAGE"

.globals <- new.env()
