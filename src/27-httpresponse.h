#ifndef HTTPSERVER_27_HTTPRESPONSE_H
#define HTTPSERVER_27_HTTPRESPONSE_H

class HttpRequest;

class HttpResponse : public std::enable_shared_from_this<HttpResponse> {

  std::shared_ptr<HttpRequest> _p_request;
  int _status_code;
  std::string _status;
  ResponseHeaders _headers;
  std::vector<char> _response_header;
  std::shared_ptr<DataSource> _p_body;
  bool _close_after_written;
  bool _chunked;

public:
  HttpResponse(std::shared_ptr<HttpRequest> p_request, int status_code,
               const std::string &status, std::shared_ptr<DataSource> p_body)
      : _p_request(p_request), _status_code(status_code), _status(status),
        _p_body(p_body), _close_after_written(false), _chunked(false) {
    _headers.push_back(std::make_pair("Date", http_date_string(time(NULL))));
  }

  ~HttpResponse();
  ResponseHeaders &headers();

  void add_header(const std::string &name, const std::string &value);
  void set_header(const std::string &name, const std::string &value);
  void write_response();
  void on_response_written(int status);
  void close_after_written();
};

#endif
