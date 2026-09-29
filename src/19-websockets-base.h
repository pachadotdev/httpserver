#ifndef HTTPSERVER_19_WEBSOCKETS_BASE_H
#define HTTPSERVER_19_WEBSOCKETS_BASE_H

class WebSocketProto {

public:
  WebSocketProto() {}
  virtual ~WebSocketProto() {}

  // Return true if the request uses this protocol version and is valid
  virtual bool can_handle(const RequestHeaders &request_headers,
                          const char *p_data, size_t len) const = 0;

  // Populate response headers with the appropriate values. This call
  // must not fail, but it will not be called unless can_handle returned
  // true previously, so any validation should be done in can_handle.
  virtual void handshake(const std::string &url,
                         const RequestHeaders &request_headers, char **pp_data,
                         size_t *p_len, ResponseHeaders *response_headers,
                         std::vector<uint8_t> *p_response) const = 0;

  void create_frame_header(Opcode opcode, bool mask, size_t payload_size,
                           int32_t masking_key, char p_data[MAX_HEADER_BYTES],
                           size_t *p_len) const;

  virtual bool is_fin(uint8_t first_bit) const = 0;
  virtual uint8_t to_fin(bool is_fin) const = 0;
  virtual Opcode decode_opcode(uint8_t raw_code) const = 0;
  virtual uint8_t encode_opcode(Opcode opcode) const = 0;
};

bool is_big_endian();
// Swaps the byte range [p_start, p_end)
void swap_byte_order(unsigned char *p_start, unsigned char *p_end);

bool is_big_endian() {
  uint32_t i = 1;
  return *((uint8_t *)&i) == 0;
}

// Swaps the byte range [p_start, p_end)
void swap_byte_order(unsigned char *p_start, unsigned char *p_end) {
  // Easier for callers to use exclusive end but easier to implement
  // using inclusive end
  p_end--;

  while (p_start < p_end) {

    unsigned char tmp;
    tmp = *p_start;
    *p_start = *p_end;
    *p_end = tmp;

    p_start++;
    p_end--;
  }
}

void WebSocketProto::create_frame_header(Opcode opcode, bool mask,
                                         size_t payload_size,
                                         int32_t masking_key,
                                         char p_data[MAX_HEADER_BYTES],
                                         size_t *p_len) const {

  unsigned char *p_buf = (unsigned char *)p_data;
  unsigned char *p_masking_key = p_buf + 2;
  // Need to copy from a 64-bit chunk of memory, but size_t may be smaller.
  uint64_t payload_size_64 = payload_size;

  p_buf[0] = to_fin(true) << 7 | // FIN; always true
             encode_opcode(opcode);
  p_buf[1] = mask ? 1 << 7 : 0;
  if (payload_size_64 <= 125) {
    p_buf[1] |= payload_size_64;
    p_masking_key = p_buf + 2;
  } else if (payload_size_64 <= 65535) { // 2^16-1
    p_buf[1] |= 126;
    memcpy(p_buf + 2, &payload_size_64, sizeof(uint16_t));
    if (!is_big_endian())
      swap_byte_order(p_buf + 2, p_buf + 4);
    p_masking_key = p_buf + 4;
  } else {
    p_buf[1] |= 127;
    memcpy(p_buf + 2, &payload_size_64, sizeof(uint64_t));
    if (!is_big_endian())
      swap_byte_order(p_buf + 2, p_buf + 10);
    p_masking_key = p_buf + 10;
  }

  if (mask) {
    memcpy(p_masking_key, &masking_key, sizeof(int32_t));
  }

  *p_len = (p_masking_key - p_buf) + (mask ? 4 : 0);
}

#endif
