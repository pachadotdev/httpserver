#ifndef HTTPSERVER_29_HTTPREQUEST_H
#define HTTPSERVER_29_HTTPREQUEST_H

enum Protocol { HTTP, WebSockets };

// HttpRequest is a bit of a misnomer -- a HttpRequest object represents a
// single connection, on which multiple actual HTTP requests can be made.
class HttpRequest : public WebSocketConnectionCallbacks,
                    public std::enable_shared_from_this<HttpRequest> {
private:
  uv_loop_t *_p_loop;
  std::shared_ptr<WebApplication> _p_web_application;
  VariantHandle _handle;
  std::shared_ptr<Socket> _p_socket;
  http_parser _parser;
  Protocol _protocol;
  std::string _url;
  RequestHeaders _headers;
  std::string _last_header_field;
  std::shared_ptr<WebSocketConnection> _p_web_socket_connection;

  // `_env` is an shared_ptr<environment> instead of an environment because it
  // must be created and deleted on the main thread. However, the creation and
  // deletion of HttpRequest objects happens on the background thread, and so
  // the lifetime of the environment can't be strictly tied to the lifetime of
  // the HttpRequest. It is instantiated with a deleter function that ensures
  // deletion happens on the main thread.
  std::shared_ptr<environment> _env;
  void _new_request();
  void _initialize_env();

  // _ignore_new_data is used in cases where we rejected a request (by sending
  // a response with a non-100 status code) before its body was received. We
  // don't want to close the connection because the response might not be
  // sent yet, but we don't want to parse any more data from this connection.
  // (You would think uv_stop_read could be called, but it seems to prevent
  // the response from being written as well.)
  bool _ignore_new_data;

  bool _is_closing;

  // This starts false, and in the case of a connection upgrade, gets set to
  // true after the headers are complete.
  bool _is_upgrade;

  void _parse_http_data(char *buf, const ssize_t n);
  // Parse data that has been stored in the buffer.
  void _parse_http_data_from_buffer();

  bool _response_scheduled;
  // True when the HttpRequest object is handling an HTTP request; gets set to
  // false when the response is written.
  bool _handling_request;

  // For buffering the incoming HTTP request when data comes in while waiting
  // for R to process headers.
  std::vector<char> _request_buffer;

  // Most of the methods in HttpRequest run on a background thread. Some
  // methods run on the main thread. This is used by the main-thread methods
  // to schedule callbacks to run on the background thread.
  CallbackQueue *_background_queue;

  // Used to keep track of state when parsing headers. This is needed because
  // sometimes the header fields and values can be split across multiple TCP
  // messages, resulting in multiple calls to _on_header_field or
  // _on_header_value.
  enum LastHeaderState { START, FIELD, VALUE };
  LastHeaderState _last_header_state;

public:
  HttpRequest(uv_loop_t *p_loop, std::shared_ptr<WebApplication> p_web_application,
              std::shared_ptr<Socket> p_socket, CallbackQueue *background_queue)
      : _p_loop(p_loop), _p_web_application(p_web_application), _p_socket(p_socket),
        _protocol(HTTP), _ignore_new_data(false), _is_closing(false),
        _is_upgrade(false), _response_scheduled(false),
        _handling_request(false), _background_queue(background_queue) {
    ASSERT_BACKGROUND_THREAD()
    uv_tcp_init(p_loop, &_handle.tcp);
    _handle.is_tcp = true;
    // This is used by the macro-defined callbacks like _on_request_read
    _handle.stream.data = this;

    http_parser_init(&_parser, HTTP_REQUEST);
    // This is used by the macro-defined callbacks like _on_message_begin
    _parser.data = this;

    _last_header_state = START;
  }

  virtual ~HttpRequest() {
    ASSERT_BACKGROUND_THREAD()
    debug_log("HttpRequest::~HttpRequest", LOG_DEBUG);
    _p_web_socket_connection.reset();
  }

  uv_stream_t *handle();
  std::shared_ptr<WebSocketConnection> websocket() const {
    return _p_web_socket_connection;
  }
  Address client_address();
  Address server_address();
  environment &env();

  void handle_request();

  std::string method() const;
  std::string url() const;
  const RequestHeaders &headers() const;

  bool has_header(const std::string &name) const;
  bool has_header(const std::string &name, const std::string &value,
                 bool ci = false) const;
  std::string get_header(const std::string &name) const;

  // Is the request an Upgrade (i.e. WebSocket connection)?
  bool is_upgrade() const;

  void send_wsframe(const char *p_header, size_t header_size, const char *p_data,
                   size_t data_size, const char *p_footer, size_t footer_size);
  void close_wssocket();

  // Call this function from the main thread to indicate that a response has
  // been scheduled. This is needed because sometimes by the time the main
  // thread knows that it needs to send a response, the bg thread will have
  // kept going and scheduled another call into the main thread to send a
  // response.
  void response_scheduled();
  bool is_response_scheduled();

  // This function should be called when a single request has been completed
  // (when the response has been sent). It is currently used to detect
  // pipelined HTTP requests.
  void request_completed();

  void _call_r_on_ws_open();
  void _schedule_on_headers_complete_complete(
      std::shared_ptr<HttpResponse> p_response);
  void _on_headers_complete_complete(std::shared_ptr<HttpResponse> p_response);
  void _schedule_on_body_error(std::shared_ptr<HttpResponse> p_response);
  void _on_body_error(std::shared_ptr<HttpResponse> p_response);
  void _schedule_on_message_complete_complete(
      std::shared_ptr<HttpResponse> p_response);
  void _on_message_complete_complete(std::shared_ptr<HttpResponse> p_response);

public:
  // Callbacks
  virtual int _on_message_begin(http_parser *p_parser);
  virtual int _on_url(http_parser *p_parser, const char *p_at, size_t length);
  virtual int _on_status(http_parser *p_parser, const char *p_at, size_t length);
  virtual int _on_header_field(http_parser *p_parser, const char *p_at,
                               size_t length);
  virtual int _on_header_value(http_parser *p_parser, const char *p_at,
                               size_t length);
  virtual int _on_headers_complete(http_parser *p_parser);
  virtual int _on_body(http_parser *p_parser, const char *p_at, size_t length);
  virtual int _on_message_complete(http_parser *p_parser);

  virtual void on_wsmessage(bool binary, const char *data, size_t len);
  virtual void on_wsclose(int code);

  // Update whether or not this HttpRequest is to be upgraded. This is called
  // from _on_headers_complete().
  void update_upgrade_status();

  void _on_closed(uv_handle_t *handle);
  void close();
  void schedule_close();
  void _on_request_read(uv_stream_t *, ssize_t nread, const uv_buf_t *buf);
  void _on_response_write(int status);

  void _initialize_socket() {
    // Coerce to parent class
    std::shared_ptr<WebSocketConnectionCallbacks> this_base(
        std::static_pointer_cast<WebSocketConnectionCallbacks>(
            shared_from_this()));

    _p_web_socket_connection = std::shared_ptr<WebSocketConnection>(
        new WebSocketConnection(this->_p_loop, this_base),
        auto_deleter_background<WebSocketConnection>);

    _p_socket->add_connection(shared_from_this());
  }
};

// Same for Websocketconnection
// Factory function needed because we can't call shared_from_this() inside the
// constructor.
inline std::shared_ptr<HttpRequest> create_http_request(
    uv_loop_t *p_loop, std::shared_ptr<WebApplication> p_web_application,
    std::shared_ptr<Socket> p_socket, CallbackQueue *background_queue) {
  ASSERT_BACKGROUND_THREAD()

  // The shared_ptr has a custom deleter which ensures that the HttpRequest is
  // deleted on the background thread.
  std::shared_ptr<HttpRequest> req(
      new HttpRequest(p_loop, p_web_application, p_socket, background_queue),
      auto_deleter_background<HttpRequest>);

  req->_initialize_socket();

  return req;
}

#define DECLARE_CALLBACK_1(type, function_name, return_type, type_1)           \
  return_type type##_##function_name(type_1 arg1);
#define DECLARE_CALLBACK_3(type, function_name, return_type, type_1, type_2,   \
                           type_3)                                             \
  return_type type##_##function_name(type_1 arg1, type_2 arg2, type_3 arg3);
#define DECLARE_CALLBACK_2(type, function_name, return_type, type_1, type_2)   \
  return_type type##_##function_name(type_1 arg1, type_2 arg2);

DECLARE_CALLBACK_1(HttpRequest, on_message_begin, int, http_parser *)
DECLARE_CALLBACK_3(HttpRequest, on_url, int, http_parser *, const char *,
                   size_t)
DECLARE_CALLBACK_3(HttpRequest, on_status, int, http_parser *, const char *,
                   size_t)
DECLARE_CALLBACK_3(HttpRequest, on_header_field, int, http_parser *,
                   const char *, size_t)
DECLARE_CALLBACK_3(HttpRequest, on_header_value, int, http_parser *,
                   const char *, size_t)
DECLARE_CALLBACK_1(HttpRequest, on_headers_complete, int, http_parser *)
DECLARE_CALLBACK_3(HttpRequest, on_body, int, http_parser *, const char *,
                   size_t)
DECLARE_CALLBACK_1(HttpRequest, on_message_complete, int, http_parser *)
DECLARE_CALLBACK_1(HttpRequest, on_closed, void, uv_handle_t *)
DECLARE_CALLBACK_3(HttpRequest, on_request_read, void, uv_stream_t *, ssize_t,
                   const uv_buf_t *)
DECLARE_CALLBACK_2(HttpRequest, on_response_write, void, uv_write_t *, int)

http_parser_settings &request_settings() {
  static http_parser_settings settings;
  settings.on_message_begin = HttpRequest_on_message_begin;
  settings.on_url = HttpRequest_on_url;
  settings.on_status = HttpRequest_on_status;
  settings.on_header_field = HttpRequest_on_header_field;
  settings.on_header_value = HttpRequest_on_header_value;
  settings.on_headers_complete = HttpRequest_on_headers_complete;
  settings.is_async_on_headers_complete = 1;
  settings.on_body = HttpRequest_on_body;
  settings.on_message_complete = HttpRequest_on_message_complete;
  return settings;
}

void on_alloc(uv_handle_t *, size_t suggested_size, uv_buf_t *buf) {
  ASSERT_BACKGROUND_THREAD()
  // Freed in HttpRequest::_on_request_read
  void *result = malloc(suggested_size);
  *buf = uv_buf_init((char *)result, suggested_size);
}

// Does a header field `name` exist?
bool HttpRequest::has_header(const std::string &name) const {
  return _headers.find(name) != _headers.end();
}

// Does a header field `name` exist and have a particular value? If ci is
// true, do a case-insensitive comparison of the value (fields are always
// case- insensitive.)
bool HttpRequest::has_header(const std::string &name, const std::string &value,
                            bool ci) const {
  RequestHeaders::const_iterator item = _headers.find(name);
  if (item == _headers.end())
    return false;

  if (ci) {
    return strcasecmp(item->second.c_str(), value.c_str()) == 0;
  } else {
    return item->second == value;
  }
}

// Return the value of a specified header. If the specified header isn't
// found, return "".
std::string HttpRequest::get_header(const std::string &name) const {
  RequestHeaders::const_iterator item = _headers.find(name);
  if (item == _headers.end())
    return "";

  return item->second;
}

uv_stream_t *HttpRequest::handle() { return &_handle.stream; }

Address HttpRequest::server_address() {
  Address address;

  if (_handle.is_tcp) {
    struct sockaddr_in addr = {};
    int len = sizeof(sockaddr_in);
    int r = uv_tcp_getsockname(&_handle.tcp, (struct sockaddr *)&addr, &len);
    if (r) {
      // TODO: warn?
      return address;
    }

    if (addr.sin_family != AF_INET) {
      // TODO: warn
      return address;
    }

    // addrstr is a pointer to static buffer, no need to free
    char *addrstr = inet_ntoa(addr.sin_addr);
    if (addrstr)
      address.host = std::string(addrstr);
    else {
      // TODO: warn?
    }
    address.port = ntohs(addr.sin_port);
  }

  return address;
}

Address HttpRequest::client_address() {
  Address address;

  if (_handle.is_tcp) {
    struct sockaddr_in addr = {};
    int len = sizeof(sockaddr_in);
    int r = uv_tcp_getpeername(&_handle.tcp, (struct sockaddr *)&addr, &len);
    if (r) {
      // TODO: warn?
      return address;
    }

    if (addr.sin_family != AF_INET) {
      // TODO: warn
      return address;
    }

    // addrstr is a pointer to static buffer, no need to free
    char *addrstr = inet_ntoa(addr.sin_addr);
    if (addrstr)
      address.host = std::string(addrstr);
    else {
      // TODO: warn?
    }
    address.port = ntohs(addr.sin_port);
  }

  return address;
}

// Each HttpRequest object represents a connection. Multiple actual HTTP
// requests can happen in sequence on this connection. Each time a new message
// starts, we need to reset some parts of the HttpRequest object.
void HttpRequest::_new_request() {
  ASSERT_BACKGROUND_THREAD()

  if (_handling_request) {
    err_printf("Error: pipelined HTTP requests not supported.\n");
    close();
  }

  _handling_request = true;
  _headers.clear();
  _response_scheduled = false;
  _last_header_state = START;

  // Schedule on main thread:
  //   this->_initialize_env();
  invoke_later(std::bind(&HttpRequest::_initialize_env, shared_from_this()));
}

void HttpRequest::_initialize_env() {
  ASSERT_MAIN_THREAD()
  environment base(R_BaseEnv);
  function new_env(base["new.env"]);

  // The deleter is called either when this function is called again, or when
  // the HttpRequest object is deleted. The deletion will happen on the
  // background thread; auto_deleter_main() schedules the deletion of the
  // environment object on the main thread.
  _env = std::shared_ptr<environment>(
      new environment(new_env("parent"_nm = R_EmptyEnv)),
      auto_deleter_main<environment>);
}

environment &HttpRequest::env() {
  ASSERT_MAIN_THREAD()
  return *_env;
}

std::string HttpRequest::method() const {
  return http_method_str((enum http_method)_parser.method);
}

std::string HttpRequest::url() const { return _url; }

const RequestHeaders &HttpRequest::headers() const { return _headers; }

void HttpRequest::response_scheduled() {
  ASSERT_MAIN_THREAD()
  debug_log("HttpRequest::response_scheduled", LOG_DEBUG);
  _response_scheduled = true;
}

bool HttpRequest::is_response_scheduled() {
  ASSERT_MAIN_THREAD()
  return _response_scheduled;
}

void HttpRequest::request_completed() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::request_completed", LOG_DEBUG);
  _handling_request = false;
}

// ============================================================================
// Miscellaneous callbacks for http parser
// ============================================================================

int HttpRequest::_on_message_begin(http_parser *) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_message_begin", LOG_DEBUG);
  _new_request();
  return 0;
}

int HttpRequest::_on_url(http_parser *, const char *p_at, size_t length) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_url", LOG_DEBUG);
  _url = std::string(p_at, length);
  return 0;
}

int HttpRequest::_on_status(http_parser *, const char *,
                            size_t) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_status", LOG_DEBUG);
  return 0;
}
int HttpRequest::_on_header_field(http_parser *, const char *p_at,
                                  size_t length) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_header_field", LOG_DEBUG);

  if (_last_header_state != FIELD) {
    _last_header_state = FIELD;
    _last_header_field.clear();
  }

  std::copy(p_at, p_at + length, std::back_inserter(_last_header_field));
  return 0;
}

int HttpRequest::_on_header_value(http_parser *, const char *p_at,
                                  size_t length) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_header_value", LOG_DEBUG);

  std::string value(p_at, length);

  if (_last_header_state != VALUE) {
    _last_header_state = VALUE;

    if (_headers.find(_last_header_field) != _headers.end()) {
      // If the field already exists. This can happen if there are multiple
      // headers with the same name, as in:
      //   foo: 1
      //   foo: 2

      if (_headers[_last_header_field].size() > 0) {
        // ...and is already non-empty...

        if (value.size() > 0) {
          // ...and this value is also non-empty, then combine using comma...
          value = _headers[_last_header_field] + "," + value;
        } else {
          // ...but if this value is empty, then use previous value (no-op).
          value = _headers[_last_header_field];
        }
      }
    }

    _headers[_last_header_field] = value;

  } else {
    // This is a subsequent call to this function when the http parser receives
    // another chunk of data for the same field. This can happen when there are
    // very large headers, as in:
    //   foo: 1234............5678
    // where the "...." is so long that it gets split across TCP messages.

    _headers[_last_header_field].append(value);
  }

  return 0;
}

// ============================================================================
// Connection upgrade
// ============================================================================

// Normally these functions shouldn't be necessary: we would just check for
// _parser.upgrade. They are present because we had to work around buggy
// proxies.

// This is called after the headers are complete. We don't want to set the
// upgrade status before all the headers have been processed.
// https://github.com/rstudio/httpuv/issues/161
void HttpRequest::update_upgrade_status() {
  ASSERT_BACKGROUND_THREAD()
  // Normally this should just be _parser.upgrade. But we also want to allow
  // Upgrade: WebSocket + Connection: close, in order to work around an issue
  // in RStudio Server's http proxying code with Firefox (only):
  // https://github.com/rstudio/rstudio/issues/2940
  // https://github.com/rstudio/shiny/issues/2064
  if (_parser.upgrade || _parser.flags & F_UPGRADE) {
    _is_upgrade = true;
  };
}

bool HttpRequest::is_upgrade() const { return _is_upgrade; }

// ============================================================================
// Headers complete
// ============================================================================

// This is called after http-parser has finished parsing the request headers.
// It uses later() to schedule the user's R on_headers() function. Always
// returns 0. Normally 0 indicates success for http-parser, while 1 and 2
// indicate errors or other conditions, but since we're processing the header
// asynchronously, we don't know at this point if there has been an error. If
// one of those conditions occurs, we'll set it later, but before we call
// http_parser_execute() again.
int HttpRequest::_on_headers_complete(http_parser *) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_headers_complete", LOG_DEBUG);
  update_upgrade_status();

  // Attempt static serving here. If the request is for a static path, this
  // will be a response object; if not, it will be an empty shared_ptr.
  std::shared_ptr<HttpResponse> p_response =
      _p_web_application->static_file_response(shared_from_this());

  if (p_response) {
    // The request was for a static path. Skip over the webapplication code
    // (which calls back into R on the main thread). Just add a call to
    // _on_headers_complete_complete to the queue on the background thread.
    std::function<void(void)> cb(
        std::bind(&HttpRequest::_on_headers_complete_complete,
                  shared_from_this(), p_response));
    _background_queue->push(cb);
    return 0;
  }

  std::function<void(std::shared_ptr<HttpResponse>)> schedule_bg_callback(
      std::bind(&HttpRequest::_schedule_on_headers_complete_complete,
                shared_from_this(), std::placeholders::_1));

  // Use later to schedule _p_web_application->on_headers(this,
  // schedule_bg_callback) to run on the main thread. That function in turn
  // calls this->_schedule_on_headers_complete_complete.
  invoke_later(std::bind(&WebApplication::on_headers, _p_web_application,
                         shared_from_this(), schedule_bg_callback));

  return 0;
}

// This is called at the end of WebApplication::on_headers(). It puts an item
// on the write queue and signals to the background thread that there's
// something there.
void HttpRequest::_schedule_on_headers_complete_complete(
    std::shared_ptr<HttpResponse> p_response) {
  ASSERT_MAIN_THREAD()
  debug_log("HttpRequest::_schedule_on_headers_complete_complete", LOG_DEBUG);

  if (p_response)
    response_scheduled();

  std::function<void(void)> cb(
      std::bind(&HttpRequest::_on_headers_complete_complete, shared_from_this(),
                p_response));
  _background_queue->push(cb);
}

// This is called after the user's R on_headers() function has finished. It can
// write a response, if on_headers() wants that. It also sets a status code for
// http-parser and then re-executes the parser. Runs on the background thread.
void HttpRequest::_on_headers_complete_complete(
    std::shared_ptr<HttpResponse> p_response) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_headers_complete_complete", LOG_DEBUG);

  int result = 0;

  if (p_response) {
    bool body_expected =
        has_header("Content-Length") || has_header("Transfer-Encoding");
    bool should_keep_alive = http_should_keep_alive(&_parser);

    // There are two reasons we might want to send a message and close:
    // 1. If we're expecting a request body and we're returning a response
    // prematurely.
    // 2. If the parser's http_should_keep_alive() returns 0. This can happen
    // when a HTTP 1.1 request has "Connection: close". Similarly, a HTTP 1.0
    // request has that behavior as the default.
    //
    // In these cases, add "Connection: close" header to the response and
    // set a flag to ignore all future reads on this connection.
    if (body_expected || !should_keep_alive) {
      p_response->close_after_written();

      uv_read_stop((uv_stream_t *)handle());

      _ignore_new_data = true;
    }
    p_response->write_response();

    // result = 1 has special meaning to http_parser for this one callback; it
    // means F_SKIPBODY should be set on the parser. That's not what we want
    // here; we just want processing to terminate, which we indicate with
    // result = 3.
    result = 3;
  } else {
    // If the request is Expect: Continue, and the app didn't say otherwise,
    // then give it what it wants
    if (has_header("Expect", "100-continue")) {
      p_response = std::shared_ptr<HttpResponse>(
          new HttpResponse(shared_from_this(), 100, "Continue",
                           std::shared_ptr<DataSource>()),
          auto_deleter_background<HttpResponse>);
      p_response->write_response();
    }
  }

  // Tell the parser what the result was and that it can move on.
  http_parser_headers_completed(&(this->_parser), result);

  // Continue parsing any data that went into the request buffer.
  this->_parse_http_data_from_buffer();
}

// ============================================================================
// Message body (for POST)
// ============================================================================

int HttpRequest::_on_body(http_parser *, const char *p_at,
                          size_t length) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_body", LOG_DEBUG);

  // Copy p_at because the source data is deleted right after calling this
  // function.
  std::shared_ptr<std::vector<char>> buf =
      std::make_shared<std::vector<char>>(p_at, p_at + length);

  std::function<void(std::shared_ptr<HttpResponse>)> schedule_bg_callback(
      std::bind(&HttpRequest::_schedule_on_body_error, shared_from_this(),
                std::placeholders::_1));

  // Schedule on main thread:
  // _p_web_application->on_body_data(this, p_at, length, schedule_bg_callback);
  invoke_later(std::bind(&WebApplication::on_body_data, _p_web_application,
                         shared_from_this(), buf, schedule_bg_callback));

  return 0;
}

void HttpRequest::_schedule_on_body_error(
    std::shared_ptr<HttpResponse> p_response) {
  ASSERT_MAIN_THREAD()
  debug_log("HttpRequest::_schedule_on_body_error", LOG_DEBUG);

  response_scheduled();

  std::function<void(void)> cb(
      std::bind(&HttpRequest::_on_body_error, shared_from_this(), p_response));
  _background_queue->push(cb);
}

void HttpRequest::_on_body_error(std::shared_ptr<HttpResponse> p_response) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_body_error", LOG_DEBUG);

  http_parser_pause(&_parser, 1);

  p_response->close_after_written();
  uv_read_stop((uv_stream_t *)handle());
  _ignore_new_data = true;

  p_response->write_response();
}

// ============================================================================
// Message complete
// ============================================================================

int HttpRequest::_on_message_complete(http_parser *) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_message_complete", LOG_DEBUG);

  if (is_upgrade())
    return 0;

  std::function<void(std::shared_ptr<HttpResponse>)> schedule_bg_callback(
      std::bind(&HttpRequest::_schedule_on_message_complete_complete,
                shared_from_this(), std::placeholders::_1));

  // Use later to schedule _p_web_application->get_response(this,
  // schedule_bg_callback) to run on the main thread. That function in turn
  // calls this->_schedule_on_message_complete_complete.
  invoke_later(std::bind(&WebApplication::get_response, _p_web_application,
                         shared_from_this(), schedule_bg_callback));

  return 0;
}

// This is called by the user's application code during or after the end of
// WebApplication::get_response(). It puts an item on the background queue.
void HttpRequest::_schedule_on_message_complete_complete(
    std::shared_ptr<HttpResponse> p_response) {
  ASSERT_MAIN_THREAD()

  response_scheduled();

  std::function<void(void)> cb(
      std::bind(&HttpRequest::_on_message_complete_complete, shared_from_this(),
                p_response));
  _background_queue->push(cb);
}

void HttpRequest::_on_message_complete_complete(
    std::shared_ptr<HttpResponse> p_response) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_message_complete_complete", LOG_DEBUG);

  // This can happen if an error occured in WebApplication::on_body_data.
  if (p_response == NULL) {
    return;
  }

  // TODO: ADding this fixes the ERROR: [uv_write] bad file descriptor, but
  // then we need to make sure the p_response gets cleaned up. Smart pointer?
  if (_is_closing)
    return;

  if (!http_should_keep_alive(&_parser)) {
    p_response->close_after_written();

    uv_read_stop((uv_stream_t *)handle());

    _ignore_new_data = true;
  }

  p_response->write_response();
}

// ============================================================================
// Incoming websocket messages
// ============================================================================

// Called from WebSocketConnection::on_frame_complete
void HttpRequest::on_wsmessage(bool binary, const char *data, size_t len) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::on_wsmessage", LOG_DEBUG);

  // Copy data because the source data is deleted right after calling this
  // function.
  std::shared_ptr<std::vector<char>> buf =
      std::make_shared<std::vector<char>>(data, data + len);

  std::function<void(void)> error_callback(
      std::bind(&HttpRequest::schedule_close, shared_from_this()));

  std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;
  // It's possible for _p_web_socket_connection to have had its refcount drop to
  // zero from another thread or earlier callback in this thread. If that
  // happened, do nothing.
  if (!p_wsc) {
    return;
  }

  // Schedule:
  // _p_web_application->on_wsmessage(p_wsc, binary, data, len);
  invoke_later(std::bind(&WebApplication::on_wsmessage, _p_web_application, p_wsc,
                         binary, buf, error_callback));
}

void HttpRequest::on_wsclose(int) {
  debug_log("HttpRequest::on_wsclose", LOG_DEBUG);
  // TODO: Call close() here?
}

// ============================================================================
// Outgoing websocket messages
// ============================================================================

typedef struct {
  uv_write_t write_req;
  std::vector<char> *p_header;
  std::vector<char> *p_data;
  std::vector<char> *p_footer;
} ws_send_t;

void on_ws_message_sent(uv_write_t *handle, int) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("on_ws_message_sent", LOG_DEBUG);
  // TODO: Handle error if status != 0
  ws_send_t *p_send = (ws_send_t *)handle;
  delete p_send->p_header;
  delete p_send->p_data;
  delete p_send->p_footer;
  free(p_send);
}

void HttpRequest::send_wsframe(const char *p_header, size_t header_size,
                              const char *p_data, size_t data_size,
                              const char *p_footer, size_t footer_size) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::send_wsframe", LOG_DEBUG);
  ws_send_t *p_send = (ws_send_t *)malloc(sizeof(ws_send_t));
  memset(p_send, 0, sizeof(ws_send_t));
  p_send->p_header = new std::vector<char>(p_header, p_header + header_size);
  p_send->p_data = new std::vector<char>(p_data, p_data + data_size);
  p_send->p_footer = new std::vector<char>(p_footer, p_footer + footer_size);

  uv_buf_t buffers[3];
  buffers[0] =
      uv_buf_init(safe_vec_addr(*p_send->p_header), p_send->p_header->size());
  buffers[1] = uv_buf_init(safe_vec_addr(*p_send->p_data), p_send->p_data->size());
  buffers[2] =
      uv_buf_init(safe_vec_addr(*p_send->p_footer), p_send->p_footer->size());

  // TODO: Handle return code
  uv_write(&p_send->write_req, (uv_stream_t *)handle(), buffers, 3,
           &on_ws_message_sent);
}

void HttpRequest::close_wssocket() {
  debug_log("HttpRequest::close_wssocket", LOG_DEBUG);
  close();
}

// ============================================================================
// Closing connection
// ============================================================================

void HttpRequest::_on_closed(uv_handle_t *) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::_on_closed", LOG_DEBUG);

  std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;
  // It's possible for _p_web_socket_connection to have had its refcount drop to
  // zero from another thread or earlier callback in this thread. If that
  // happened, do nothing.
  if (!p_wsc) {
    return;
  }

  // Tell the WebSocketConnection that the connection is closed, before
  // resetting the shared_ptr. This is useful because there may be some
  // callbacks that will execute later, and we want to make sure the WSC
  // doesn't try to do anything with them.
  p_wsc->mark_closed();

  // Note that this location and the destructor are the only places where
  // _p_web_socket_connection is reset; both are on the background thread.
  _p_web_socket_connection.reset();
}

void HttpRequest::close() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpRequest::close", LOG_DEBUG);
  // std::cerr << "Closing handle " << &_handle << std::endl;

  if (_is_closing) {
    debug_log("close() called twice on HttpRequest object", LOG_INFO);
    // We can get here in unusual cases when close() is called once directly,
    // and another time via a scheduled callback. When this happens, don't do
    // the closing machinery twice.
    return;
  }
  _is_closing = true;

  std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;

  if (p_wsc && _protocol == WebSockets) {
    // Schedule:
    // _p_web_application->on_wsclose(p_wsc)
    invoke_later(
        std::bind(&WebApplication::on_wsclose, _p_web_application, p_wsc));
  }

  _p_socket->remove_connection(shared_from_this());

  uv_close(to_handle(&_handle.stream), HttpRequest_on_closed);
}

// This is to be called from the main thread, when the main thread needs to
// tell the background thread to close the request. The main thread should not
// close() directly.
void HttpRequest::schedule_close() {
  debug_log("HttpRequest::schedule_close", LOG_DEBUG);
  // Schedule on background thread:
  //  p_request->close()
  _background_queue->push(std::bind(&HttpRequest::close, shared_from_this()));
}

// ============================================================================
// Open websocket
// ============================================================================

void HttpRequest::_call_r_on_ws_open() {
  ASSERT_MAIN_THREAD()
  debug_log("HttpRequest::_call_r_on_ws_open", LOG_DEBUG);

  std::function<void(void)> error_callback(
      std::bind(&HttpRequest::schedule_close, shared_from_this()));

  this->_p_web_application->on_wsopen(shared_from_this(), error_callback);

  std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;
  // It's possible for _p_web_socket_connection to have had its refcount drop to
  // zero from another thread or earlier callback in this thread. If that
  // happened, do nothing.
  if (!p_wsc) {
    return;
  }

  // _request_buffer is likely empty at this point, but copy its contents and
  // _pass along just in case.

  std::shared_ptr<std::vector<char>> req_buffer =
      std::make_shared<std::vector<char>>(_request_buffer);
  _request_buffer.clear();

  // Schedule on background thread:
  // p_wsc->read(safe_vec_addr(*req_buffer), req_buffer->size())
  std::function<void(void)> cb(std::bind(&WebSocketConnection::read, p_wsc,
                                         safe_vec_addr(*req_buffer),
                                         req_buffer->size()));

  _background_queue->push(cb);
}

// ============================================================================
// Parse incoming data
// ============================================================================

void HttpRequest::_parse_http_data(char *buffer, const ssize_t n) {
  ASSERT_BACKGROUND_THREAD()
  int parsed = http_parser_execute(&_parser, &request_settings(), buffer, n);

  if (http_parser_waiting_for_headers_completed(&_parser)) {
    // If we're waiting for the header response, just store the data in the
    // buffer.
    _request_buffer.insert(_request_buffer.end(), buffer + parsed, buffer + n);

  } else if (is_upgrade()) {
    char *p_data = buffer + parsed;
    size_t p_data_len = n - parsed;

    std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;
    // It's possible for _p_web_socket_connection to have had its refcount drop to
    // zero from another thread or earlier callback in this thread. If that
    // happened, do nothing.
    if (!p_wsc) {
      return;
    }

    if (p_wsc->accept(_headers, p_data, p_data_len)) {
      // Freed in on_response_written
      std::shared_ptr<InMemoryDataSource> p_ds =
          std::make_shared<InMemoryDataSource>();
      std::shared_ptr<HttpResponse> p_resp(
          new HttpResponse(shared_from_this(), 101, "Switching Protocols", p_ds),
          auto_deleter_background<HttpResponse>);

      std::vector<uint8_t> body;
      p_wsc->handshake(_url, _headers, &p_data, &p_data_len, &p_resp->headers(),
                       &body);
      if (body.size() > 0) {
        p_ds->add(body);
      }
      body.clear();

      p_resp->write_response();

      _protocol = WebSockets;

      _request_buffer.insert(_request_buffer.end(), p_data, p_data + p_data_len);

      // Schedule on main thread:
      // this->_call_r_on_ws_open()
      invoke_later(
          std::bind(&HttpRequest::_call_r_on_ws_open, shared_from_this()));
    }

    if (_protocol != WebSockets) {
      // TODO: Write failure
      close();
    }
  } else if (parsed < n) {
    if (!_ignore_new_data) {
      debug_log(std::string("HttpRequest::_parse_http_data error: ") +
                    http_errno_description(HTTP_PARSER_ERRNO(&_parser)),
                LOG_INFO);
      uv_read_stop((uv_stream_t *)handle());
      close();
    }
  }
}

void HttpRequest::_parse_http_data_from_buffer() {
  ASSERT_BACKGROUND_THREAD()
  // Copy contents of _request_buffer, then clear _request_buffer, because it
  // might be written to in _parse_http_data().
  std::vector<char> req_buffer = _request_buffer;
  _request_buffer.clear();

  this->_parse_http_data(safe_vec_addr(req_buffer), req_buffer.size());
}

void HttpRequest::_on_request_read(uv_stream_t *, ssize_t nread,
                                   const uv_buf_t *buf) {
  ASSERT_BACKGROUND_THREAD()
  if (nread > 0) {
    // std::cerr << nread << " bytes read\n";
    if (_ignore_new_data) {
      // Do nothing
    } else if (_protocol == HTTP) {
      this->_parse_http_data(buf->base, nread);

    } else if (_protocol == WebSockets) {
      std::shared_ptr<WebSocketConnection> p_wsc = _p_web_socket_connection;
      // It's possible for _p_web_socket_connection to have had its refcount drop
      // to zero from another thread or earlier callback in this thread. If that
      // happened, do nothing.
      if (p_wsc) {
        p_wsc->read(buf->base, nread);
      }
    }
  } else if (nread < 0) {
    if (nread == UV_EOF || nread == UV_ECONNRESET) {
    } else {
      debug_log(std::string("HttpRequest::on_request_read error: ") +
                    uv_strerror(nread),
                LOG_INFO);
    }
    close();
  } else {
    // It's normal for nread == 0, it's when uv requests a buffer then
    // decides it doesn't need it after all
  }

  free(buf->base);
}

void HttpRequest::handle_request() {
  ASSERT_BACKGROUND_THREAD()
  int r = uv_read_start(handle(), &on_alloc, &HttpRequest_on_request_read);
  if (r) {
    debug_log(std::string("HttpRequest::handl_request error: [uv_read_start] ") +
                  uv_strerror(r),
              LOG_INFO);
    return;
  }
}

#define IMPLEMENT_CALLBACK_1(type, function_name, return_type, type_1)         \
  return_type type##_##function_name(type_1 arg1) {                            \
    return ((type *)(arg1->data))->_##function_name(arg1);                     \
  }
#define IMPLEMENT_CALLBACK_2(type, function_name, return_type, type_1, type_2) \
  return_type type##_##function_name(type_1 arg1, type_2 arg2) {               \
    return ((type *)(arg1->data))->_##function_name(arg1, arg2);               \
  }
#define IMPLEMENT_CALLBACK_3(type, function_name, return_type, type_1, type_2, \
                             type_3)                                           \
  return_type type##_##function_name(type_1 arg1, type_2 arg2, type_3 arg3) {  \
    return ((type *)(arg1->data))->_##function_name(arg1, arg2, arg3);         \
  }

IMPLEMENT_CALLBACK_1(HttpRequest, on_message_begin, int, http_parser *)
IMPLEMENT_CALLBACK_3(HttpRequest, on_url, int, http_parser *, const char *,
                     size_t)
IMPLEMENT_CALLBACK_3(HttpRequest, on_status, int, http_parser *, const char *,
                     size_t)
IMPLEMENT_CALLBACK_3(HttpRequest, on_header_field, int, http_parser *,
                     const char *, size_t)
IMPLEMENT_CALLBACK_3(HttpRequest, on_header_value, int, http_parser *,
                     const char *, size_t)
IMPLEMENT_CALLBACK_1(HttpRequest, on_headers_complete, int, http_parser *)
IMPLEMENT_CALLBACK_3(HttpRequest, on_body, int, http_parser *, const char *,
                     size_t)
IMPLEMENT_CALLBACK_1(HttpRequest, on_message_complete, int, http_parser *)
IMPLEMENT_CALLBACK_1(HttpRequest, on_closed, void, uv_handle_t *)
IMPLEMENT_CALLBACK_3(HttpRequest, on_request_read, void, uv_stream_t *, ssize_t,
                     const uv_buf_t *)

#endif
