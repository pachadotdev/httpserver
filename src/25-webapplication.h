#ifndef HTTPSERVER_25_WEBAPPLICATION_H
#define HTTPSERVER_25_WEBAPPLICATION_H

class HttpRequest;
class HttpResponse;

// RequestDispatcher is the transport-independent boundary between the HTTP
// state machine and an application. The parser emits stages; the dispatcher
// decides whether to serve, defer to R, or transition a connection.
class RequestDispatcher {
public:
  virtual ~RequestDispatcher() {}
  virtual void
  dispatch_headers(
      std::shared_ptr<HttpRequest> p_request,
      std::function<void(std::shared_ptr<HttpResponse>)> callback) = 0;
  virtual void dispatch_body(
      std::shared_ptr<HttpRequest> p_request,
      std::shared_ptr<std::vector<char>> data,
      std::function<void(std::shared_ptr<HttpResponse>)> error_callback) = 0;
  virtual void
  dispatch_complete(
      std::shared_ptr<HttpRequest> request,
      std::function<void(std::shared_ptr<HttpResponse>)> callback) = 0;
  virtual void dispatch_wsopen(
      std::shared_ptr<HttpRequest> p_request,
      std::function<void(void)> error_callback) = 0;
  virtual void dispatch_wsmessage(
      std::shared_ptr<WebSocketConnection>, bool binary,
      std::shared_ptr<std::vector<char>> data,
      std::function<void(void)> error_callback) = 0;
  virtual void dispatch_wsclose(std::shared_ptr<WebSocketConnection>) = 0;

  virtual std::shared_ptr<HttpResponse>
  static_file_response(std::shared_ptr<HttpRequest> p_request) = 0;
  virtual StaticPathManager &get_static_path_manager() = 0;
};

class WebApplication : public RequestDispatcher {};

class RWebApplication : public WebApplication {
private:
  sexp _on_headers;
  function _on_body_data;
  function _on_request;
  function _on_wsopen;
  function _on_wsmessage;
  function _on_wsclose;

  StaticPathManager _static_path_manager;

public:
  RWebApplication(sexp on_headers, function on_body_data, function on_request,
                  function on_wsopen, function on_wsmessage,
                  function on_wsclose, list static_paths,
                  list static_path_options);

  virtual ~RWebApplication() { ASSERT_MAIN_THREAD() }

  virtual void
  dispatch_headers(std::shared_ptr<HttpRequest> p_request,
                   std::function<void(std::shared_ptr<HttpResponse>)> callback);
  virtual void dispatch_body(
      std::shared_ptr<HttpRequest> p_request,
      std::shared_ptr<std::vector<char>> data,
      std::function<void(std::shared_ptr<HttpResponse>)> error_callback);
  virtual void
  dispatch_complete(
      std::shared_ptr<HttpRequest> request,
      std::function<void(std::shared_ptr<HttpResponse>)> callback);
  virtual void dispatch_wsopen(
      std::shared_ptr<HttpRequest> p_request,
      std::function<void(void)> error_callback);
  virtual void dispatch_wsmessage(
      std::shared_ptr<WebSocketConnection> conn, bool binary,
      std::shared_ptr<std::vector<char>> data,
      std::function<void(void)> error_callback);
  virtual void dispatch_wsclose(std::shared_ptr<WebSocketConnection> conn);

  virtual std::shared_ptr<HttpResponse>
  static_file_response(std::shared_ptr<HttpRequest> p_request);
  virtual StaticPathManager &get_static_path_manager();
};

#endif
