#ifndef HTTPSERVER_24_WEBSOCKETS_H
#define HTTPSERVER_24_WEBSOCKETS_H

template <typename T> T min(T a, T b) { return (a > b) ? b : a; }

std::string dumpbin(const char *data, size_t len) {
  std::string output;
  for (size_t i = 0; i < len; i++) {
    char byte = data[i];
    for (size_t mask = 0x80; mask > 0; mask >>= 1) {
      output.push_back(byte & mask ? '1' : '0');
    }
    if (i % 4 == 3)
      output.push_back('\n');
    else
      output.push_back(' ');
  }
  return output;
}

bool WSHyBiFrameHeader::is_header_complete() const {
  if (_data.size() < 2)
    return false;

  return _data.size() >= (size_t)header_length();
}

WSFrameHeaderInfo WSHyBiFrameHeader::info() const {
  WSFrameHeaderInfo inf;
  inf.fin = fin();
  inf.opcode = opcode();
  inf.hasLength = true;
  inf.masked = masked();
  if (masked()) {
    inf.masking_key.resize(4);
    masking_key(safe_vec_addr(inf.masking_key));
  }
  inf.payload_length = payload_length();
  return inf;
}

bool WSHyBiFrameHeader::fin() const { return _p_proto->is_fin(read(0, 1)); }
Opcode WSHyBiFrameHeader::opcode() const {
  uint8_t oc = read(4, 4);
  return _p_proto->decode_opcode(oc);
}
bool WSHyBiFrameHeader::masked() const { return read(8, 1) != 0; }
uint64_t WSHyBiFrameHeader::payload_length() const {
  uint8_t pl = read(9, 7);
  switch (pl) {
  case 126:
    return read64(16, 16);
  case 127:
    return read64(16, 64);
  default:
    return pl;
  }
}
void WSHyBiFrameHeader::masking_key(uint8_t key[4]) const {
  if (!masked())
    memset(key, 0, 4);
  else {
    key[0] = read(9 + payload_length_length(), 8);
    key[1] = read(9 + payload_length_length() + 8, 8);
    key[2] = read(9 + payload_length_length() + 16, 8);
    key[3] = read(9 + payload_length_length() + 24, 8);
  }
}
size_t WSHyBiFrameHeader::header_length() const {
  return (9 + payload_length_length() + masking_key_length()) / 8;
}
uint8_t WSHyBiFrameHeader::read(size_t bit_offset, size_t bit_width) const {
  size_t byte_offset = bit_offset / 8;
  bit_offset = bit_offset % 8;

  assert((bit_offset + bit_width) <= 8);
  assert(byte_offset < _data.size());

  uint8_t mask = 0xFF;
  mask <<= (8 - bit_width);
  mask >>= bit_offset;

  char byte = _data[byte_offset];
  return (byte & mask) >> (8 - bit_width - bit_offset);
}
uint64_t WSHyBiFrameHeader::read64(size_t bit_offset, size_t bit_width) const {
  assert((bit_offset % 8) == 0);
  assert((bit_width % 8) == 0);

  size_t byte_offset = bit_offset / 8;
  size_t byte_width = bit_width / 8;
  assert(byte_offset + byte_width <= _data.size());

  uint64_t result = 0;

  for (size_t i = 0; i < byte_width; i++) {
    result <<= 8;
    result += (uint64_t)(unsigned char)_data[byte_offset + i];
  }

  return result;
}
uint8_t WSHyBiFrameHeader::payload_length_length() const {
  uint8_t pll = read(9, 7);
  switch (pll) {
  case 126:
    return 7 + 16;
  case 127:
    return 7 + 64;
  default:
    return 7;
  }
}
uint8_t WSHyBiFrameHeader::masking_key_length() const {
  return masked() ? 32 : 0;
}

void WSHyBiParser::handshake(const std::string &url,
                             const RequestHeaders &request_headers,
                             char **pp_data, size_t *p_len,
                             ResponseHeaders *p_response_headers,
                             std::vector<uint8_t> *p_response) const {
  ASSERT_BACKGROUND_THREAD()
  _p_proto->handshake(url, request_headers, pp_data, p_len, p_response_headers,
                     p_response);
}

void WSHyBiParser::create_frame_header_footer(
    Opcode opcode, bool mask, size_t payload_size, int32_t masking_key,
    char p_header_data[MAX_HEADER_BYTES], size_t *p_header_len,
    char[MAX_FOOTER_BYTES], size_t *) const {
  _p_proto->create_frame_header(opcode, mask, payload_size, masking_key, p_header_data,
                             p_header_len);
}

void WSHyBiParser::read(const char *data, size_t len) {
  ASSERT_BACKGROUND_THREAD()
  bool recur = false;
  while (len > 0 || recur) {
    // crude check for underflow
    assert(len < 1000000000000000000);

    switch (_state) {
    case InHeader: {
      // The _header vector<char> accumulates header data until
      // the complete header is read. It's possible/likely it also
      // holds part of the payload.
      size_t starting_size = _header.size();
      std::copy(data, data + min(len, MAX_HEADER_BYTES - starting_size),
                std::back_inserter(_header));

      WSHyBiFrameHeader frame(_p_proto, safe_vec_addr(_header), _header.size());

      if (frame.is_header_complete()) {
        _p_callbacks->on_header_complete(frame.info());

        size_t payload_offset = frame.header_length() - starting_size;
        _bytes_left = frame.payload_length();

        // Header was consumed, but no payload
        if (_bytes_left == 0)
          recur = true;

        _state = InPayload;
        _header.clear();

        data += payload_offset;
        len -= payload_offset;
      } else {
        // All of the data was consumed, but no header
        data += len;
        len = 0;
      }
      break;
    }
    case InPayload: {
      recur = false;

      size_t bytes_to_consume = min((uint64_t)len, _bytes_left);
      _bytes_left -= bytes_to_consume;
      _p_callbacks->on_payload(data, bytes_to_consume);

      data += bytes_to_consume;
      len -= bytes_to_consume;

      if (_bytes_left == 0) {
        _p_callbacks->on_frame_complete();

        _state = InHeader;
      }
      break;
    }
    default:
      assert(false);
      break;
    }
  }
}

void WebSocketConnection::start_ping_timer() {
  ASSERT_BACKGROUND_THREAD()

  uv_timer_start(_p_ping_timer, ping_timer_callback, 20000, 20000);
}

bool WebSocketConnection::accept(const RequestHeaders &request_headers,
                                 const char *p_data, size_t len) {
  ASSERT_BACKGROUND_THREAD()
  assert(!_p_parser);
  if (_conn_state == WS_CLOSED)
    return false;

  WebSocketProto_IETF ietf;
  if (ietf.can_handle(request_headers, p_data, len)) {
    _p_parser = new WSHyBiParser(this, new WebSocketProto_IETF());
    this->start_ping_timer();
    return true;
  }

  WebSocketProto_HyBi03 hybi03;
  if (hybi03.can_handle(request_headers, p_data, len)) {
    _p_parser = new WSHixie76Parser(this);
    this->start_ping_timer();
    return true;
  }
  return false;
}

void WebSocketConnection::handshake(const std::string &url,
                                    const RequestHeaders &request_headers,
                                    char **pp_data, size_t *p_len,
                                    ResponseHeaders *p_response_headers,
                                    std::vector<uint8_t> *p_response) {
  ASSERT_BACKGROUND_THREAD()
  assert(_p_parser);
  if (_conn_state == WS_CLOSED)
    return;

  _p_parser->handshake(url, request_headers, pp_data, p_len, p_response_headers,
                      p_response);
}

void WebSocketConnection::send_ws_message(Opcode opcode, const char *p_data,
                                        size_t length) {
  ASSERT_BACKGROUND_THREAD()
  if (_conn_state == WS_CLOSED)
    return;

  std::vector<char> header(MAX_HEADER_BYTES);
  std::vector<char> footer(MAX_FOOTER_BYTES);

  size_t header_length = 0;
  size_t footer_length = 0;

  _p_parser->create_frame_header_footer(opcode, false, length, 0,
                                    safe_vec_addr(header), &header_length,
                                    safe_vec_addr(footer), &footer_length);
  header.resize(header_length);
  footer.resize(footer_length);

  _p_callbacks->send_wsframe(safe_vec_addr(header), header.size(), p_data, length,
                           safe_vec_addr(footer), footer.size());
}

void WebSocketConnection::send_ping() {
  ASSERT_BACKGROUND_THREAD()
  assert(_p_parser);
  debug_log("WebSocketConnection::send_ping", LOG_DEBUG);
  this->send_ws_message(Ping, NULL, 0);
}

void WebSocketConnection::close_ws(uint16_t code, std::string reason) {
  ASSERT_BACKGROUND_THREAD()
  debug_log("WebSocketConnection::close_ws", LOG_DEBUG);

  switch (_conn_state) {
  // If we have already sent a close message, do nothing.
  case WS_CLOSE_SENT:
  case WS_CLOSED:
    return;
  case WS_OPEN:
    _conn_state = WS_CLOSE_SENT;
    break;
  case WS_CLOSE_RECEIVED:
    _conn_state = WS_CLOSED;
    break;
  }

  // Make sure code has right endian-ness
  unsigned char *code_p = (unsigned char *)&code;
  if (!is_big_endian())
    swap_byte_order(code_p, code_p + 2);

  std::string message =
      std::string(reinterpret_cast<char *>(code_p), 2) + reason;

  send_ws_message(Close, message.c_str(), message.length());

  // If close messages have been both sent and received, close socket.
  if (_conn_state == WS_CLOSED)
    _p_callbacks->close_wssocket();
}

void WebSocketConnection::read(const char *data, size_t len) {
  ASSERT_BACKGROUND_THREAD()
  if (_conn_state == WS_CLOSED)
    return;
  assert(_p_parser);
  _p_parser->read(data, len);
}

void WebSocketConnection::mark_closed() {
  ASSERT_BACKGROUND_THREAD()
  _conn_state = WS_CLOSED;
}

void WebSocketConnection::on_header_complete(const WSFrameHeaderInfo &header) {
  ASSERT_BACKGROUND_THREAD()
  if (_conn_state == WS_CLOSED)
    return;

  _header = header;
  if (!header.fin && header.opcode != Continuation)
    _incomplete_content_header = header;
}
void WebSocketConnection::on_payload(const char *data, size_t len) {
  ASSERT_BACKGROUND_THREAD()
  if (_conn_state == WS_CLOSED)
    return;

  size_t orig_size = _payload.size();
  std::copy(data, data + len, std::back_inserter(_payload));

  if (_header.masked != 0) {
    for (size_t i = orig_size; i < _payload.size(); i++) {
      size_t j = i % 4;
      _payload[i] = _payload[i] ^ _header.masking_key[j];
    }
  }
}
void WebSocketConnection::on_frame_complete() {
  ASSERT_BACKGROUND_THREAD()
  debug_log("WebSocketConnection::on_frame_complete", LOG_DEBUG);
  if (_conn_state == WS_CLOSED)
    return;

  if (!_header.fin) {
    std::copy(_payload.begin(), _payload.end(),
              std::back_inserter(_incomplete_content_payload));
  } else {
    switch (_header.opcode) {
    case Continuation: {
      std::copy(_payload.begin(), _payload.end(),
                std::back_inserter(_incomplete_content_payload));
      _p_callbacks->on_wsmessage(_incomplete_content_header.opcode == Binary,
                               safe_vec_addr(_incomplete_content_payload),
                               _incomplete_content_payload.size());

      _incomplete_content_payload.clear();
      break;
    }
    case Text:
    case Binary: {
      _p_callbacks->on_wsmessage(_header.opcode == Binary,
                               safe_vec_addr(_payload), _payload.size());
      break;
    }
    case Close: {

      if (_conn_state == WS_OPEN) {
        _conn_state = WS_CLOSE_RECEIVED;
      } else if (_conn_state == WS_CLOSE_SENT) {
        _conn_state = WS_CLOSED;
      }

      // If we haven't sent a Close frame before, send one now, echoing
      // the callback
      if (_conn_state != WS_CLOSE_SENT && _conn_state != WS_CLOSED) {
        _conn_state = WS_CLOSED;
        send_ws_message(Close, safe_vec_addr(_payload), _payload.size());
      }

      _p_callbacks->close_wssocket();

      _p_callbacks->on_wsclose(0);

      break;
    }
    case Ping: {
      // Send back a pong
      send_ws_message(Pong, safe_vec_addr(_payload), _payload.size());
      break;
    }
    case Pong: {
      // No action needed
      break;
    }
    case Reserved: {
      debug_log("WebSocketConnection::on_frame_complete: reserved opcode",
                LOG_WARN);
      close_ws(1002, "Protocol error");
      break;
    }
    }
  }

  _payload.clear();
}

void ping_timer_callback(uv_timer_t *p_handle) {
  ASSERT_BACKGROUND_THREAD()

  WebSocketConnection *c =
      reinterpret_cast<WebSocketConnection *>(p_handle->data);
  c->send_ping();
}

#endif
