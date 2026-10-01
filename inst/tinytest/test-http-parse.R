source("helper-app.R")

local({
  # large http header values are preserved ----

  if (!requireNamespace("curl")) { return(NULL) }

  # Large headers may span multiple TCP messages and must not be truncated.
  s <- httpserver::start_server(
    "0.0.0.0",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 200L,
          headers = list('content-type' = 'text/plain'),
          # use paste0("", ...) in case the header is NULL
          body = paste0("", req$http_test_header)
        )
      }
    )
  )

  on.exit(s$stop())

  # create a request with a large header (80000 bytes)
  # the maximum size of a tcp message is 64k_b, so i believe this header will
  # necessarily result in more than 1 call to http_request::_on_header_value().
  # note that this is under the http_max_header_size defined in http_parser.h
  # to be 80*1024. a larger message would result in the server just closing the
  # connection.
  long_string <- paste0(rep(".", 80000), collapse = "")
  h <- curl::new_handle()
  curl::handle_setheaders(h, `test-header` = long_string)

  res <- fetch(local_url("/", s$get_port()), h)
  content <- rawToChar(res$content)

  expect_identical(content, long_string)

  # similar to previous, but make sure there are two header entries with the
  # same field name, as in:
  #   foo: aaaaaaaa....
  #   foo: bbbbbbbb....
  # the resulting value of foo should should be "aaaaaa,bbbbbbbbbbbb"
  # (note: i've tested, and repeating the same header name with curl does result
  # two of those headers.)
  long_string_a <- paste0(rep("a", 100), collapse = "")
  long_string_b <- paste0(rep("b", 80000), collapse = "")

  # the second test-header value is the long one, so it will be split across
  # multiple tcp messages.
  h <- curl::new_handle()
  curl::handle_setheaders(
    h,
    `test-header` = long_string_a,
    `test-header` = long_string_b
  )
  res <- fetch(local_url("/", s$get_port()), h)
  content <- rawToChar(res$content)
  
  expect_identical(content, paste0(long_string_a, ",", long_string_b))

  # the first test-header value is the long one, so it will be split across
  # multiple tcp messages.
  h <- curl::new_handle()
  curl::handle_setheaders(
    h,
    `test-header` = long_string_b,
    `test-header` = long_string_a
  )
  res <- fetch(local_url("/", s$get_port()), h)
  content <- rawToChar(res$content)

  expect_identical(content, paste0(long_string_b, ",", long_string_a))
})

local({
  # large http header field names are preserved ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  # Field names may also be split across messages.
  headers_received <- NULL
  s <- httpserver::start_server(
    "0.0.0.0",
    random_port(),
    list(
      call = function(req) {
        # save the headers for examination later
        headers_received <<- req$headers
        list(
          status = 200L,
          headers = list('content-type' = 'text/plain'),
          body = paste0("ok")
        )
      }
    )
  )
  
  on.exit(s$stop())

  # test for long field names, as in:
  #  aaaaaa...aaaaaa: a
  #  bbbbbb...bbbbbb: b
  # variable names in r must be 10000 bytes or less, so we need several of them
  # to do this test.
  h <- curl::new_handle()
  values <- as.list(letters[1:8])
  
  # use 9900-byte field names (instead of 10000) because the rook object makes
  # them longer by prepending "http_".
  fields <- vapply(
    letters[1:8],
    function(x) paste0(rep(x, 9900), collapse = ""),
    ""
  )
  headers <- setNames(values, fields)
  do.call(curl::handle_setheaders, c(list(h), headers))

  res <- fetch(local_url("/", s$get_port()), h)
  expect_true(all(fields %in% names(headers_received)))
  expect_identical(as.list(headers_received[fields]), headers)
})
