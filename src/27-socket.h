#ifndef HTTPSERVER_28_SOCKET_H
#define HTTPSERVER_28_SOCKET_H

class HttpRequest;
class WebApplication;

class Socket {
public:
  VariantHandle handle;
  std::shared_ptr<WebApplication> pWebApplication;
  CallbackQueue *background_queue;
  std::vector<std::shared_ptr<HttpRequest>> connections;

  Socket(std::shared_ptr<WebApplication> pWebApplication,
         CallbackQueue *background_queue)
      : pWebApplication(pWebApplication), background_queue(background_queue) {}

  void addConnection(std::shared_ptr<HttpRequest> request);
  void removeConnection(std::shared_ptr<HttpRequest> request);
  void close();

  virtual ~Socket();
};

#endif
