#' create a static_path object
#'
#' the `static_path` function creates a `static_path` object. note that
#' if any of the arguments (other than `path`) are `NULL`, then that
#' means that for this particular static path, it should inherit the behavior
#' from the static_path_options set for the application as a whole.
#'
#' the `exclude_static_path` function tells the application to ignore a
#' particular path for static serving. this is useful when you want to include a
#' path for static serving (like `"/"`) but then exclude a subdirectory of
#' it (like `"/dynamic"`) so that the subdirectory will always be passed to
#' the r code for handling requests. `exclude_static_path` can be used not
#' only for directories; it can also exclude specific files.
#'
#' @param path the local path.
#' @inheritParams static_path_options
#'
#' @return an object of class `static_path`, consisting of the normalized local
#'   path and a `static_path_options` object describing how it is served.
#'
#' @seealso [static_path_options()].
#'
#' @export
static_path <- function(
  path,
  index_html = NULL,
  fallthrough = NULL,
  html_charset = NULL,
  headers = NULL,
  validation = NULL
) {
  if (!is.character(path) || length(path) != 1 || path == "") {
    stop("`path` must be a non-empty string.")
  }

  path <- normalizePath(path, winslash = "/", mustWork = TRUE)
  path <- enc2utf8(path)

  structure(
    list(
      path = path,
      options = normalize_static_path_options(static_path_options(
        index_html = index_html,
        fallthrough = fallthrough,
        html_charset = html_charset,
        headers = headers,
        validation = validation,
        exclude = FALSE
      ))
    ),
    class = "static_path"
  )
}

#' @rdname static_path
#' @export
exclude_static_path <- function() {
  structure(
    list(
      path = "",
      options = static_path_options(
        index_html = NULL,
        fallthrough = NULL,
        html_charset = NULL,
        headers = NULL,
        validation = NULL,
        exclude = TRUE
      )
    ),
    class = "static_path"
  )
}

#' @title convert an object to a static_path object
#'
#' @description convert an object to a `static_path` object. This is primarily
#' used internally, but can also be used by users to ensure that an object is a
#' `static_path` object.
#'
#' @param path the object to convert.
#'
#' @return a `static_path` object. character paths are converted to objects of
#'   class `static_path`; existing `static_path` objects are returned unchanged.
#'   objects of unsupported classes cause an error.
#'
#' @keywords internal
as.static_path <- function(path) {
  UseMethod("as.static_path", path)
}

#' @rdname as.static_path
#' @method as.static_path static_path
#' @exportS3Method as.static_path static_path
as.static_path.static_path <- function(path) {
  path
}

#' @rdname as.static_path
#' @method as.static_path character
#' @exportS3Method as.static_path character
as.static_path.character <- function(path) {
  static_path(path)
}

#' @rdname as.static_path
#' @method as.static_path default
#' @exportS3Method as.static_path default
as.static_path.default <- function(path) {
  stop(
    "cannot convert object of class ",
    class(path),
    " to a static_path object."
  )
}

#' @rdname static_path
#' @method print static_path
#' @exportS3Method print static_path
#' @param x a `static_path` object.
#' @param ... further arguments passed to or from other methods (currently
#'   unused).
print.static_path <- function(x, ...) {
  cat(format(x, ...), sep = "\n")
  invisible(x)
}

#' @rdname static_path
#' @method format static_path
#' @exportS3Method format static_path
format.static_path <- function(x, ...) {
  ret <- paste0(
    "<static_path>\n",
    "  local path:        ",
    x$path,
    "\n",
    format_opts(x$options)
  )
}

#' create options for static paths
#'
#'
#' @param index_html if an index.html file is present, should it be served up
#'   when the client requests the static path or any subdirectory?
#' @param fallthrough with the default value, `FALSE`, if a request is made
#'   for a file that doesn't exist, then httpserver will immediately send a 404
#'   response from the background i/o thread, without needing to call back into
#'   the main r thread. this offers the best performance. if the value is
#'   `TRUE`, then instead of sending a 404 response, httpserver will call the
#'   application's `call` function, and allow it to handle the request.
#' @param html_charset when html files are served, the value that will be
#'   provided for `charset` in the content-type header. for example, with
#'   the default value, `"utf-8"`, the header is `content-type:
#'   text/html; charset=utf-8`. if `""` is used, then no `charset`
#'   will be added in the content-type header.
#' @param headers additional headers and values that will be included in the
#'   response.
#' @param validation an optional validation pattern. presently, the only type of
#'   validation supported is an exact string match of a header. for example, if
#'   `validation` is `'"abc" = "xyz"'`, then http requests must have a
#'   header named `abc` (case-insensitive) with the value `xyz`
#'   (case-sensitive). if a request does not have a matching header, than
#'   httpserver will give a 403 forbidden response. if the `character(0)` (the
#'   default), then no validation check will be performed.
#' @param exclude should this path be excluded from static serving? (this is
#'   only to be used internally, for [exclude_static_path()].)
#'
#' @return an object of class `static_path_options`, containing the normalized
#'   options used when serving static paths.
#'
#' @export
static_path_options <- function(
  index_html = TRUE,
  fallthrough = FALSE,
  html_charset = "utf-8",
  headers = list(),
  validation = character(0),
  exclude = FALSE
) {
  res <- structure(
    list(
      index_html = index_html,
      fallthrough = fallthrough,
      html_charset = html_charset,
      headers = headers,
      validation = validation,
      exclude = exclude
    ),
    class = "static_path_options"
  )

  normalize_static_path_options(res)
}

#' @rdname static_path_options
#' @method print static_path_options
#' @exportS3Method print static_path_options
#' @param x a `static_path_options` object.
#' @param ... further arguments passed to or from other methods (currently
#'   unused).
print.static_path_options <- function(x, ...) {
  cat(format(x, ...), sep = "\n")
  invisible(x)
}


#' @rdname static_path_options
#' @method format static_path_options
#' @exportS3Method format static_path_options
format.static_path_options <- function(x, ...) {
  paste0(
    "<static_path_options>\n",
    format_opts(x, format_empty = "<none>")
  )
}

format_opts <- function(x, format_empty = "<inherit>") {
  format_option <- function(opt) {
    if (is.null(opt) || length(opt) == 0) {
      format_empty
    } else if (!is.null(names(opt))) {
      # named character vector
      lines <- mapply(
        function(name, value) paste0('    "', name, '" = "', value, '"'),
        names(opt),
        opt,
        simplify = FALSE,
        use.names = FALSE
      )

      lines <- paste(as.character(lines), collapse = "\n")
      lines <- paste0("\n", lines)
      lines
    } else {
      paste(as.character(opt), collapse = " ")
    }
  }
  ret <- paste0(
    "  use index.html:    ",
    format_option(x$index_html),
    "\n",
    "  fallthrough to r:  ",
    format_option(x$fallthrough),
    "\n",
    "  html charset:      ",
    format_option(x$html_charset),
    "\n",
    "  extra headers:     ",
    format_option(x$headers),
    "\n",
    "  validation params: ",
    format_option(x$validation),
    "\n",
    "  exclude path:      ",
    format_option(x$exclude),
    "\n"
  )
}


# this function always returns a named list of static_path objects. the names
# will all start with "/". the input can be a named character vector or a
# named list containing a mix of strings and static_path objects. this function
# is idempotent.
normalize_static_paths <- function(paths) {
  if (is.null(paths) || length(paths) == 0) {
    return(list())
  }

  if (any_unnamed(paths)) {
    stop(
      "paths must be a named character vector, a named list containing strings and static_path objects, or null."
    )
  }

  if (!is.character(paths) && !is.list(paths)) {
    stop(
      "paths must be a named character vector, a named list containing strings and static_path objects, or null."
    )
  }

  # convert to list of static_path objects. need this verbose wrapping of
  # as.static_path because of s3 dispatch for non-registered methods.
  paths <- lapply(paths, function(path) as.static_path(path))

  # make sure url paths have a leading '/' and no trailing '/'.
  names(paths) <- vapply(
    names(paths),
    function(path) {
      path <- enc2utf8(path)

      if (path == "") {
        stop("all paths must be non-empty strings.")
      }
      # ensure there's a leading / for every path
      if (substr(path, 1, 1) != "/") {
        path <- paste0("/", path)
      }
      # strip trailing slashes, except when the path is just "/".
      if (path != "/") {
        path <- sub("/+$", "", path)
      }

      path
    },
    ""
  )

  paths
}

# takes a static_path_options object and modifies it so that the resulting
# object is easier to work with on the c++ side. the resulting object is not
# meant to be modified on the r side. this function is idempotent; if the
# object has already been normalized, it will not be modified. for each entry,
# a NULL means to inherit.
normalize_static_path_options <- function(opts) {
  if (isTRUE(attr(opts, "normalized", exact = TRUE))) {
    return(opts)
  }

  # html_charset can accept "" or character(0). but on the c++ side, we want
  # "".
  if (!is.null(opts$html_charset)) {
    if (length(opts$html_charset) == 0) {
      opts$html_charset <- ""
    }
  }

  if (!is.null(opts$exclude)) {
    if (!is.logical(opts$exclude) || length(opts$exclude) != 1) {
      stop("`exclude` option must be TRUE or FALSE.")
    }
  }

  # can be a named list of strings, or a named character vector. on the c++
  # side, we want a named character vector.
  if (is.list(opts$headers)) {
    # convert list to named character vector
    opts$headers <- unlist(opts$headers, recursive = FALSE)
    # special case: if opts$headers was an empty list before unlist(), it is
    # now null. replace it with an empty named character vector.
    if (length(opts$headers) == 0) {
      opts$headers <- c(a = "a")[0]
    }

    if (!is.character(opts$headers) || any_unnamed(opts$headers)) {
      stop("`headers` option must be a named list or character vector.")
    }
  }

  if (!is.null(opts$validation)) {
    if (!is.character(opts$validation) || length(opts$validation) > 1) {
      stop(
        "`validation` option must be a character vector with zero or one element."
      )
    }

    # both "" and character(0) result in character(0). length-1 strings other
    # than "" will be parsed.
    if (length(opts$validation) == 1) {
      if (opts$validation == "") {
        opts$validation <- character(0)
      } else {
        fail <- FALSE
        tryCatch(
          p <- parse(text = opts$validation)[[1]],
          error = function(e) fail <<- TRUE
        )
        if (!fail) {
          if (
            length(p) != 3 ||
              p[[1]] != as.symbol("==") ||
              !is.character(p[[2]]) ||
              length(p[[2]]) != 1 ||
              !is.character(p[[3]]) ||
              length(p[[3]]) != 1
          ) {
            fail <- TRUE
          }
        }
        if (fail) {
          stop(
            "`validation` must be a string of the form: '\"xxx\" == \"yyy\"'"
          )
        }

        # turn it into a char vector for easier processing in c++
        opts$validation <- as.character(p)
      }
    }
  }

  attr(opts, "normalized") <- TRUE
  opts
}
