#ifndef HTTPSERVER_32_HTTP_H
#define HTTPSERVER_32_HTTP_H

void on_request(uv_stream_t *handle, int status) {
  ASSERT_BACKGROUND_THREAD()
  if (status) {
    err_printf("connection error: %s\n", uv_strerror(status));
    return;
  }

  // Copy the shared_ptr
  std::shared_ptr<Socket> p_socket(*(std::shared_ptr<Socket> *)handle->data);
  CallbackQueue *bg_queue = p_socket->background_queue;

  // Freed by HttpRequest itself when close() is called, which
  // can occur on EOF, error, or when the Socket is destroyed
  std::shared_ptr<HttpRequest> req = create_http_request(
      handle->loop, p_socket->p_web_application, p_socket, bg_queue);

  int r = uv_accept(handle, req->handle());
  if (r) {
    err_printf("accept: %s\n", uv_strerror(r));
    req->close();
    return;
  }

  req->handle_request();
}

uv_stream_t *
create_pipe_server(uv_loop_t *p_loop, const std::string &name, int mask,
                   std::shared_ptr<WebApplication> p_web_application,
                   bool quiet, CallbackQueue *background_queue) {
  ASSERT_BACKGROUND_THREAD()

  // We own p_web_application. It will be destroyed by the socket but if in
  // the future we have failure cases that stop execution before we get
  // that far, we MUST delete p_web_application ourselves.

  std::shared_ptr<Socket> p_socket =
      std::make_shared<Socket>(p_web_application, background_queue);

  int r = uv_pipe_init(p_loop, &p_socket->handle.pipe, 0);
  if (r) {
    if (!quiet)
      err_printf("create_pipe_server: %s\n", uv_strerror(r));
    return NULL;
  }
  p_socket->handle.is_tcp = false;
  // data is a pointer to the shared_ptr. This is necessary because the
  // uv_stream_t.data field is a void*.
  p_socket->handle.stream.data = new std::shared_ptr<Socket>(p_socket);

  mode_t old_mask = 0;
  if (mask >= 0)
    old_mask = umask(mask);
  r = uv_pipe_bind(&p_socket->handle.pipe, name.c_str());
  if (mask >= 0)
    umask(old_mask);

  if (r) {
    if (!quiet)
      err_printf("create_pipe_server: %s\n", uv_strerror(r));
    // It's important that close() is explicitly called, so that the uv_pipe_t
    // is cleaned up
    p_socket->close();
    return NULL;
  }
  r = uv_listen((uv_stream_t *)&p_socket->handle.stream, 128, &on_request);
  if (r) {
    if (!quiet)
      err_printf("create_pipe_server: %s\n", uv_strerror(r));
    // It's important that close() is explicitly called, so that the uv_pipe_t
    // is cleaned up
    p_socket->close();
    return NULL;
  }

  return &p_socket->handle.stream;
}

// A wrapper for create_pipe_server. The main thread schedules this to run on
// the background thread, then waits for this to finish, using a barrier.
void create_pipe_server_sync(uv_loop_t *loop, const std::string &name, int mask,
                             std::shared_ptr<WebApplication> p_web_application,
                             bool quiet, CallbackQueue *background_queue,
                             uv_stream_t **p_server,
                             std::shared_ptr<Barrier> blocker) {
  ASSERT_BACKGROUND_THREAD()

  *p_server = create_pipe_server(loop, name, mask, p_web_application, quiet,
                                 background_queue);

  // Tell the main thread that the server is ready
  blocker->wait();
}

uv_stream_t *
create_tcp_server(uv_loop_t *p_loop, const std::string &host, int port,
                  std::shared_ptr<WebApplication> p_web_application, bool quiet,
                  CallbackQueue *background_queue) {
  ASSERT_BACKGROUND_THREAD()

  // We own p_web_application. It will be destroyed by the socket but if in
  // the future we have failure cases that stop execution before we get
  // that far, we MUST delete p_web_application ourselves.

  std::shared_ptr<Socket> p_socket =
      std::make_shared<Socket>(p_web_application, background_queue);

  int r = uv_tcp_init(p_loop, &p_socket->handle.tcp);
  if (r) {
    if (!quiet)
      err_printf("create_tcp_server: %s\n", uv_strerror(r));
    return NULL;
  }
  p_socket->handle.is_tcp = true;
  // data is a pointer to the shared_ptr. This is necessary because the
  // uv_stream_t.data field is a void*.
  p_socket->handle.stream.data = new std::shared_ptr<Socket>(p_socket);

  // Lifetime of these needs to encompass use of p_address in uv_tcp_bind()
  struct sockaddr_in6 addr6;
  struct sockaddr_in addr4;
  sockaddr *p_address;
  int family = ip_family_impl(host);
  if (family == AF_INET6) {
    r = uv_ip6_addr(host.c_str(), port, &addr6);
    p_address = reinterpret_cast<sockaddr *>(&addr6);
  } else if (family == AF_INET) {
    r = uv_ip4_addr(host.c_str(), port, &addr4);
    p_address = reinterpret_cast<sockaddr *>(&addr4);
  } else {
    r = 1;
    if (!quiet)
      err_printf("%s is not a valid IPv4 or IPv6 address.\n", host.c_str());
  }

  if (r) {
    if (!quiet)
      err_printf("create_tcp_server: %s\n", uv_strerror(r));
    // It's important that close() is explicitly called, so that the uv_tcp_t is
    // cleaned up
    p_socket->close();
    return NULL;
  }

  r = uv_tcp_bind(&p_socket->handle.tcp, p_address, 0);

  if (r) {
    if (!quiet)
      err_printf("create_tcp_server: %s\n", uv_strerror(r));
    // It's important that close() is explicitly called, so that the uv_tcp_t is
    // cleaned up
    p_socket->close();
    return NULL;
  }
  r = uv_listen((uv_stream_t *)&p_socket->handle.stream, 128, &on_request);
  if (r) {
    if (!quiet)
      err_printf("create_tcp_server: %s\n", uv_strerror(r));
    // It's important that close() is explicitly called, so that the uv_tcp_t is
    // cleaned up
    p_socket->close();
    return NULL;
  }

  return &p_socket->handle.stream;
}

// A wrapper for create_tcp_server. The main thread schedules this to run on the
// background thread, then waits for this to finish, using a barrier.
void create_tcp_server_sync(uv_loop_t *p_loop, const std::string &host,
                            int port,
                            std::shared_ptr<WebApplication> p_web_application,
                            bool quiet, CallbackQueue *background_queue,
                            uv_stream_t **p_server,
                            std::shared_ptr<Barrier> blocker) {
  ASSERT_BACKGROUND_THREAD()

  *p_server = create_tcp_server(p_loop, host, port, p_web_application, quiet,
                                background_queue);

  // Tell the main thread that the server is ready
  blocker->wait();
}

void free_server(uv_stream_t *p_handle) {
  ASSERT_BACKGROUND_THREAD()
  if (p_handle == NULL || uv_is_closing((uv_handle_t *)p_handle))
    return;
  std::shared_ptr<Socket> *pp_socket =
      (std::shared_ptr<Socket> *)p_handle->data;
  (*pp_socket)->close();
  // pp_socket gets deleted in a callback in close()
}

#endif
