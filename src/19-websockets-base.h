#ifndef HTTPSERVER_19_WEBSOCKETS_BASE_H
#define HTTPSERVER_19_WEBSOCKETS_BASE_H

class WebSocketProto {

public:
  WebSocketProto() {}
  virtual ~WebSocketProto() {}

  // Return true if the request uses this protocol version and is valid
  virtual bool canHandle(const RequestHeaders &requestHeaders,
                         const char *pData, size_t len) const = 0;

  // Populate response headers with the appropriate values. This call
  // must not fail, but it will not be called unless canHandle returned
  // true previously, so any validation should be done in canHandle.
  virtual void handshake(const std::string &url,
                         const RequestHeaders &requestHeaders, char **ppData,
                         size_t *pLen, ResponseHeaders *responseHeaders,
                         std::vector<uint8_t> *pResponse) const = 0;

  void createFrameHeader(Opcode opcode, bool mask, size_t payloadSize,
                         int32_t maskingKey, char pData[MAX_HEADER_BYTES],
                         size_t *pLen) const;

  virtual bool isFin(uint8_t firstBit) const = 0;
  virtual uint8_t toFin(bool isFin) const = 0;
  virtual Opcode decodeOpcode(uint8_t rawCode) const = 0;
  virtual uint8_t encodeOpcode(Opcode opcode) const = 0;
};

bool isBigEndian();
// Swaps the byte range [pStart, pEnd)
void swapByteOrder(unsigned char *pStart, unsigned char *pEnd);

bool isBigEndian() {
  uint32_t i = 1;
  return *((uint8_t *)&i) == 0;
}

// Swaps the byte range [pStart, pEnd)
void swapByteOrder(unsigned char *pStart, unsigned char *pEnd) {
  // Easier for callers to use exclusive end but easier to implement
  // using inclusive end
  pEnd--;

  while (pStart < pEnd) {

    unsigned char tmp;
    tmp = *pStart;
    *pStart = *pEnd;
    *pEnd = tmp;

    pStart++;
    pEnd--;
  }
}

void WebSocketProto::createFrameHeader(Opcode opcode, bool mask,
                                       size_t payloadSize, int32_t maskingKey,
                                       char pData[MAX_HEADER_BYTES],
                                       size_t *pLen) const {

  unsigned char *pBuf = (unsigned char *)pData;
  unsigned char *pMaskingKey = pBuf + 2;
  // Need to copy from a 64-bit chunk of memory, but size_t may be smaller.
  uint64_t payloadSize_64 = payloadSize;

  pBuf[0] = toFin(true) << 7 | // FIN; always true
            encodeOpcode(opcode);
  pBuf[1] = mask ? 1 << 7 : 0;
  if (payloadSize_64 <= 125) {
    pBuf[1] |= payloadSize_64;
    pMaskingKey = pBuf + 2;
  } else if (payloadSize_64 <= 65535) { // 2^16-1
    pBuf[1] |= 126;
    memcpy(pBuf + 2, &payloadSize_64, sizeof(uint16_t));
    if (!isBigEndian())
      swapByteOrder(pBuf + 2, pBuf + 4);
    pMaskingKey = pBuf + 4;
  } else {
    pBuf[1] |= 127;
    memcpy(pBuf + 2, &payloadSize_64, sizeof(uint64_t));
    if (!isBigEndian())
      swapByteOrder(pBuf + 2, pBuf + 10);
    pMaskingKey = pBuf + 10;
  }

  if (mask) {
    memcpy(pMaskingKey, &maskingKey, sizeof(int32_t));
  }

  *pLen = (pMaskingKey - pBuf) + (mask ? 4 : 0);
}

#endif
