#ifndef HTTPSERVER_30_HTTPRESPONSE_H
#define HTTPSERVER_30_HTTPRESPONSE_H

void on_response_written(uv_write_t *handle, int status) {
  ASSERT_BACKGROUND_THREAD()
  // Make a local copy of the shared_ptr before deleting the original one.
  std::shared_ptr<HttpResponse> p_response(
      *(std::shared_ptr<HttpResponse> *)handle->data);

  delete (std::shared_ptr<HttpResponse> *)handle->data;
  free(handle);

  p_response->on_response_written(status);
}

ResponseHeaders &HttpResponse::headers() { return _headers; }

void HttpResponse::add_header(const std::string &name,
                             const std::string &value) {
  _headers.push_back(std::pair<std::string, std::string>(name, value));
}

// Set a header to a particular value. If the header already exists, delete
// it, and add the header with the new value. The new header will be the last
// item.
void HttpResponse::set_header(const std::string &name,
                             const std::string &value) {
  // Look for existing header with same name, and delete if present
  ResponseHeaders::iterator it = _headers.begin();
  while (it != _headers.end()) {
    if (strcasecmp(it->first.c_str(), name.c_str()) == 0) {
      it = _headers.erase(it);
    } else {
      ++it;
    }
  }

  add_header(name, value);
}

class HttpResponseExtendedWrite : public ExtendedWrite {
  std::shared_ptr<HttpResponse> _p_parent;

public:
  HttpResponseExtendedWrite(std::shared_ptr<HttpResponse> p_parent,
                            uv_stream_t *p_handle,
                            std::shared_ptr<DataSource> p_data_source,
                            bool chunked)
      : ExtendedWrite(p_handle, p_data_source, chunked), _p_parent(p_parent) {}

  void on_write_complete(int status) { delete this; }
};

void HttpResponse::write_response() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpResponse::write_response", LOG_DEBUG);
  // TODO: Optimize
  std::ostringstream response(std::ios_base::binary);
  response << "HTTP/1.1 " << _status_code << " " << _status << "\r\n";
  bool content_encoding = false;
  std::string content_length;
  for (ResponseHeaders::const_iterator it = _headers.begin();
       it != _headers.end(); it++) {
    if (strcasecmp(it->first.c_str(), "Content-Length") == 0) {
      content_length = it->second;
    } else {
      response << it->first << ": " << it->second << "\r\n";
      if (strcasecmp(it->first.c_str(), "Content-Encoding") == 0) {
        content_encoding = true;
      }
    }
  }

  // Determine if gzip compression should be used
  bool gzip;
  if (content_encoding) {
    // The response already has a Content-Encoding
    gzip = false;
  } else if (_status_code == 101 || _p_body == nullptr) {
    gzip = false;
  } else {
    RequestHeaders h = _p_request->headers();
    auto accept_encoding = h.find("Accept-Encoding");
    if (accept_encoding != h.end()) {
      std::string enc = accept_encoding->second;
      if (enc.find("gzip") != std::string::npos) {
        gzip = true;
      } else {
        // There was an "Accept-Encoding", but it didn't include gzip
        gzip = false;
      }
    } else {
      // No "Accept-Encoding" header
      gzip = false;
    }
  }

  if (gzip) {
    response << "Content-Encoding: gzip\r\n";
    _chunked = true;
    _p_body = std::make_shared<GZipDataSource>(_p_body);
  }

  if (_status_code == 101) {
    // HTTP 101 must not set this header, even if there *is* body data (which is
    // actually not a true HTTP body, but instead, just the first bytes for the
    // switched-to protocol)
  } else if (_chunked) {
    response << "Transfer-Encoding: chunked\r\n";
  } else if (!content_length.empty()) {
    response << "Content-Length: " << content_length << "\r\n";
  } else if (_p_body != nullptr) {
    response << "Content-Length: " << _p_body->size() << "\r\n";
  } else {
    // Some valid responses (such as HTTP 204 and 304) must not set this header,
    // since they can't have a body.
    //
    // See: https://tools.ietf.org/html/rfc7230#section-3.3.2
  }

  response << "\r\n";
  std::string response_str = response.str();
  _response_header.assign(response_str.begin(), response_str.end());

  // For Hixie-76 and HyBi-03, it's important that the body be sent immediately,
  // before any WebSocket traffic is sent from the server
  if (_status_code == 101 && _p_body != NULL && _p_body->size() > 0 &&
      _p_body->size() < 256) {
    uv_buf_t buffer = _p_body->get_data(_p_body->size());
    if (buffer.len > 0) {
      _response_header.reserve(_response_header.size() + buffer.len);
    }
    _response_header.insert(_response_header.end(), buffer.base,
                           buffer.base + buffer.len);
    if (buffer.len == _p_body->size()) {
      // We used up the body, kill it
      _p_body.reset();
    }
  }

  uv_buf_t header_buf =
      uv_buf_init(safe_vec_addr(_response_header), _response_header.size());
  uv_write_t *p_write_req = (uv_write_t *)malloc(sizeof(uv_write_t));
  memset(p_write_req, 0, sizeof(uv_write_t));
  // Pointer to shared_ptr
  p_write_req->data = new std::shared_ptr<HttpResponse>(shared_from_this());

  int r = uv_write(p_write_req, _p_request->handle(), &header_buf, 1,
                   &::on_response_written);
  if (r) {
    debug_log(std::string("uv_write() error:") + uv_strerror(r), LOG_INFO);
    delete (std::shared_ptr<HttpResponse> *)p_write_req->data;
    free(p_write_req);
  } else {
    _p_request->request_completed();
  }
}

void HttpResponse::on_response_written(int status) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpResponse::on_response_written", LOG_DEBUG);
  if (status != 0) {
    err_printf("Error writing response: %d\n", status);
    _close_after_written = true; // Cause the request connection to close.
    return;
  }

  if (_p_body != NULL) {
    HttpResponseExtendedWrite *p_response_write = new HttpResponseExtendedWrite(
        shared_from_this(), _p_request->handle(), _p_body, this->_chunked);
    p_response_write->begin();
  }
}

// This sets a flag so that the connection is closed after the response is
// written. It also adds a "Connection: close" header to the response.
void HttpResponse::close_after_written() {
  set_header("Connection", "close");
  _close_after_written = true;
}

HttpResponse::~HttpResponse() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("HttpResponse::~HttpResponse", LOG_DEBUG);
  if (_close_after_written) {
    _p_request->close();
  }
  _p_body.reset();
}

#endif
