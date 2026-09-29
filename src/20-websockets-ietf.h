#ifndef HTTPSERVER_20_WEBSOCKETS_IETF_H
#define HTTPSERVER_20_WEBSOCKETS_IETF_H

class WebSocketProto_IETF : public WebSocketProto {

public:
  WebSocketProto_IETF() {}
  virtual ~WebSocketProto_IETF() {}

  bool can_handle(const RequestHeaders &request_headers, const char *p_data,
                 size_t len) const;

  void handshake(const std::string &url, const RequestHeaders &request_headers,
                 char **pp_data, size_t *p_len, ResponseHeaders *response_headers,
                 std::vector<uint8_t> *p_response) const;

  bool is_fin(uint8_t first_bit) const;
  uint8_t to_fin(bool is_fin) const;
  Opcode decode_opcode(uint8_t raw_code) const;
  uint8_t encode_opcode(Opcode opcode) const;
};

bool WebSocketProto_IETF::can_handle(const RequestHeaders &request_headers,
                                    const char *, size_t) const {

  return request_headers.find("upgrade") != request_headers.end() &&
         strcasecmp(request_headers.at("upgrade").c_str(), "websocket") == 0 &&
         request_headers.find("sec-websocket-key") != request_headers.end();
}

void WebSocketProto_IETF::handshake(const std::string &,
                                    const RequestHeaders &request_headers,
                                    char **, size_t *,
                                    ResponseHeaders *p_response_headers,
                                    std::vector<uint8_t> *) const {

  std::string key = request_headers.at("sec-websocket-key");

  std::string clear = trim(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  SHA1_CTX ctx;
  reid_SHA1_Init(&ctx);
  reid_SHA1_Update(&ctx, (const uint8_t *)safe_str_addr(clear), clear.size());

  std::vector<uint8_t> digest(SHA1_DIGEST_SIZE);
  reid_SHA1_Final(&ctx, safe_vec_addr(digest));

  std::string response = b64encode(digest.begin(), digest.end());

  p_response_headers->push_back(
      std::pair<std::string, std::string>("Connection", "Upgrade"));
  p_response_headers->push_back(
      std::pair<std::string, std::string>("Upgrade", "websocket"));
  p_response_headers->push_back(
      std::pair<std::string, std::string>("Sec-WebSocket-Accept", response));
}

bool WebSocketProto_IETF::is_fin(uint8_t first_bit) const {
  return first_bit != 0;
}

uint8_t WebSocketProto_IETF::to_fin(bool is_fin) const { return is_fin ? 1 : 0; }

Opcode WebSocketProto_IETF::decode_opcode(uint8_t raw_code) const {
  switch (raw_code) {
  case 0:
    return Continuation;
  case 1:
    return Text;
  case 2:
    return Binary;
  case 8:
    return Close;
  case 9:
    return Ping;
  case 0xA:
    return Pong;
  case 0xF:
    return Reserved;
  default:
    return Reserved;
  }
}

uint8_t WebSocketProto_IETF::encode_opcode(Opcode opcode) const {
  switch (opcode) {
  case Continuation:
    return 0;
  case Text:
    return 1;
  case Binary:
    return 2;
  case Close:
    return 8;
  case Ping:
    return 9;
  case Pong:
    return 0xA;
  case Reserved:
    return 0xF;
  default:
    return 0xF; // not expected
  }
}

#endif
