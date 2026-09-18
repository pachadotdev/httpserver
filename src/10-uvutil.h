#ifndef HTTPSERVER_10_UVUTIL_H
#define HTTPSERVER_10_UVUTIL_H

inline uv_handle_t *toHandle(uv_timer_t *timer) { return (uv_handle_t *)timer; }
inline uv_handle_t *toHandle(uv_tcp_t *tcp) { return (uv_handle_t *)tcp; }
inline uv_handle_t *toHandle(uv_stream_t *stream) {
  return (uv_handle_t *)stream;
}

inline uv_stream_t *toStream(uv_tcp_t *tcp) { return (uv_stream_t *)tcp; }

void freeAfterClose(uv_handle_t *handle);

class WriteOp;

// Abstract class for synchronously streaming known-length data
// without needing to know where the data comes from.
class DataSource {
public:
  virtual ~DataSource() {}
  virtual uint64_t size() const = 0;
  virtual uv_buf_t getData(size_t bytesDesired) = 0;
  virtual void freeData(uv_buf_t buffer) = 0;
  virtual void close() = 0;
};

class InMemoryDataSource : public DataSource {
private:
  std::vector<uint8_t> _buffer;
  size_t _pos;

public:
  explicit InMemoryDataSource(
      const std::vector<uint8_t> &buffer = std::vector<uint8_t>())
      : _buffer(buffer), _pos(0) {}

  explicit InMemoryDataSource(const raws &rawVector)
      : _buffer(rawVector.size()), _pos(0) {
    ASSERT_MAIN_THREAD()
    std::copy(rawVector.begin(), rawVector.end(), _buffer.begin());
  }

  virtual ~InMemoryDataSource() { close(); }

  uint64_t size() const;
  uv_buf_t getData(size_t bytesDesired);
  void freeData(uv_buf_t buffer);
  void close();

  void add(const std::vector<uint8_t> &moreData);
};

// Class for writing a DataSource to a uv_stream_t. Takes care
// not to buffer too much data in memory (happens when you try
// to write too much data to a slow uv_stream_t).
class ExtendedWrite {
  bool _chunked;
  int _activeWrites;
  bool _errored;
  bool _completed;
  uv_stream_t *_pHandle;
  std::shared_ptr<DataSource> _pDataSource;

public:
  ExtendedWrite(uv_stream_t *pHandle, std::shared_ptr<DataSource> pDataSource,
                bool chunked)
      : _chunked(chunked), _activeWrites(0), _errored(false), _completed(false),
        _pHandle(pHandle), _pDataSource(pDataSource) {}
  virtual ~ExtendedWrite() {}

  virtual void onWriteComplete(int status) = 0;

  void begin();
  friend class WriteOp;

protected:
  void next();
};

inline int ip_family(const std::string &ip) {
  // A buffer big enough for an IPv6 address
  unsigned char addr[16];

  if (uv_inet_pton(AF_INET6, ip.c_str(), addr) == 0) {
    return AF_INET6;
  }
  if (uv_inet_pton(AF_INET, ip.c_str(), addr) == 0) {
    return AF_INET;
  }
  return -1;
}

void freeAfterClose(uv_handle_t *handle) { free(handle); }

class WriteOp {
private:
  ExtendedWrite *pParent;

  // Bytes to write before writing the buffer
  std::vector<char> prefix;

  // The main payload
  uv_buf_t buffer;

  // Bytes to write after writing the buffer
  std::vector<char> suffix;

public:
  uv_write_t handle;

  WriteOp(ExtendedWrite *parent, std::string prefix, uv_buf_t data,
          std::string suffix)
      : pParent(parent), prefix(prefix.begin(), prefix.end()), buffer(data),
        suffix(suffix.begin(), suffix.end()) {
    memset(&handle, 0, sizeof(uv_write_t));
    handle.data = this;
  }

  std::vector<uv_buf_t> bufs() {
    ASSERT_BACKGROUND_THREAD()
    std::vector<uv_buf_t> res;
    if (prefix.size() > 0) {
      res.push_back(uv_buf_init(&prefix[0], prefix.size()));
    }
    if (buffer.len != 0) {
      res.push_back(buffer);
    }
    if (suffix.size() > 0) {
      res.push_back(uv_buf_init(&suffix[0], suffix.size()));
    }
    return res;
  }

  void end() {
    ASSERT_BACKGROUND_THREAD()
    pParent->_pDataSource->freeData(buffer);
    pParent->_activeWrites--;

    if (handle.handle->write_queue_size == 0) {
      // Write queue is empty, so we're ready to check for
      // more data and send if it available.
      pParent->next();
    }

    delete this;
  }
};

uint64_t InMemoryDataSource::size() const { return _buffer.size(); }
uv_buf_t InMemoryDataSource::getData(size_t bytesDesired) {
  ASSERT_BACKGROUND_THREAD()
  size_t bytes = _buffer.size() - _pos;
  if (bytesDesired < bytes)
    bytes = bytesDesired;

  uv_buf_t mem;
  mem.base = bytes > 0 ? reinterpret_cast<char *>(&_buffer[_pos]) : 0;
  mem.len = bytes;

  _pos += bytes;
  return mem;
}
void InMemoryDataSource::freeData(uv_buf_t buffer) {}
void InMemoryDataSource::close() {
  ASSERT_BACKGROUND_THREAD()
  _buffer.clear();
}

void InMemoryDataSource::add(const std::vector<uint8_t> &moreData) {
  ASSERT_BACKGROUND_THREAD()
  if (_buffer.capacity() < _buffer.size() + moreData.size())
    _buffer.reserve(_buffer.size() + moreData.size());
  _buffer.insert(_buffer.end(), moreData.begin(), moreData.end());
}

static void writecb(uv_write_t *handle, int status) {
  ASSERT_BACKGROUND_THREAD()
  WriteOp *pWriteOp = (WriteOp *)handle->data;
  pWriteOp->end();
}

void ExtendedWrite::begin() {
  ASSERT_BACKGROUND_THREAD()
  next();
}

const std::string CRLF = "\r\n";
const std::string TRAILER = "0\r\n\r\n";

void ExtendedWrite::next() {
  ASSERT_BACKGROUND_THREAD()
  if (_errored || _completed) {
    if (_activeWrites == 0) {
      _pDataSource->close();
      onWriteComplete(_errored ? 1 : 0);
    }
    return;
  }

  uv_buf_t buf;
  try {
    buf = _pDataSource->getData(65536);
  } catch (std::exception &e) {
    _errored = true;
    if (_activeWrites == 0) {
      _pDataSource->close();
      onWriteComplete(1);
    }
    return;
  }
  if (buf.len == 0) {
    // No more data is going to come.
    // Ensure future calls to next() results in disposal (assuming that all
    // outstanding writes are done).
    _completed = true;
  }

  std::string prefix;
  std::string suffix;
  if (this->_chunked) {
    if (buf.len == 0) {
      // In chunked mode, the last chunk must be followed by one more "\r\n".
      suffix = TRAILER;
    } else {
      // In chunked mode, data chunks must be preceded by 1) the number of bytes
      // in the chunk, as a hexadecimal string; and 2) "\r\n"; and succeeded by
      // another "\r\n"
      std::stringstream ss;
      ss << std::uppercase << std::hex << buf.len << "\r\n";
      prefix = ss.str();
      suffix = "\r\n";
    }
  } else {
    // Non-chunked mode
    if (buf.len == 0) {
      // We've reached the end of the response body. We'll exit before calling
      // uv_write, below.
    } else {
      // This is the simple/common case; we're about to write some data to the
      // socket, then we'll come back and see if there's more to write.
    }
  }

  if (prefix.size() == 0 && buf.len == 0 && suffix.size() == 0) {
    // It's not safe to proceed with uv_write() in this situation. uv_write
    // will not tolerate being called with 0 buffers, and clang-ASAN will
    // complain if any buf.base is NULL (even if buf.len is 0).
    _pDataSource->freeData(buf);
    next();
    return;
  }

  WriteOp *pWriteOp = new WriteOp(this, prefix, buf, suffix);
  _activeWrites++;
  auto op_bufs = pWriteOp->bufs();
  uv_write(&pWriteOp->handle, _pHandle, &op_bufs[0], op_bufs.size(), &writecb);
}

#endif
