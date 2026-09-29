source("helper-app.R")

local({
  # basic functionality ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s1 <- httpserver::start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 200L,
          headers = list('content-type' = 'text/html'),
          body = "server 1"
        )
      }
    )
  )

  expect_equal(length(list_servers()), 1)

  s2 <- httpserver::start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        list(
          status = 200L,
          headers = list('content-type' = 'text/html'),
          body = "server 2"
        )
      }
    )
  )

  expect_equal(length(list_servers()), 2)

  r1 <- fetch(local_url("/", s1$get_port()), gzip = FALSE)
  r2 <- fetch(local_url("/", s2$get_port()))

  expect_equal(r1$status_code, 200)
  expect_equal(r2$status_code, 200)

  expect_identical(rawToChar(r1$content), "server 1")
  expect_identical(rawToChar(r2$content), "server 2")

  expect_identical(curl::parse_headers_list(r1$headers)$`content-type`, "text/html")
  expect_identical(curl::parse_headers_list(r1$headers)$`content-length`, "8")

  s1$stop()
  expect_equal(length(list_servers()), 1)
  stop_all_servers()
  expect_equal(length(list_servers()), 0)
})

local({
  # empty and NULL headers are ok ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- httpserver::start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        if (req$path_info == "/NULL") {
          list(
            status = 200L,
            headers = NULL,
            body = ""
          )
        } else if (req$path_info == "/emptylist") {
          list(
            status = 200L,
            headers = list(),
            body = ""
          )
        } else if (req$path_info == "/noheaders") {
          list(
            status = 200L,
            body = ""
          )
        }
      }
    )
  )

  on.exit(s$stop())
  
  expect_equal(length(list_servers()), 1)

  r <- fetch(local_url("/NULL", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, raw())

  r <- fetch(local_url("/emptylist", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, raw())

  r <- fetch(local_url("/noheaders", s$get_port()))
  expect_equal(r$status_code, 200)
  expect_identical(r$content, raw())
})

local({
  # content length depends on the presence of 'body' ----

  if (!requireNamespace("curl", quietly = TRUE)) { return (NULL) }

  s <- httpserver::start_server(
    "127.0.0.1",
    random_port(),
    list(
      call = function(req) {
        if (req$path_info == "/ok") {
          list(
            status = 200L,
            headers = list('content-type' = 'text/html'),
            body = if (req$request_method != "HEAD") raw()
          )
        } else if (req$path_info == "/nullbody") {
          list(
            status = 204L,
            headers = list('content-type' = 'text/html'),
            body = NULL
          )
        } else if (req$path_info == "/nobody") {
          list(
            status = 204L,
            headers = list('content-type' = 'text/html')
          )
        }
      }
    )
  )
  
  on.exit(s$stop())
  
  expect_equal(length(list_servers()), 1)

  r1 <- fetch(local_url("/ok", s$get_port()), gzip = FALSE)
  # head requests should not have a body.
  r2 <- fetch(local_url("/ok", s$get_port()), curl::new_handle(nobody = TRUE))
  r3 <- fetch(local_url("/nullbody", s$get_port()))
  r4 <- fetch(local_url("/nobody", s$get_port()))

  expect_equal(r1$status_code, 200)
  expect_equal(r2$status_code, 200)
  expect_equal(r3$status_code, 204)
  expect_equal(r3$status_code, 204)

  expect_equal(length(r1$content), 0)
  expect_equal(length(r2$content), 0)
  expect_equal(length(r3$content), 0)
  expect_equal(length(r4$content), 0)

  expect_identical(curl::parse_headers_list(r1$headers)$`content-length`, "0")
  # head requests *can* have a content-length, but they don't have to.
  expect_identical(curl::parse_headers_list(r2$headers)$`content-length`, NULL)
  expect_identical(curl::parse_headers_list(r3$headers)$`content-length`, NULL)
  expect_identical(curl::parse_headers_list(r4$headers)$`content-length`, NULL)
})
