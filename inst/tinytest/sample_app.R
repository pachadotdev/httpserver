library(httpserver)

# the working directory is set by the caller (see test-traffic.r) before this
# file is sourced, so a plain file.path() resolves paths correctly here.
test_path <- function(...) file.path(...)

content <- list(
  status = 200L,
  headers = list(
    'content-type' = 'text/html'
  ),
  body = "abc"
)


app_handle <- start_server(
  "0.0.0.0",
  app_port,
  list(
    on_headers = function(req) {
      if (req$path_info == "/header") {
        content$body <- "this is a response from on_headers()\n"
        return(content)
      } else if (req$path_info == "/header-delay") {
        print(capture.output(print(str(as.list(req)))))
        Sys.sleep(5)
        return(content)
      } else if (req$path_info == "/header-print") {
        print(capture.output(print(str(as.list(req)))))
        return(content)
      } else if (req$path_info == "/header-error") {
        stop("error in app (header)")
      } else {
        return(NULL)
      }
    },
    call = function(req) {
      if (req$path_info %in% c("/", "/sync")) {
        return(content)
      } else if (req$path_info == "/body-error") {
        return(content)
      } else if (req$path_info == "/sync-delay") {
        Sys.sleep(5)
        return(content)
      } else if (req$path_info == "/sync-error") {
        stop("error in app (sync)")
      } else if (req$path_info == "/async") {
        later2::promise(function(resolve, reject) {
          resolve(content)
        })
      } else if (req$path_info == "/async-delay") {
        later2::promise(function(resolve, reject) {
          Sys.sleep(5)
          resolve(content)
        })
      } else if (req$path_info == "/async-error") {
        later2::promise(function(resolve, reject) {
          stop("error in app (async)")
        })
      } else {
        stop("unknown request path:", req$path_info)
      }
    },
    static_paths = list(
      "/static" = test_path("apps/content"),
      "/static_fallthrough" = static_path(
        test_path("apps/content"),
        fallthrough = TRUE
      )
    )
  )
)
