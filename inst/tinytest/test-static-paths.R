# tinytest sets the working directory to this file's directory before
# sourcing it, so paths can be resolved with base r's file.path() instead of
# testthat::test_path().
test_path <- function(...) file.path(...)

index_file_content <- raw_file_content(test_path("apps/content/index.html"))
data_file_content <- raw_file_content(test_path("apps/content/data.txt"))
subdir_index_file_content <- raw_file_content(test_path(
  "apps/content/subdir/index.html"
))

local({
  # basic static file serving ----

  if (!requireNamespace("curl")) { return(NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      static_paths = list(
        # testing out various leading and trailing slashes
        "/" = test_path("apps/content"),
        "/1" = test_path("apps/content"),
        "/2/" = test_path("apps/content/"),
        "3" = test_path("apps/content"),
        "4/" = test_path("apps/content/")
      ),
      static_path_options = static_path_options(
        headers = list("Test-Code-Path" = "C++")
      )
    )
  )
  on.exit(s$stop())

  # fetch index.html
  r <- fetch(local_url("/", s$get_port()), gzip = FALSE)
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)

  # index.html for subdirectory
  r_subdir <- fetch(local_url("/subdir", s$get_port()))
  expect_equal(r_subdir$status_code, 200)
  expect_identical(r_subdir$content, subdir_index_file_content)

  h <- curl::parse_headers_list(r$headers)
  expect_equal(as.integer(h$`content-length`), length(index_file_content))
  expect_equal(as.integer(h$`content-length`), length(r$content))
  expect_identical(h$`content-type`, "text/html; charset=utf-8")
  expect_identical(h$`test-code-path`, "C++")
  # check that response time is within 1 minute of now. (possible dst problems?)
  expect_true(
    abs(as.numeric(parse_http_date(h$date)) - as.numeric(Sys.time())) < 60
  )

  # testing index for other paths
  r1 <- fetch(local_url("/1", s$get_port()), gzip = FALSE)
  h1 <- curl::parse_headers_list(r1$headers)
  expect_identical(r$content, r1$content)
  expect_identical(h$`content-length`, h1$`content-length`)
  expect_identical(h$`content-type`, h1$`content-type`)

  r2 <- fetch(local_url("/1/", s$get_port()), gzip = FALSE)
  h2 <- curl::parse_headers_list(r2$headers)
  expect_identical(r$content, r2$content)
  expect_identical(h$`content-length`, h2$`content-length`)
  expect_identical(h$`content-type`, h2$`content-type`)

  r3 <- fetch(local_url("/1/index.html", s$get_port()), gzip = FALSE)
  h3 <- curl::parse_headers_list(r3$headers)
  expect_identical(r$content, r3$content)
  expect_identical(h$`content-length`, h3$`content-length`)
  expect_identical(h$`content-type`, h3$`content-type`)

  # missing file (404)
  r <- fetch(local_url("/foo", s$get_port()), gzip = FALSE)
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 404)
  expect_identical(rawToChar(r$content), "404 Not Found\n")
  expect_equal(h$`content-length`, "14")

  # missing directory in path (404)
  r <- fetch(local_url("/foo/bar", s$get_port()), gzip = FALSE)
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 404)
  expect_identical(rawToChar(r$content), "404 Not Found\n")
  expect_equal(h$`content-length`, "14")

  # mime types for other files
  r <- fetch(local_url("/mtcars.csv", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(h$`content-type`, "text/csv")

  r <- fetch(local_url("/data.txt", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(h$`content-type`, "text/plain")
})

local({
  # missing file fallthrough ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        return(list(
          status = 404,
          headers = list("Test-Code-Path" = "R"),
          body = paste0("404 file not found: ", req$path_info)
        ))
      },
      static_paths = list(
        # testing out various leading and trailing slashes
        "/" = static_path(
          test_path("apps/content"),
          index_html = FALSE,
          fallthrough = TRUE
        )
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 404)
  expect_identical(h$`test-code-path`, "R")
  expect_identical(rawToChar(r$content), "404 file not found: /")
})

local({
  # longer paths override shorter ones ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      static_paths = list(
        # testing out various leading and trailing slashes
        "/" = test_path("apps/content"),
        "/a" = static_path(
          test_path("apps/content"),
          index_html = FALSE
        ),
        "/a/b" = static_path(
          test_path("apps/content"),
          index_html = NULL
        ),
        "/a/b/c" = static_path(
          test_path("apps/content"),
          index_html = TRUE
        )
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)

  r <- fetch(local_url("/a/", s$get_port()))
  expect_equal(r$status_code, 404)

  # when NULL, option values are not inherited from the parent dir, "/a";
  # they're inherited from the overall options for the app.
  r <- fetch(local_url("/a/b", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)

  r <- fetch(local_url("/a/b/c", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)
})

local({
  # options and option inheritance ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        return(list(
          status = 404,
          headers = list("Test-Code-Path" = "R"),
          body = paste0("404 file not found: ", req$path_info)
        ))
      },
      static_paths = list(
        "/default" = static_path(test_path("apps/content")),
        # this path overrides options
        "/override" = static_path(
          test_path("apps/content"),
          index_html = FALSE,
          fallthrough = TRUE,
          html_charset = "ISO-8859-1",
          headers = list("Test-Code-Path" = "C++2")
        ),
        # this path unsets some options
        "/unset" = static_path(
          test_path("apps/content"),
          html_charset = "",
          headers = list()
        )
      ),
      static_path_options = static_path_options(
        index_html = TRUE,
        fallthrough = FALSE,
        headers = list("Test-Code-Path" = "C++")
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/default", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`content-type`, "text/html; charset=utf-8")
  expect_identical(h$`test-code-path`, "C++")
  expect_identical(r$content, index_file_content)

  r <- fetch(local_url("/override", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 404)
  expect_identical(h$`test-code-path`, "R")
  expect_identical(rawToChar(r$content), "404 file not found: /override")

  r <- fetch(local_url("/override/index.html", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`test-code-path`, "C++2")
  expect_identical(h$`content-type`, "text/html; charset=ISO-8859-1")

  r <- fetch(local_url("/unset", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_false("test-code-path" %in% names(h))
  expect_identical(h$`content-type`, "text/html")
  expect_identical(r$content, index_file_content)

  r <- fetch(local_url("/unset/index.html", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_false("test-code-path" %in% names(h))
  expect_identical(h$`content-type`, "text/html")
  expect_identical(r$content, index_file_content)
})

local({
  # excluding subpaths ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        # return a 403 for the r code path; the c++ code path will return 404
        # for missing files.
        return(list(
          status = 403,
          headers = list("Test-Code-Path" = "R"),
          body = paste0("403 Forbidden: ", req$path_info)
        ))
      },
      static_paths = list(
        "/" = static_path(test_path("apps/content")),
        "/exclude" = exclude_static_path(),
        "/subdi" = exclude_static_path(),

        "/a" = static_path(test_path("apps/content")),
        "/a/exclude" = exclude_static_path(),
        "/a/mtcars.csv" = exclude_static_path()
      )
    )
  )
  on.exit(s$stop())

  exclude_subdir_index_file_content <- raw_file_content(test_path(
    "apps/content/exclude/subdir/index.html"
  ))

  # basic test
  r <- fetch(local_url("/", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)
  r <- fetch(local_url("/subdir", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, subdir_index_file_content)
  r <- fetch(local_url("/exclude", s$get_port()))
  expect_equal(r$status_code, 403)
  r <- fetch(local_url("/exclude/index.html", s$get_port()))
  expect_equal(r$status_code, 403)
  r <- fetch(local_url("/exclude/subdir", s$get_port()))
  expect_equal(r$status_code, 403)
  r <- fetch(local_url("/exclude/subdir/index.html", s$get_port()))
  expect_equal(r$status_code, 403)

  # include directories underneath excluded dir.
  s$set_static_path("exclude/include" = test_path("apps/content"))
  r <- fetch(local_url("/exclude/include", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)

  s$set_static_path("exclude/subdir" = test_path("apps/content/exclude/subdir"))
  r <- fetch(local_url("/exclude/subdir", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, exclude_subdir_index_file_content)

  # a file that is not specifically excluded will use the c++ 404 path.
  r <- fetch(local_url("/nonexistent.txt", s$get_port()))
  expect_equal(r$status_code, 404)

  # fallthrough. behavior should be unchanged except for non-existent files that
  # are not in the excluded path.
  s$set_static_path_option(fallthrough = TRUE)
  # now, a file that is not specifically excluded will use the r 403 path
  r <- fetch(local_url("/nonexistent.txt", s$get_port()))
  expect_equal(r$status_code, 403)
  s$set_static_path_option(fallthrough = FALSE)

  # partial name matching ("subdi" was excluded) doesn't work.
  r <- fetch(local_url("/subdir", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, subdir_index_file_content)

  # specific files
  r <- fetch(local_url("/a/", s$get_port()))
  expect_equal(r$status_code, 200)
  r <- fetch(local_url("/a/mtcars.csv", s$get_port()))
  expect_equal(r$status_code, 403)
  # a file that is not specifically excluded will use the c++ 404 path.
  r <- fetch(local_url("/file/nonexistent.txt", s$get_port()))
  expect_equal(r$status_code, 404)
})

local({
  # header validation ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        if (!identical(req$http_test_validation, "aaa")) {
          return(list(
            status = 403,
            headers = list("Test-Code-Path" = "R"),
            body = "403 Forbidden\n"
          ))
        }
        return(list(
          status = 200,
          headers = list("Test-Code-Path" = "R"),
          body = "200 OK\n"
        ))
      },
      static_paths = list(
        "/default" = static_path(test_path("apps/content")),
        # this path overrides validation
        "/override" = static_path(
          test_path("apps/content"),
          validation = c('"Test-Validation-1" == "bbb"')
        ),
        # this path unsets validation
        "/unset" = static_path(
          test_path("apps/content"),
          validation = character()
        ),
        # fall through to r
        "/fallthrough" = static_path(
          test_path("apps/content"),
          fallthrough = TRUE
        )
      ),
      static_path_options = static_path_options(
        headers = list("Test-Code-Path" = "C++"),
        validation = c('"Test-Validation" == "aaa"')
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/default", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 403)
  # this header doesn't get set. should it?
  expect_false("test-code-path" %in% names(h))
  expect_identical(rawToChar(r$content), "403 Forbidden\n")

  r <- fetch(
    local_url("/default", s$get_port()),
    curl::handle_setheaders(curl::new_handle(), "test-validation" = "aaa")
  )
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`test-code-path`, "C++")
  expect_identical(r$content, index_file_content)

  # check case insensitive
  r <- fetch(
    local_url("/default", s$get_port()),
    curl::handle_setheaders(curl::new_handle(), "tesT-ValidatioN" = "aaa")
  )
  expect_equal(r$status_code, 200)

  r <- fetch(local_url("/unset", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`test-code-path`, "C++")
  expect_identical(r$content, index_file_content)

  # when fallthrough=TRUE, the header validation is still checked before falling
  # through to the r code path.
  r <- fetch(local_url("/fallthrough/missingfile", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 403)
  # this header doesn't get set. should it?
  expect_false("test-code-path" %in% names(h))
  expect_identical(rawToChar(r$content), "403 Forbidden\n")

  r <- fetch(
    local_url("/fallthrough/missingfile", s$get_port()),
    curl::handle_setheaders(curl::new_handle(), "test-validation" = "aaa")
  )
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`test-code-path`, "R")
  expect_identical(rawToChar(r$content), "200 OK\n")
})


local({
  # dynamically changing paths ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 500,
          headers = list("Test-Code-Path" = "R"),
          body = "500 Internal Server Error\n"
        )
      },
      static_paths = list(
        "/static" = test_path("apps/content")
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)

  # replace with different static path and options
  s$set_static_path(
    "/static" = static_path(
      test_path("apps/content"),
      index_html = FALSE
    )
  )

  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 404)

  r <- fetch(local_url("/static/index.html", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(
    r$content,
    raw_file_content(test_path("apps/content/index.html"))
  )

  # remove static path
  s$remove_static_path("/static")

  expect_equal(length(s$get_static_paths()), 0)

  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 500)
  h <- curl::parse_headers_list(r$headers)
  expect_identical(h$`test-code-path`, "R")
  expect_identical(rawToChar(r$content), "500 Internal Server Error\n")

  # add static path
  s$set_static_path(
    "/static_new" = test_path("apps/content")
  )
  r <- fetch(local_url("/static_new", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, index_file_content)
})


local({
  # dynamically changing options ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 500,
          headers = list("Test-Code-Path" = "R"),
          body = "500 Internal Server Error\n"
        )
      },
      static_paths = list(
        "/static" = test_path("apps/content")
      )
    )
  )
  on.exit(s$stop())

  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 200)

  s$set_static_path_option(index_html = FALSE)
  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 404)

  s$set_static_path_option(fallthrough = TRUE)
  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 500)

  s$set_static_path_option(
    index_html = TRUE,
    headers = list("Test-Headers" = "aaa"),
    validation = c('"Test-Validation" == "aaa"')
  )
  r <- fetch(local_url("/static", s$get_port()))
  expect_equal(r$status_code, 403)
  r <- fetch(
    local_url("/static", s$get_port()),
    curl::handle_setheaders(curl::new_handle(), "test-validation" = "aaa")
  )
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_identical(h$`test-headers`, "aaa")

  # unset some options
  s$set_static_path_option(
    headers = list(),
    validation = character()
  )
  r <- fetch(local_url("/static", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  expect_equal(r$status_code, 200)
  expect_false("test-headers" %in% h)
})

local({
  # escaped characters in paths ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  # need to create files with weird names
  static_dir <- tempfile("httpuv_test")
  dir.create(static_dir)
  # use write_bin() instead of cat() because in windows, cat() will convert "\n"
  # to "\r\n".
  writeBin(
    charToRaw("This is file content.\n"),
    file.path(static_dir, "file with space.txt")
  )
  on.exit(unlink(static_dir, recursive = TRUE))

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 500,
          headers = list("Test-Code-Path" = "R"),
          body = "500 Internal Server Error\n"
        )
      },
      static_paths = list(
        "/static" = static_dir
      )
    )
  )
  on.exit(s$stop(), add = TRUE)

  r <- fetch(local_url("/static/file%20with%20space.txt", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(rawToChar(r$content), "This is file content.\n")
})

local({
  # paths with .. ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 404,
          headers = list("Test-Code-Path" = "R"),
          body = "404 Not Found\n"
        )
      },
      static_paths = list(
        "/static" = test_path("apps/content")
      )
    )
  )
  on.exit(s$stop())

  # need to use http_request_con() instead of fetch() to send custom requests
  # with "..".
  res <- http_request_con("GET /", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 404 Not Found")
  expect_true(any(grepl("^Test-Code-Path: R$", res, ignore.case = TRUE)))

  res <- http_request_con("GET /static", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 200 OK")

  # the presence of a ".." path segment results in a 400.
  res <- http_request_con("GET /static/..", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")

  res <- http_request_con("GET /static/../", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")

  res <- http_request_con("GET /static/../static", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")

  # ".." is valid as part of a path segment (but we'll get 404's since the files
  # don't actually exist).
  res <- http_request_con("GET /static/..foo", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 404 Not Found")
  expect_false(any(grepl("^test-code-path: r$", res, ignore.case = TRUE)))

  res <- http_request_con("GET /static/foo..", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 404 Not Found")
  expect_false(any(grepl("^test-code-path: r$", res, ignore.case = TRUE)))

  res <- http_request_con("GET /static/foo../", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 404 Not Found")
  expect_false(any(grepl("^test-code-path: r$", res, ignore.case = TRUE)))
})

local({
  # paths with backslash ----

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 400,
          headers = list("Test-Code-Path" = "R"),
          body = "400 Bad Request\n"
        )
      },
      static_paths = list(
        "/static" = test_path("apps/content")
      )
    )
  )
  on.exit(s$stop())

  # need to use http_request_con() instead of fetch() to send custom requests
  # with "..".
  # when a backslash is in path, should fall through to r code path.

  # raw backslash
  res <- http_request_con("GET /static\\index.html", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")
  expect_true(any(grepl("^Test-Code-Path: R$", res, ignore.case = TRUE)))

  # escaped backslash
  res <- http_request_con("GET /static%5cindex.html", "127.0.0.1", s$get_port())
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")
  expect_true(any(grepl("^Test-Code-Path: R$", res, ignore.case = TRUE)))

  # raw backslash with ..
  res <- http_request_con(
    "GET /static/..\\index.html",
    "127.0.0.1",
    s$get_port()
  )
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")
  expect_true(any(grepl("^test-code-path: r$", res, ignore.case = TRUE)))

  # escaped backslash with ..
  res <- http_request_con(
    "GET /static/..%5cindex.html",
    "127.0.0.1",
    s$get_port()
  )
  expect_identical(res[1], "HTTP/1.1 400 Bad Request")
  expect_true(any(grepl("^test-code-path: r$", res, ignore.case = TRUE)))
})

local({
  # head, post, put requests ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 404,
          headers = list("Test-Code-Path" = "R"),
          body = "404 Not Found\n"
        )
      },
      static_paths = list(
        "/static" = test_path("apps/content")
      )
    )
  )
  on.exit(s$stop())

  # the get results, for comparison to head.
  r_get <- fetch(local_url("/static", s$get_port()), gzip = FALSE)
  h_get <- curl::parse_headers_list(r_get$headers)

  # head is ok.
  # note the weird interface for a head request:
  # https://github.com/jeroen/curl/issues/24
  r <- fetch(
    local_url("/static", s$get_port()),
    curl::new_handle(nobody = TRUE),
    gzip = FALSE
  )
  expect_equal(r$status_code, 200)
  expect_true(length(r$content) == 0) # no message body for head
  h <- curl::parse_headers_list(r$headers)
  # headers should match get request, except for date.
  expect_identical(
    h[setdiff(names(h), "date")],
    h_get[setdiff(names(h_get), "date")]
  )

  # post and put are not ok
  r <- fetch(
    local_url("/static", s$get_port()),
    curl::handle_setopt(curl::new_handle(), customrequest = "POST")
  )
  expect_equal(r$status_code, 400)

  r <- fetch(
    local_url("/static", s$get_port()),
    curl::handle_setopt(curl::new_handle(), customrequest = "PUT")
  )
  expect_equal(r$status_code, 400)
})


local({
  # last-modified and if-modified-since headers ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- start_server(
    "127.0.0.1",
    random_port(),
    list(
      static_paths = list(
        "/" = static_path(
          test_path("apps/content"),
          headers = list(
            "ETag" = "abc",
            "Cache-Control" = "max-age=12345",
            "Other" = "xyz"
          )
        )
      )
    )
  )
  on.exit(s$stop())

  # mtime of the target file, rounded down to nearest second.
  file_mtime <- as.POSIXct(trunc(
    file.info(test_path("apps/content/mtcars.csv"))$mtime
  ))

  # first time retrieving: no last-modified header.
  r <- fetch(local_url("/mtcars.csv", s$get_port()))
  h <- curl::parse_headers_list(r$headers)
  http_mtime <- r$modified
  expect_equal(as.character(file_mtime), as.character(http_mtime))

  # use the last-modified value in the if-modified-since header.
  r1 <- fetch(
    local_url("/mtcars.csv", s$get_port()),
    curl::handle_setheaders(curl::new_handle(), "if-modified-since" = h$`last-modified`)
  )
  expect_identical(r1$status_code, 304L)
  expect_true(length(r1$content) == 0)
  h1 <- curl::parse_headers_list(r1$headers)
  # a 304 response should contain only the following headers (and must contain
  # them if the corresponding 200 response would have them):
  # cache-control, content-location, date, etag, expires, vary
  # https://httpstatuses.com/304
  expect_identical(
    h[c("cache-control", "etag")],
    h1[c("cache-control", "etag")]
  )
  # the date header differs from the previous response because the request was
  # made at a different time. we just need to check that it's present.
  expect_true("date" %in% names(h1))

  # the mtime plus 1 second should result in a 304.
  r1 <- fetch(
    local_url("/mtcars.csv", s$get_port()),
    curl::handle_setheaders(
      curl::new_handle(),
      "if-modified-since" = http_date_string(file_mtime + 1)
    )
  )
  expect_identical(r1$status_code, 304L)

  # last-modified header minus 1 second should result in a regular 200 response.
  r1 <- fetch(
    local_url("/mtcars.csv", s$get_port()),
    curl::handle_setheaders(
      curl::new_handle(),
      "if-modified-since" = http_date_string(file_mtime - 1)
    )
  )
  expect_identical(r1$status_code, 200L)
  h1 <- curl::parse_headers_list(r1$headers)
  expect_identical(h[setdiff(names(h), "date")], h1[setdiff(names(h1), "date")])

  # malformed if-modified-since value should be ignored.
  #
  # first, a date far in the future should result in 304. note that the 2038
  # date is used here because on 32-bit windows, dates that are beyond
  # 2038-01-19 will overflow and wrap around, and this request will get a 200
  # instead of 304. other platforms seem not to have this limitation.
  r1 <- fetch(
    local_url("/mtcars.csv", s$get_port()),
    curl::handle_setheaders(
      curl::new_handle(),
      "If-Modified-Since" = "Mon, 01 Jan 2038 12:00:00 GMT"
    )
  )
  expect_identical(r1$status_code, 304L)
  # next, almost the same date, but slightly malformed, should result in 200.
  r1 <- fetch(
    local_url("/mtcars.csv", s$get_port()),
    curl::handle_setheaders(
      curl::new_handle(),
      "if-modified-since" = "mon, 01 jan 2038 12:100:00 gmt"
    )
  )
  expect_identical(r1$status_code, 200L)
})

local({
  # paths with non-ascii characters ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  # workaround for https://github.com/rstudio/httpuv/issues/264
  # on unix platforms that are using a non-utf-8 locale, don't do these tests.
  if (.Platform$OS.type == "unix" && !l10n_info()[["UTF-8"]]) {
    return(NULL)
  }

  # "apps/fü", in utf-8 encoding.
  nonascii_path <- test_path("apps/f\U00FC")
  dir.create(nonascii_path)
  on.exit(unlink(nonascii_path, recursive = TRUE))

  index_file_path <- file.path(nonascii_path, "index.html")
  writeLines("Hello world!", index_file_path)
  file_content <- raw_file_content(index_file_path)

  s <- start_server(
    "0.0.0.0",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 200L,
          headers = list('content-type' = 'text/html'),
          body = "r code path"
        )
      },
      static_paths = list(
        "/f\U00FC" = nonascii_path,
        "/foo" = nonascii_path
      )
    )
  )
  on.exit(s$stop(), add = TRUE)

  # url-encoded non-ascii url path, which maps to non-ascii local path.
  r <- fetch(local_url("/f%c3%bc", s$get_port()))
  expect_identical(r$status_code, 200L)
  expect_identical(r$content, file_content)

  # ascii url path, which maps to non-ascii local path.
  r <- fetch(local_url("/foo", s$get_port()))
  expect_identical(r$status_code, 200L)
  expect_identical(r$content, file_content)
})
