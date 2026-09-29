#ifndef HTTPSERVER_22_WEBSOCKETS_H
#define HTTPSERVER_22_WEBSOCKETS_H

class WSFrameHeaderInfo {
public:
  bool fin;
  Opcode opcode;
  bool masked;
  std::vector<uint8_t> masking_key;
  bool hasLength;
  uint64_t payload_length;
};

/* Interprets the bytes that make up a WebSocket frame header.
 * See RFC 6455 Section 5 (especially 5.2) for details on the
 * wire format.
 */
class WSHyBiFrameHeader {
  std::vector<char> _data;
  WebSocketProto *_p_proto;

public:
  WSHyBiFrameHeader() : _data(MAX_HEADER_BYTES), _p_proto(NULL) {}

  // The data is copied (up to 14 bytes worth)
  WSHyBiFrameHeader(WebSocketProto *p_proto, const char *data, size_t len)
      : _data(data, data + (std::min(MAX_HEADER_BYTES, len))), _p_proto(p_proto) {
  }

  virtual ~WSHyBiFrameHeader() {}

  // IMPORTANT: Don't attempt to call any of the other methods
  // until is_header_complete is true!!
  bool is_header_complete() const;
  bool is_payload_complete() const;

  WSFrameHeaderInfo info() const;
  uint64_t payload_length() const;
  size_t header_length() const;

private:
  bool fin() const;
  Opcode opcode() const;
  bool masked() const;
  void masking_key(uint8_t key[4]) const;

  // Read part of a byte, and interpret the bits as an unsigned number.
  // The bit_offset is starting from the most significant bit.
  // IMPORTANT: (bit_offset % 8) + bit_width MUST be 8 or less!
  // In other words, the bits you request must not span multiple bytes.
  uint8_t read(size_t bit_offset, size_t bit_width) const;
  // Read bytes, an interpret them as a big endian number.
  // IMPORTANT: bit_offset and bit_width MUST be multiples of 8!
  // IMPORTANT: bit_width MUST be 64 or less!
  uint64_t read64(size_t bit_offset, size_t bit_width) const;
  uint8_t payload_length_length() const;
  uint8_t masking_key_length() const;
};

class WSParserCallbacks {
public:
  virtual void on_header_complete(const WSFrameHeaderInfo &header) = 0;
  // The data is copied
  virtual void on_payload(const char *data, size_t len) = 0;
  virtual void on_frame_complete() = 0;
};

class WSParser {
public:
  virtual ~WSParser() {}

  // Populate response headers with the appropriate values. This call
  // must not fail, but it will not be called unless can_handle returned
  // true previously, so any validation should be done in can_handle.
  virtual void handshake(const std::string &url,
                         const RequestHeaders &request_headers, char **pp_data,
                         size_t *p_len, ResponseHeaders *response_headers,
                         std::vector<uint8_t> *p_response) const = 0;

  virtual void create_frame_header_footer(Opcode opcode, bool mask,
                                       size_t payload_size, int32_t masking_key,
                                       char p_header_data[MAX_HEADER_BYTES],
                                       size_t *p_header_len,
                                       char p_footer_data[MAX_FOOTER_BYTES],
                                       size_t *p_footer_len) const = 0;

  virtual void read(const char *data, size_t len) = 0;
};

class WSHyBiParser : public WSParser {
  WSParserCallbacks *_p_callbacks;
  WebSocketProto *_p_proto;
  WSParseState _state;
  std::vector<char> _header;
  uint64_t _bytes_left;

public:
  WSHyBiParser(WSParserCallbacks *callbacks, WebSocketProto *p_proto)
      : _p_callbacks(callbacks), _p_proto(p_proto), _state(InHeader) {}
  virtual ~WSHyBiParser() {
    try {
      delete _p_proto;
    } catch (...) {
    }
  }

  void handshake(const std::string &url, const RequestHeaders &request_headers,
                 char **pp_data, size_t *p_len, ResponseHeaders *response_headers,
                 std::vector<uint8_t> *p_response) const;

  void create_frame_header_footer(Opcode opcode, bool mask, size_t payload_size,
                               int32_t masking_key,
                               char p_header_data[MAX_HEADER_BYTES],
                               size_t *p_header_len,
                               char p_footer_data[MAX_FOOTER_BYTES],
                               size_t *p_footer_len) const;

  void read(const char *data, size_t len);
};

enum WSConnState {
  WS_OPEN,
  WS_CLOSE_RECEIVED,
  WS_CLOSE_SENT,
  // This can represent two cases:
  //   1. When a close message is received, and a close message is sent.
  //   2. When the connection was simply closed without any messages.
  // It may be useful in the future to split this up.
  WS_CLOSED
};

class WebSocketConnectionCallbacks {
public:
  virtual void on_wsmessage(bool binary, const char *data, size_t len) = 0;
  virtual void on_wsclose(int code) = 0;
  // Implementers MUST copy data
  virtual void send_wsframe(const char *header_data, size_t header_length,
                           const char *p_data, size_t data_length,
                           const char *footer_data, size_t footer_length) = 0;
  virtual void close_wssocket() = 0;
};

void ping_timer_callback(uv_timer_t *handle);

class WebSocketConnection : WSParserCallbacks, NoCopy {
  uv_loop_t *_p_loop;
  WSConnState _conn_state;
  std::shared_ptr<WebSocketConnectionCallbacks> _p_callbacks;
  WSParser *_p_parser;
  WSFrameHeaderInfo _incomplete_content_header;
  WSFrameHeaderInfo _header;
  std::vector<char> _incomplete_content_payload;
  std::vector<char> _payload;
  uv_timer_t *_p_ping_timer;

public:
  WebSocketConnection(uv_loop_t *p_loop,
                      std::shared_ptr<WebSocketConnectionCallbacks> callbacks)
      : _p_loop(p_loop), _conn_state(WS_OPEN), _p_callbacks(callbacks),
        _p_parser(NULL) {
    ASSERT_BACKGROUND_THREAD()
    debug_log("WebSocketConnection::WebSocketConnection", LOG_DEBUG);

    _p_ping_timer = static_cast<uv_timer_t *>(malloc(sizeof(uv_timer_t)));
    uv_timer_init(_p_loop, _p_ping_timer);
    _p_ping_timer->data = this;
  }

  virtual ~WebSocketConnection() {
    ASSERT_BACKGROUND_THREAD()
    debug_log("WebSocketConnection::~WebSocketConnection", LOG_DEBUG);
    // calling uv_close() on a timer implicitly calls uv_timer_stop()
    uv_close(to_handle(_p_ping_timer), free_after_close);
    try {
      delete _p_parser;
    } catch (...) {
    }
  }

  bool accept(const RequestHeaders &request_headers, const char *p_data,
              size_t len);
  void handshake(const std::string &url, const RequestHeaders &request_headers,
                 char **pp_data, size_t *p_len, ResponseHeaders *p_response_headers,
                 std::vector<uint8_t> *p_response);

  void send_ws_message(Opcode opcode, const char *p_data, size_t length);
  void send_ping();
  void close_ws(uint16_t code = 1000, std::string reason = "");
  void read(const char *data, size_t len);
  void mark_closed();
  void start_ping_timer();

protected:
  void on_header_complete(const WSFrameHeaderInfo &header);
  void on_payload(const char *data, size_t len);
  void on_frame_complete();
};

#endif
