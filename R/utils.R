# A memoized wrapper for packageVersion(), because it is a fairly slow function
# which is called often. we can't get the version at build time because the
# package won't have been installed yet. instead, we'll get it at run time and
# cache it.
httpserver_version <- local({
  version <- NULL

  function() {
    if (is.null(version)) {
      version <<- utils::packageVersion("httpserver")
    }
    version
  }
})

# given a vector/list, return TRUE if any elements are unnamed, FALSE otherwise.
any_unnamed <- function(x) {
  # zero-length vector
  if (length(x) == 0) {
    return(FALSE)
  }

  nms <- names(x)

  # list with no name attribute
  if (is.null(nms)) {
    return(TRUE)
  }

  # list with name attribute; check for any ""
  any(!nzchar(nms))
}

# given a vector with multiple keys with the same name, drop any duplicated
# names. for example, with an input like list(a=1, a=2), returns list(a=1).
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

# create an empty named list
named_list <- function() {
  list(a = 1)[0]
}
