#ifndef HTTPSERVER_25_WEBAPPLICATION_H
#define HTTPSERVER_25_WEBAPPLICATION_H

class HttpRequest;
class HttpResponse;

class WebApplication {
public:
  virtual ~WebApplication() {}
  virtual void
  on_headers(std::shared_ptr<HttpRequest> p_request,
            std::function<void(std::shared_ptr<HttpResponse>)> callback) = 0;
  virtual void on_body_data(
      std::shared_ptr<HttpRequest> p_request,
      std::shared_ptr<std::vector<char>> data,
      std::function<void(std::shared_ptr<HttpResponse>)> error_callback) = 0;
  virtual void
  get_response(std::shared_ptr<HttpRequest> request,
              std::function<void(std::shared_ptr<HttpResponse>)> callback) = 0;
  virtual void on_wsopen(std::shared_ptr<HttpRequest> p_request,
                        std::function<void(void)> error_callback) = 0;
  virtual void on_wsmessage(std::shared_ptr<WebSocketConnection>, bool binary,
                           std::shared_ptr<std::vector<char>> data,
                           std::function<void(void)> error_callback) = 0;
  virtual void on_wsclose(std::shared_ptr<WebSocketConnection>) = 0;

  virtual std::shared_ptr<HttpResponse>
  static_file_response(std::shared_ptr<HttpRequest> p_request) = 0;
  virtual StaticPathManager &get_static_path_manager() = 0;
};

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
                  function on_wsopen, function on_wsmessage, function on_wsclose,
                  list static_paths, list static_path_options);

  virtual ~RWebApplication() { ASSERT_MAIN_THREAD() }

  virtual void
  on_headers(std::shared_ptr<HttpRequest> p_request,
            std::function<void(std::shared_ptr<HttpResponse>)> callback);
  virtual void
  on_body_data(std::shared_ptr<HttpRequest> p_request,
             std::shared_ptr<std::vector<char>> data,
             std::function<void(std::shared_ptr<HttpResponse>)> error_callback);
  virtual void
  get_response(std::shared_ptr<HttpRequest> request,
              std::function<void(std::shared_ptr<HttpResponse>)> callback);
  virtual void on_wsopen(std::shared_ptr<HttpRequest> p_request,
                        std::function<void(void)> error_callback);
  virtual void on_wsmessage(std::shared_ptr<WebSocketConnection> conn,
                           bool binary, std::shared_ptr<std::vector<char>> data,
                           std::function<void(void)> error_callback);
  virtual void on_wsclose(std::shared_ptr<WebSocketConnection> conn);

  virtual std::shared_ptr<HttpResponse>
  static_file_response(std::shared_ptr<HttpRequest> p_request);
  virtual StaticPathManager &get_static_path_manager();
};

#endif
