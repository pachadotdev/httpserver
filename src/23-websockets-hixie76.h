#ifndef HTTPSERVER_23_WEBSOCKETS_HIXIE76_H
#define HTTPSERVER_23_WEBSOCKETS_HIXIE76_H

enum Hixie76State {
  // Starting state, also what we return to after finishing a frame
  H76_START,
  // We saw 0x00 indicating text frame, now looking for matching 0xFF.
  // Everything in between is payload data.
  H76_IN_TEXT_FRAME,
  // We just saw 0xFF, the next byte will determine if it's a close
  // message (i.e. if it's 0x00) or if it's a regular binary frame.
  H76_IN_BINARY_OR_CLOSE_FRAME_LENGTH,
  // The current byte is part of the frame length info; if the high-order
  // bit is set to 1, then the next byte is also part of the frame length
  // info.
  H76_IN_BINARY_FRAME_LENGTH,
  // We are in the binary payload, with _bytes_left to go.
  H76_IN_BINARY_FRAME
};

class WSHixie76Parser : public WSParser {
private:
  WSParserCallbacks *_p_callbacks;
  WebSocketProto_HyBi03 _hybi03;
  int _state;
  size_t _bytes_left;

public:
  WSHixie76Parser(WSParserCallbacks *p_callbacks)
      : _p_callbacks(p_callbacks), _state(H76_START) {}
  ~WSHixie76Parser() {}

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

void WSHixie76Parser::handshake(const std::string &url,
                                const RequestHeaders &request_headers,
                                char **pp_data, size_t *p_len,
                                ResponseHeaders *response_headers,
                                std::vector<uint8_t> *p_response) const {
  _hybi03.handshake(url, request_headers, pp_data, p_len, response_headers,
                    p_response);
}

void WSHixie76Parser::create_frame_header_footer(
    Opcode, bool, size_t, int32_t,
    char p_header_data[MAX_HEADER_BYTES], size_t *p_header_len,
    char p_footer_data[MAX_FOOTER_BYTES], size_t *p_footer_len) const {
  p_header_data[0] = 0;
  *p_header_len = 1;

  p_footer_data[0] = static_cast<char>(0xFF);
  *p_footer_len = 1;
}

void WSHixie76Parser::read(const char *data, size_t len) {
  if (len == 0)
    return;
  for (const char *pos = data; pos < data + len; pos++) {
    uint8_t b = *pos;

    if (_state == H76_START) {
      _bytes_left = 0;

      if (b == 0xFF) {
        _state = H76_IN_BINARY_OR_CLOSE_FRAME_LENGTH;
      } else if ((0x80 & b) == 0) {
        _state = H76_IN_TEXT_FRAME;

        WSFrameHeaderInfo info;
        info.fin = true;
        info.opcode = Text;
        info.masked = false;
        info.hasLength = false;
        info.payload_length = 0;
        _p_callbacks->on_header_complete(info);

      } else {
        _state = H76_IN_BINARY_FRAME_LENGTH;
      }

    } else if (_state == H76_IN_TEXT_FRAME) {

      const char *end_marker = pos;
      while (end_marker < (data + len) && *end_marker != (char)0xFF) {
        end_marker++;
      }

      // end_marker is either on an end marker, or past the end of
      // the data that has been given to us.

      // In either case, pass the data (if any) to the callbacks.
      if (pos != end_marker) {
        _p_callbacks->on_payload(pos, end_marker - pos);
      }

      if (end_marker < (data + len)) {
        assert(*end_marker == (char)0xFF);
        // We encountered a marker, all done.
        _state = H76_START;
        _p_callbacks->on_frame_complete();

        // Make sure to skip over what we read
        pos = end_marker;
      } else {
        // We didn't encounter a marker, just consumed all the data.
        return;
      }

    } else if (_state == H76_IN_BINARY_OR_CLOSE_FRAME_LENGTH) {

      if (b == 0) {
        // Close up shop
        WSFrameHeaderInfo info;
        info.fin = true;
        info.opcode = Close;
        info.masked = false;
        info.hasLength = true;
        info.payload_length = 0;
        _p_callbacks->on_header_complete(info);
        _p_callbacks->on_frame_complete();
      } else {
        // Take another look at this byte, now that we know it's not
        // a close directive.
        pos--;
        _state = H76_IN_BINARY_FRAME_LENGTH;
      }

    } else if (_state == H76_IN_BINARY_FRAME_LENGTH) {

      // Add the 7 lower bits to the accumulator
      _bytes_left *= 128;
      _bytes_left += (b & 0x7F);

      // TODO: Detect pathologically large lengths

      // If high-order bit is set, don't continue
      if ((b & 0x80) == 0) {

        _state = H76_IN_BINARY_FRAME;

        WSFrameHeaderInfo info;
        info.fin = true;
        info.opcode = Binary;
        info.masked = false;
        info.hasLength = true;
        info.payload_length = _bytes_left;
        _p_callbacks->on_header_complete(info);

        if (_bytes_left == 0) {
          _p_callbacks->on_frame_complete();
          _state = H76_START;
        }
      }

    } else if (_state == H76_IN_BINARY_FRAME) {

      size_t bytes_to_read = len - (pos - data);
      if (bytes_to_read > _bytes_left)
        bytes_to_read = _bytes_left;

      _bytes_left -= bytes_to_read;
      _p_callbacks->on_payload(pos, bytes_to_read);

      pos += bytes_to_read - 1; // -1 is to compensate for pos++ in for loop

      if (_bytes_left == 0) {
        _p_callbacks->on_frame_complete();
        _state = H76_START;
      }
    }
  }
}

#endif
