#ifndef HTTPSERVER_31_SOCKET_H
#define HTTPSERVER_31_SOCKET_H

void on_socket_close(uv_handle_t *p_handle);

void Socket::add_connection(std::shared_ptr<HttpRequest> request) {
  connections.push_back(request);
}

void Socket::remove_connection(std::shared_ptr<HttpRequest> request) {
  connections.erase(
      std::remove(connections.begin(), connections.end(), request),
      connections.end());
}

Socket::~Socket() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("Socket::~Socket", LOG_DEBUG);
}

// A deleter callback for the shared_ptr<Socket>.
void delete_ppsocket(uv_handle_t *p_handle) {
  std::shared_ptr<Socket> *pp_socket = (std::shared_ptr<Socket> *)p_handle->data;
  delete pp_socket;
}

// This tells all the HttpRequests to close and deletes the
// shared_ptr<Socket>. Each HttpRequest also has a shared_ptr to this Socket.
// Once they're all closed, the Socket will be deleted. In some cases, there
// may be an extant HttpRequest object which doesn't get deleted until an R GC
// event occurs, and so this Socket will continue to exist until then.
void Socket::close() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("Socket::close", LOG_DEBUG);
  for (std::vector<std::shared_ptr<HttpRequest>>::reverse_iterator it =
           connections.rbegin();
       it != connections.rend(); it++) {

    // std::cerr << "Request close on " << *it << std::endl;
    (*it)->close();
  }

  uv_handle_t *p_handle = to_handle(&handle.stream);

  // Delete the shared_ptr<Socket> only after uv_close() does its work. This
  // will decrease the refcount and should trigger deletion of the Socket.
  uv_close(p_handle, delete_ppsocket);
}

#endif
