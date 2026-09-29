#ifndef HTTPSERVER_22_WEBSOCKETS_HYBI03_H
#define HTTPSERVER_22_WEBSOCKETS_HYBI03_H

class WebSocketProto_HyBi03 : public WebSocketProto {

public:
  WebSocketProto_HyBi03() {}
  virtual ~WebSocketProto_HyBi03() {}

  bool can_handle(const RequestHeaders &request_headers, const char *p_data,
                  size_t len) const;

  void handshake(const std::string &url, const RequestHeaders &request_headers,
                 char **pp_data, size_t *p_len,
                 ResponseHeaders *p_response_headers,
                 std::vector<uint8_t> *p_response) const;

  void create_frame_header(Opcode opcode, bool mask, size_t payload_size,
                           int32_t masking_key, char p_data[MAX_HEADER_BYTES],
                           size_t *p_len) const;

  bool is_fin(uint8_t first_bit) const;
  uint8_t to_fin(bool is_fin) const;
  Opcode decode_opcode(uint8_t raw_code) const;
  uint8_t encode_opcode(Opcode opcode) const;
};

extern "C" {}

bool calculate_key_value(const std::string &key, uint32_t *p_result = NULL) {
  std::string trimmed = trim(key);
  uint32_t value = 0;
  uint32_t spaces = 0;
  for (std::string::const_iterator it = trimmed.begin(); it != trimmed.end();
       it++) {
    if (*it == ' ')
      spaces++;
    else if (*it >= '0' && *it <= '9') {
      value *= 10;
      value += *it - '0';
    }
  }
  if (spaces == 0)
    return false;
  if (p_result)
    *p_result = value / spaces;
  return true;
}

bool WebSocketProto_HyBi03::can_handle(const RequestHeaders &request_headers,
                                       const char *, size_t len) const {

  if (len != 8)
    return false;
  if (request_headers.find("sec-websocket-key1") == request_headers.end())
    return false;
  if (request_headers.find("sec-websocket-key2") == request_headers.end())
    return false;
  if (!calculate_key_value(request_headers.at("sec-websocket-key1")) ||
      !calculate_key_value(request_headers.at("sec-websocket-key2"))) {
    return false;
  }
  if (request_headers.find("host") == request_headers.end())
    return false;

  return request_headers.find("upgrade") != request_headers.end() &&
         strcasecmp(request_headers.at("upgrade").c_str(), "websocket") == 0;
}

void WebSocketProto_HyBi03::handshake(const std::string &url,
                                      const RequestHeaders &request_headers,
                                      char **pp_data, size_t *p_len,
                                      ResponseHeaders *p_response_headers,
                                      std::vector<uint8_t> *p_response) const {

  assert(*p_len >= 8);

  uint32_t key1, key2;
  calculate_key_value(request_headers.at("sec-websocket-key1"), &key1);
  calculate_key_value(request_headers.at("sec-websocket-key2"), &key2);

  uint8_t handshake[16];
  *reinterpret_cast<uint32_t *>(handshake) = key1;
  *reinterpret_cast<uint32_t *>(handshake + 4) = key2;
  if (!is_big_endian()) {
    swap_byte_order(handshake, handshake + 4);
    swap_byte_order(handshake + 4, handshake + 8);
  }
  memcpy(handshake + 8, *pp_data, 8);
  *pp_data += 8;
  *p_len -= 8;

  MD5_CTX ctx;
  MD5_Init(&ctx);

  MD5_Update(&ctx, handshake, 16);

  p_response->resize(16, 0);
  MD5_Final(safe_vec_addr(*p_response), &ctx);

  std::string origin;
  if (request_headers.find("sec-websocket-origin") != request_headers.end())
    origin = request_headers.at("sec-websocket-origin");
  else if (request_headers.find("origin") != request_headers.end())
    origin = request_headers.at("origin");

  std::string location("ws://");
  location += request_headers.at("host");
  location += url;

  p_response_headers->push_back(std::make_pair("Connection", "Upgrade"));
  p_response_headers->push_back(std::make_pair("Upgrade", "WebSocket"));
  p_response_headers->push_back(std::make_pair("Sec-WebSocket-Origin", origin));
  p_response_headers->push_back(
      std::make_pair("Sec-WebSocket-Location", location));
}

bool WebSocketProto_HyBi03::is_fin(uint8_t first_bit) const {
  return first_bit == 0;
}

uint8_t WebSocketProto_HyBi03::to_fin(bool is_fin) const {
  return is_fin ? 0 : 1;
}

Opcode WebSocketProto_HyBi03::decode_opcode(uint8_t raw_code) const {
  switch (raw_code) {
  case 0:
    return Continuation;
  case 1:
    return Close;
  case 2:
    return Ping;
  case 3:
    return Pong;
  case 4:
    return Text;
  case 5:
    return Binary;
  default:
    return Reserved;
  }
}

uint8_t WebSocketProto_HyBi03::encode_opcode(Opcode opcode) const {
  switch (opcode) {
  case Continuation:
    return 0;
  case Close:
    return 1;
  case Ping:
    return 2;
  case Pong:
    return 3;
  case Text:
    return 4;
  case Binary:
    return 5;
  case Reserved:
  default:
    return 6; // not expected
  }
}

#endif
