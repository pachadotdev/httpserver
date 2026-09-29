#ifndef HTTPSERVER_28_SOCKET_H
#define HTTPSERVER_28_SOCKET_H

class HttpRequest;
class WebApplication;

class Socket {
public:
  VariantHandle handle;
  std::shared_ptr<WebApplication> p_web_application;
  CallbackQueue *background_queue;
  std::vector<std::shared_ptr<HttpRequest>> connections;

  Socket(std::shared_ptr<WebApplication> p_web_application,
         CallbackQueue *background_queue)
      : p_web_application(p_web_application),
        background_queue(background_queue) {}

  void add_connection(std::shared_ptr<HttpRequest> request);
  void remove_connection(std::shared_ptr<HttpRequest> request);
  void close();

  virtual ~Socket();
};

#endif
