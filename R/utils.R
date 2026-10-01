# Memoize the package version because it is queried for every Rook request.
httpserver_version <- local({
  version <- NULL

  function() {
    if (is.null(version)) {
      version <<- utils::packageVersion("httpserver")
    }
    version
  }
})

# Return TRUE when any element is unnamed.
any_unnamed <- function(x) {
  # A zero-length object has no unnamed elements.
  if (length(x) == 0) {
    return(FALSE)
  }

  nms <- names(x)

  # A list without a names attribute is unnamed.
  if (is.null(nms)) {
    return(TRUE)
  }

  # An empty name is also considered unnamed.
  any(!nzchar(nms))
}

# Keep the first value for each name.
drop_duplicate_names <- function(x) {
  if (any_unnamed(x)) {
    stop("all items must be named.")
  }
  x[unique(names(x))]
}

#' get and set logging level
#'
#' the logging level for httpserver can be set to report differing levels of
#' information. possible logging levels (from least to most information
#' reported) are: `"off"`, `"error"`, `"warn"`, `"info"`, or
#' `"debug"`. the default level is `error`.
#'
#' @param level the logging level. must be one of `NULL`, `"off"`,
#'   `"error"`, `"warn"`, `"info"`, or `"debug"`. if
#'   `NULL` (the default), then this function simply returns the current
#'   logging level.
#'
#' @return if `level=NULL`, then this returns the current logging level. if
#'   `level` is any other value, then this returns the previous logging
#'   level, from before it is set to the new value.
#'
#' @keywords internal
log_level <- function(level = NULL) {
  if (is.null(level)) {
    level <- ""
    log_level("")
  } else {
    level <- match.arg(level, c("off", "error", "warn", "info", "debug"))
    invisible(log_level(level))
  }
}

# Create an empty named list.
named_list <- function() {
  list(a = 1)[0]
}
