# these tests are time-sensitive, which makes cran unhappy.

path_example_site <- function(...) {
  system.file("example-static-site", ..., package = "httpserver")
}

index_file_content <- raw_file_content(path_example_site("index.html"))

expect_example_site <- function(port, host = "127.0.0.1") {
  res <- fetch(local_url("/index.html", port), gzip = FALSE)
  expect_equal(res$status_code, 200)
  expect_identical(res$content, index_file_content)
}

start_example_server <- function(port) {
  actual_port <- if (is.null(port)) 7446 else port

  r <- callr::r_bg(
    function(port) {
      ex <- system.file("example-static-site", package = "httpserver")
      httpuv::run_static_server(
        ex,
        port = port,
        background = FALSE,
        browse = FALSE
      )
    },
    list(port = port)
  )

  # poll until the port is actually bound by the background process, rather
  # than relying on stderr output. checking for *any* stderr output is racy:
  # in some environments (e.g. docker containers with incomplete locale
  # data), the background process can emit a warning almost immediately,
  # which would make us think the server is ready before it's actually
  # listening, leading to "couldn't connect to server" errors.
  # `skip()` is a testthat function and isn't available under tinytest, so we
  # signal a skip by returning NULL and letting the caller do `return(NULL)`
  # (the same pattern already used elsewhere in this file).
  max <- Sys.time() + 2
  while (isTRUE(httpserver:::is_port_available(actual_port))) {
    if (!r$is_alive()) {
      message(
        "server process exited before starting up:\n",
        paste(r$read_error_lines(), collapse = "\n")
      )
      return(NULL)
    }
    if (Sys.time() > max) {
      message("server didn't start up in 2 seconds")
      r$kill()
      return(NULL)
    }
    Sys.sleep(0.1)
  }

  r
}

local({
  # run_static_server() in foreground with custom port ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  port <- random_port()

  r <- start_example_server(port)
  if (is.null(r)) { return(NULL) }
  on.exit(
    {
      r$kill()
    },
    add = TRUE
  )

  expect_example_site(port)
})

local({
  # run_static_server() in foreground with default port ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }
  if (isFALSE(httpserver:::is_port_available(7446))) { return(NULL) }

  r <- start_example_server(NULL)
  if (is.null(r)) { return(NULL) }
  on.exit(
    {
      r$kill()
    },
    add = TRUE
  )

  expect_example_site(7446)
})

local({
  # run_static_server() throws an error for invalid ports ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  on.exit({
    stop_all_servers()
  }) # in case of a test failure

  expect_error(
    run_static_server(path_example_site(), port = 0, background = TRUE)
  )
  expect_error(
    run_static_server(path_example_site(), port = 700:720, background = TRUE)
  )
  expect_error(
    run_static_server(path_example_site(), port = 74469, background = TRUE)
  )
  expect_error(
    run_static_server(path_example_site(), port = "1234", background = TRUE)
  )
})

local({
  # run_static_server() throws an error if the requested port is used ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  on.exit({
    stop_all_servers()
  }) # in case of a test failure

  s1 <- run_static_server(path_example_site(), background = TRUE, browse = FALSE)

  expect_error(
    run_static_server(
      path_example_site(),
      port = s1$get_port(),
      background = TRUE,
      browse = FALSE
    )
  )
})

local({
  # run_static_server() in background uses default port ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }
  if (isFALSE(httpserver:::is_port_available(7446))) { return(NULL) }

  s <- run_static_server(path_example_site(), background = TRUE, browse = FALSE)
  on.exit(
    {
      stop_server(s)
    },
    add = TRUE
  )

  expect_example_site(7446)
})

local({
  # run_static_server() in background uses default port or random port ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  if (isFALSE(httpserver:::is_port_available(7446))) { return(NULL) }

  s1 <- run_static_server(path_example_site(), background = TRUE, browse = FALSE)
  on.exit(
    {
      s1$stop()
    },
    add = TRUE
  )

  s2 <- run_static_server(path_example_site(), background = TRUE, browse = FALSE)
  on.exit(
    {
      s2$stop()
    },
    add = TRUE
  )

  expect_example_site(7446)
  expect_example_site(s2$get_port())
})

local({
  # run_static_server() in background errors if requested port is in use ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  s1 <- run_static_server(path_example_site(), background = TRUE, browse = FALSE)
  on.exit(
    {
      s1$stop()
    },
    add = TRUE
  )

  used_port <- s1$get_port()

  expect_error({
    s2 <- run_static_server(
      path_example_site(),
      port = used_port,
      background = TRUE,
      browse = FALSE
    )
    s2$stop() # clean up in case test fails
  })
})

local({
  # run_static_server() prints informative console messages ----

  if (Sys.getenv("tinyhttpserver_full_testing") != "yes") { return(NULL) }
  if (!requireNamespace("curl")) { return(NULL) }

  # tinytest has no expect_snapshot(). message() writes to the "message"
  # stream, so we capture it with capture.output(type = "message") and
  # compare against a fixed expected value, after redacting the parts that
  # vary between runs/machines (site path and port).
  msgs <- capture.output(
    {
      s <- run_static_server(
        path_example_site(),
        background = TRUE,
        browse = FALSE
      )
      s$stop()
    },
    type = "message"
  )

  msgs <- sub(path_example_site(), "/users/user/path/to/site", msgs, fixed = TRUE)
  msgs <- sub(":\\d+$", ":port", msgs)

  expect_equal(
    msgs,
    c(
      "serving: '/users/user/path/to/site'",
      "view at: http://127.0.0.1:port"
    )
  )
})
