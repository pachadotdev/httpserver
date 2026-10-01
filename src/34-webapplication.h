#ifndef HTTPSERVER_34_WEBAPPLICATION_H
#define HTTPSERVER_34_WEBAPPLICATION_H

// ============================================================================
// Utility functions
// ============================================================================

std::string normalize_header_name(const std::string &name) {
  std::string result = name;
  for (std::string::iterator it = result.begin(); it != result.end(); it++) {
    if (*it == '-')
      *it = '_';
    else if (*it >= 'A' && *it <= 'Z')
      *it = *it + ('a' - 'A');
  }
  return result;
}

const std::string &get_status_description(int code) {
  static std::map<int, std::string> status_descs;
  static std::string unknown("Dunno");
  if (status_descs.size() == 0) {
    status_descs[100] = "Continue";
    status_descs[101] = "Switching Protocols";
    status_descs[200] = "OK";
    status_descs[201] = "Created";
    status_descs[202] = "Accepted";
    status_descs[203] = "Non-Authoritative Information";
    status_descs[204] = "No Content";
    status_descs[205] = "Reset Content";
    status_descs[206] = "Partial Content";
    status_descs[300] = "Multiple Choices";
    status_descs[301] = "Moved Permanently";
    status_descs[302] = "Found";
    status_descs[303] = "See Other";
    status_descs[304] = "Not Modified";
    status_descs[305] = "Use Proxy";
    status_descs[307] = "Temporary Redirect";
    status_descs[400] = "Bad Request";
    status_descs[401] = "Unauthorized";
    status_descs[402] = "Payment Required";
    status_descs[403] = "Forbidden";
    status_descs[404] = "Not Found";
    status_descs[405] = "Method Not Allowed";
    status_descs[406] = "Not Acceptable";
    status_descs[407] = "Proxy Authentication Required";
    status_descs[408] = "Request Timeout";
    status_descs[409] = "Conflict";
    status_descs[410] = "Gone";
    status_descs[411] = "Length Required";
    status_descs[412] = "Precondition Failed";
    status_descs[413] = "Request Entity Too Large";
    status_descs[414] = "Request-URI Too Long";
    status_descs[415] = "Unsupported Media Type";
    status_descs[416] = "Requested Range Not Satisifable";
    status_descs[417] = "Expectation Failed";
    status_descs[500] = "Internal Server Error";
    status_descs[501] = "Not Implemented";
    status_descs[502] = "Bad Gateway";
    status_descs[503] = "Service Unavailable";
    status_descs[504] = "Gateway Timeout";
    status_descs[505] = "HTTP Version Not Supported";
  }
  std::map<int, std::string>::iterator it = status_descs.find(code);
  if (it != status_descs.end())
    return it->second;
  else
    return unknown;
}

// A generic HTTP response to send when an error (uncaught in the R code)
// happens during processing a request.
list error_response() {
  ASSERT_MAIN_THREAD()
  return writable::list({"status"_nm = 500L,
                         "headers"_nm = writable::list(
                             {"Content-Type"_nm = "text/plain; charset=UTF-8"}),
                         "body"_nm = "An exception occurred."});
}

// An analog to error_response, but this returns an shared_ptr<HttpResponse>
// instead of an list, doesn't involve any R objects, and can be run on
// the background thread.
std::shared_ptr<HttpResponse>
error_response(std::shared_ptr<HttpRequest> p_request, int code) {
  std::string description = get_status_description(code);
  std::string content = to_string(code) + " " + description + "\n";

  std::vector<uint8_t> response_data(content.begin(), content.end());

  // Freed in on_response_written
  std::shared_ptr<DataSource> p_data_source =
      std::make_shared<InMemoryDataSource>(response_data);

  return std::shared_ptr<HttpResponse>(
      new HttpResponse(p_request, code, description, p_data_source),
      auto_deleter_background<HttpResponse>);
}

// Given a URL path like "/foo?abc=123", removes the '?' and everything after.
std::pair<std::string, std::string> split_query_string(const std::string &url) {
  size_t qs_index = url.find('?');
  std::string path, query_string;
  if (qs_index == std::string::npos)
    path = url;
  else {
    path = url.substr(0, qs_index);
    query_string = url.substr(qs_index);
  }

  return std::pair<std::string, std::string>(path, query_string);
}

void request_to_env(std::shared_ptr<HttpRequest> p_request,
                    environment *p_env) {
  ASSERT_MAIN_THREAD()
  environment &env = *p_env;

  std::pair<std::string, std::string> url_query =
      split_query_string(p_request->url());
  std::string &path = url_query.first;
  std::string &query_string = url_query.second;

  env["request_method"] = p_request->method();
  env["script_name"] = std::string("");
  env["path_info"] = path;
  env["query_string"] = query_string;

  env["rook.version"] = std::string("1.1-0");
  env["rook.url_scheme"] = std::string("http");

  Address addr = p_request->server_address();
  env["server_name"] = addr.host;
  std::ostringstream portstr;
  portstr << addr.port;
  env["server_port"] = portstr.str();

  Address raddr = p_request->client_address();
  env["remote_addr"] = raddr.host;
  std::ostringstream rportstr;
  rportstr << raddr.port;
  env["remote_port"] = rportstr.str();

  const RequestHeaders &headers = p_request->headers();
  writable::strings raw_headers(headers.size());
  writable::strings raw_header_names(headers.size());

  for (RequestHeaders::const_iterator it = headers.begin(); it != headers.end();
       it++) {
    int idx = std::distance(headers.begin(), it);
    env["http_" + normalize_header_name(it->first)] = it->second;
    raw_header_names[idx] = to_lower(it->first);
    raw_headers[idx] = it->second;
  }
  raw_headers.attr("names") = raw_header_names;

  env["headers"] = raw_headers;
}

std::shared_ptr<HttpResponse>
list_to_response(std::shared_ptr<HttpRequest> p_request, const list &response) {
  ASSERT_MAIN_THREAD()

  if (Rf_isNull(response) || response.size() == 0) {
    return std::shared_ptr<HttpResponse>();
  }

  strings resp_names = response.names();

  int status = as_cpp<int>(response["status"]);
  std::string status_desc = get_status_description(status);

  // Self-frees when response is written
  std::shared_ptr<DataSource> p_data_source;

  // HTTP 1xx, 204 and 304 responses (as well as responses to HEAD requests)
  // cannot have a body, so permit the body element to be missing or NULL, in
  // which case p_data_source will be nullptr.
  //
  // See https://tools.ietf.org/html/rfc7231#section-6.3.5 and
  //     https://tools.ietf.org/html/rfc7232#section-4.1
  bool has_body = response.contains("body") && !Rf_isNull(response["body"]);

  // The response can either contain:
  // - body_file: String value that names the file that should be streamed
  // - body: Character vector (which is charToRaw-ed) or raw vector, or NULL
  if (std::find(resp_names.begin(), resp_names.end(), "body_file") !=
      resp_names.end()) {
    std::shared_ptr<FileDataSource> p_fds = std::make_shared<FileDataSource>();
    FileDataSourceResult ret =
        p_fds->initialize(as_cpp<std::string>(response["body_file"]),
                          as_cpp<bool>(response["body_file_owned"]));
    if (ret != FDS_OK) {
      REprintf("%s", p_fds->last_error_message().c_str());
      return error_response(p_request, 500);
    }
    p_data_source = p_fds;
  } else if (has_body && Rf_isString(response["body"])) {
    raws response_bytes(package("base")["charToRaw"](response["body"]));
    p_data_source = std::make_shared<InMemoryDataSource>(
        std::vector<uint8_t>(response_bytes.begin(), response_bytes.end()));
  } else if (has_body) {
    raws response_bytes(response["body"]);
    p_data_source = std::make_shared<InMemoryDataSource>(
        std::vector<uint8_t>(response_bytes.begin(), response_bytes.end()));
  }

  std::shared_ptr<HttpResponse> p_resp(
      new HttpResponse(p_request, status, status_desc, p_data_source),
      auto_deleter_background<HttpResponse>);
  if (response.contains("headers") && !Rf_isNull(response["headers"])) {
    list response_headers(response["headers"]);
    strings header_names = response_headers.names();
    for (R_len_t i = 0; i < response_headers.size(); i++) {
      p_resp->add_header(std::string(header_names[i]),
                         as_cpp<std::string>(response_headers[i]));
    }
  }

  return p_resp;
}

void invoke_response_fun(std::function<void(std::shared_ptr<HttpResponse>)> fun,
                         std::shared_ptr<HttpRequest> p_request,
                         list response) {
  ASSERT_MAIN_THREAD()
  // new HttpResponse object. The callback will invoke
  // HttpResponse->write_response().
  std::shared_ptr<HttpResponse> p_response =
      list_to_response(p_request, response);
  fun(p_response);
}

// ============================================================================
// Methods
// ============================================================================

RWebApplication::RWebApplication(sexp on_headers, function on_body_data,
                                 function on_request, function on_wsopen,
                                 function on_wsmessage, function on_wsclose,
                                 list static_paths, list static_path_options)
    : _on_headers(on_headers), _on_body_data(on_body_data),
      _on_request(on_request), _on_wsopen(on_wsopen),
      _on_wsmessage(on_wsmessage), _on_wsclose(on_wsclose),
      _static_path_manager(static_paths, static_path_options) {
  ASSERT_MAIN_THREAD()
}

void RWebApplication::dispatch_headers(
    std::shared_ptr<HttpRequest> p_request,
    std::function<void(std::shared_ptr<HttpResponse>)> callback) {
  ASSERT_MAIN_THREAD()

  request_to_env(p_request, &p_request->env());

  // Call the R header-stage function. If an exception occurs during processing,
  // catch it and then send a generic error response.
  list response;
  try {
    sexp result(function(_on_headers)(p_request->env()));
    if (Rf_isNull(result)) {
      std::shared_ptr<HttpResponse> null_ptr;
      callback(null_ptr);
      return;
    }
    response = list(result);
  } catch (unwind_exception &e) {
    debug_log("Interrupt occurred in _on_headers", LOG_INFO);
    response = error_response();
  } catch (...) {
    debug_log("Exception occurred in _on_headers", LOG_INFO);
    response = error_response();
  }

  // new HttpResponse object. The callback will invoke
  // HttpResponse->write_response(), which adds a callback to destroy(), which
  // deletes the object.
  std::shared_ptr<HttpResponse> p_response =
      list_to_response(p_request, response);
  callback(p_response);
}

void RWebApplication::dispatch_body(
    std::shared_ptr<HttpRequest> p_request,
    std::shared_ptr<std::vector<char>> data,
    std::function<void(std::shared_ptr<HttpResponse>)> error_callback) {
  ASSERT_MAIN_THREAD()
  debug_log("RWebApplication::dispatch_body", LOG_DEBUG);

  // The background thread may already have queued more body chunks after an
  // earlier dispatch failure. Do not process them.
  if (p_request->is_response_scheduled())
    return;

  writable::raws raw_vector(data->size());
  std::copy(data->begin(), data->end(), raw_vector.begin());
  try {
    _on_body_data(p_request->env(), raw_vector);
  } catch (...) {
    debug_log("Exception occurred in _on_body_data", LOG_INFO);
    // Send an error message to the client. More dispatch stages may already
    // have been queued before the error callback is reached.
    //
    // Note that some (most?) clients won't correctly handle a response that's
    // sent early, before the request is completed.
    // https://stackoverflow.com/a/18370751/412655
    error_callback(list_to_response(p_request, error_response()));
  }
}

void RWebApplication::dispatch_complete(
    std::shared_ptr<HttpRequest> p_request,
    std::function<void(std::shared_ptr<HttpResponse>)> callback) {
  ASSERT_MAIN_THREAD()
  debug_log("RWebApplication::dispatch_complete", LOG_DEBUG);

  // Pass callback to R:
  // invoke_response_fun(callback, p_request, _1)
  std::function<void(list)> *callback_wrapper =
      new std::function<void(list)>(std::bind(
          invoke_response_fun, callback, p_request, std::placeholders::_1));

  SEXP callback_xptr =
      PROTECT(R_MakeExternalPtr(callback_wrapper, R_NilValue, R_NilValue));

  // We previously encountered an error processing the body. Don't call into
  // the R call/_on_request() function. We need to signal the HttpRequest
  // object to let it know that we had an error.
  if (p_request->is_response_scheduled()) {
    invoke_cpp_callback(SEXP(list()), callback_xptr);
  } else {

    // Call the R call() function, and pass it the callback xptr so it can
    // asynchronously pass data back to C++.
    try {
      _on_request(p_request->env(), callback_xptr);

      // On the R side, the application's call() function will catch errors that happen
      // in the user-defined call() function, but if an error happens outside of
      // that scope, or if another uncaught exception happens (like an interrupt
      // if Ctrl-C is pressed), then it will bubble up to here, where we'll
      // catch it and deal with it.

    } catch (unwind_exception &e) {
      debug_log("Interrupt occurred in _on_request", LOG_INFO);
      invoke_cpp_callback(SEXP(error_response()), callback_xptr);
    } catch (...) {
      debug_log("Exception occurred in _on_request", LOG_INFO);
      invoke_cpp_callback(SEXP(error_response()), callback_xptr);
    }
  }

  UNPROTECT(1);
}

void RWebApplication::dispatch_wsopen(
    std::shared_ptr<HttpRequest> p_request,
    std::function<void(void)> error_callback) {
  ASSERT_MAIN_THREAD()
  std::shared_ptr<WebSocketConnection> p_conn = p_request->websocket();
  if (!p_conn) {
    return;
  }

  request_to_env(p_request, &p_request->env());
  try {
    _on_wsopen(externalize_shared_ptr(p_conn), p_request->env());
  } catch (...) {
    error_callback();
  }
}

void RWebApplication::dispatch_wsmessage(
    std::shared_ptr<WebSocketConnection> p_conn, bool binary,
    std::shared_ptr<std::vector<char>> data,
    std::function<void(void)> error_callback) {
  ASSERT_MAIN_THREAD()
  try {
    if (binary)
      _on_wsmessage(externalize_shared_ptr(p_conn), binary,
                    std::vector<uint8_t>(data->begin(), data->end()));
    else
      _on_wsmessage(externalize_shared_ptr(p_conn), binary,
                    std::string(data->begin(), data->end()));
  } catch (...) {
    error_callback();
  }
}

void RWebApplication::dispatch_wsclose(
    std::shared_ptr<WebSocketConnection> p_conn) {
  ASSERT_MAIN_THREAD()
  _on_wsclose(externalize_shared_ptr(p_conn));
}

// ============================================================================
// Static file serving
// ============================================================================
//
// Unlike most of the methods for an RWebApplication, these ones are called on
// the background thread.

std::shared_ptr<HttpResponse>
RWebApplication::static_file_response(std::shared_ptr<HttpRequest> p_request) {
  ASSERT_BACKGROUND_THREAD()

  // If there's any Upgrade header, don't try to serve a static file. Just
  // fall through, even if the path is one that is in the StaticPathManager.
  if (p_request->has_header("Upgrade")) {
    return std::shared_ptr<HttpResponse>();
  }

  // Strip off query string
  std::pair<std::string, std::string> url_query =
      split_query_string(p_request->url());
  std::string url_path = do_decode_uri(url_query.first, true);

  std::experimental::optional<std::pair<StaticPath, std::string>> sp_pair =
      _static_path_manager.match_static_path(url_path);

  if (!sp_pair) {
    // This was not a static path. Fall through to the R code to handle this
    // path.
    return std::shared_ptr<HttpResponse>();
  }

  // If we get here, we've matched a static path.

  const StaticPath &sp = sp_pair->first;
  // Note that the subpath may include leading dirs, as in "foo/bar/abc.txt".
  const std::string &subpath = sp_pair->second;

  // This is an excluded path
  if (*sp.options.exclude) {
    return std::shared_ptr<HttpResponse>();
  }

  // Validate headers (if validation pattern was provided).
  if (!sp.options.validate_request_headers(p_request->headers())) {
    return error_response(p_request, 403);
  }

  // Check that method is GET or HEAD; error otherwise.
  std::string method = p_request->method();
  if (method != "GET" && method != "HEAD") {
    return error_response(p_request, 400);
  }

  // Make sure that there's no message body.
  if ((p_request->has_header("Content-Length") &&
       p_request->get_header("Content-Length") != "0") ||
      p_request->has_header("Transfer-Encoding")) {
    return error_response(p_request, 400);
  }

  // Disallow ".." in paths. (Browsers collapse them anyway, so no normal
  // requests should contain them.) The ones we care about will always be
  // between two slashes, as in "/foo/../bar", except in the case where it's
  // at the end of the URL, as in "/foo/..". Paths like "/foo../" or "/..foo/"
  // are OK.
  if (url_path.find("/../") != std::string::npos ||
      (url_path.length() >= 3 &&
       url_path.substr(url_path.length() - 3, 3) == "/..")) {
    if (*sp.options.fallthrough) {
      return std::shared_ptr<HttpResponse>();
    } else {
      return error_response(p_request, 400);
    }
  }

  // Path to local file on disk
  std::string local_path = sp.path;
  if (subpath != "") {
    local_path += "/" + subpath;
  }

  if (is_directory(local_path)) {
    if (*sp.options.index_html) {
      local_path = local_path + "/" + "index.html";
    }
  }

  // The URL traversal check above is not sufficient: a file below the
  // configured root may itself be a symlink. Resolve the final path before
  // opening it so static serving cannot escape the declared directory.
  if (path_exists(local_path) && !is_path_within(sp.path, local_path)) {
    if (*sp.options.fallthrough) {
      return std::shared_ptr<HttpResponse>();
    } else {
      return error_response(p_request, 403);
    }
  }

  std::shared_ptr<FileDataSource> p_data_source =
      std::make_shared<FileDataSource>();
  FileDataSourceResult ret = p_data_source->initialize(local_path, false);

  if (ret != FDS_OK) {
    if (ret == FDS_NOT_EXIST || ret == FDS_ISDIR) {
      if (*sp.options.fallthrough) {
        return std::shared_ptr<HttpResponse>();
      } else {
        return error_response(p_request, 404);
      }
    } else {
      return error_response(p_request, 500);
    }
  }

  // Use local_path instead of subpath, because if the subpath is "/foo/" and
  // *(sp.options.index_html) is true, then the local_path will be
  // "/foo/index.html". We need to use the latter to determine mime type.
  std::string content_type =
      find_mime_type(find_extension(basename(local_path)));
  if (content_type == "") {
    content_type = "application/octet-stream";
  } else if (content_type == "text/html") {
    // Add the encoding if specified by the options.
    if (*sp.options.html_charset != "") {
      content_type = "text/html; charset=" + *sp.options.html_charset;
    }
  }

  // Check if the client has an up-to-date copy of the file in cache. To do
  // this, compare the If-Modified-Since header to the file's mtime.
  bool client_cache_is_valid = false;
  if (p_request->has_header("If-Modified-Since")) {
    time_t file_mtime = p_data_source->get_mtime();
    time_t if_mod_since =
        parse_http_date_string(p_request->get_header("If-Modified-Since"));

    if (file_mtime != 0 && if_mod_since != 0 && file_mtime <= if_mod_since) {
      client_cache_is_valid = true;
    }
  }

  // ==================================
  // Create the HTTP response
  // ==================================

  // Default status code at this point is 200.
  int status_code = 200;

  // This is the pointer that will be passed to the new HttpResponse. We'll
  // start by setting it to point to the same thing as p_data_source, but it can
  // be unset based on various conditions, which means that no body data will
  // be sent.
  std::shared_ptr<FileDataSource> p_data_source2 = p_data_source;

  if (method == "HEAD") {
    p_data_source2.reset();
  }

  if (client_cache_is_valid) {
    p_data_source2.reset();
    status_code = 304;
  }

  std::shared_ptr<HttpResponse> p_response = std::shared_ptr<HttpResponse>(
      new HttpResponse(p_request, status_code,
                       get_status_description(status_code), p_data_source2),
      auto_deleter_background<HttpResponse>);

  ResponseHeaders &resp_headers = p_response->headers();

  // Add extra user-specified headers.
  const ResponseHeaders &extra_resp_headers = *sp.options.headers;
  if (extra_resp_headers.size() != 0) {
    ResponseHeaders::const_iterator it;
    for (it = extra_resp_headers.begin(); it != extra_resp_headers.end();
         it++) {
      if (status_code == 304) {
        // For a 304 response, only a few headers should be added. See
        // https://tools.ietf.org/html/rfc7232#section-4.1
        // (Date is automatically added in the HttpResponse.)
        if (it->first == "Cache-Control" || it->first == "Content-Location" ||
            it->first == "ETag" || it->first == "Expires" ||
            it->first == "Vary") {
          resp_headers.push_back(*it);
        }

      } else {
        // For a normal 200 response, add all headers.
        resp_headers.push_back(*it);
      }
    }
  }

  if (status_code != 304) {
    // Set the Content-Length here so that both GET and HEAD requests will get
    // it. If we didn't set it here, the response for the GET would
    // automatically set the Content-Length (by using the FileDataSource), but
    // the response for the HEAD would not.
    resp_headers.push_back(
        std::make_pair("Content-Length", to_string(p_data_source->size())));
    resp_headers.push_back(std::make_pair("Content-Type", content_type));
    resp_headers.push_back(std::make_pair(
        "Last-Modified", http_date_string(p_data_source->get_mtime())));
  }

  return p_response;
}

StaticPathManager &RWebApplication::get_static_path_manager() {
  return _static_path_manager;
}

#endif
