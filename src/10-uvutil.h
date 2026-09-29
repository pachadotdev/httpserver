#ifndef HTTPSERVER_10_UVUTIL_H
#define HTTPSERVER_10_UVUTIL_H

inline uv_handle_t *to_handle(uv_timer_t *timer) {
  return (uv_handle_t *)timer;
}
inline uv_handle_t *to_handle(uv_tcp_t *tcp) { return (uv_handle_t *)tcp; }
inline uv_handle_t *to_handle(uv_stream_t *stream) {
  return (uv_handle_t *)stream;
}

inline uv_stream_t *to_stream(uv_tcp_t *tcp) { return (uv_stream_t *)tcp; }

void free_after_close(uv_handle_t *handle);

class WriteOp;

// Abstract class for synchronously streaming known-length data
// without needing to know where the data comes from.
class DataSource {
public:
  virtual ~DataSource() {}
  virtual uint64_t size() const = 0;
  virtual uv_buf_t get_data(size_t bytes_desired) = 0;
  virtual void free_data(uv_buf_t buffer) = 0;
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

  explicit InMemoryDataSource(const raws &raw_vector)
      : _buffer(raw_vector.size()), _pos(0) {
    ASSERT_MAIN_THREAD()
    std::copy(raw_vector.begin(), raw_vector.end(), _buffer.begin());
  }

  virtual ~InMemoryDataSource() { close(); }

  uint64_t size() const;
  uv_buf_t get_data(size_t bytes_desired);
  void free_data(uv_buf_t buffer);
  void close();

  void add(const std::vector<uint8_t> &more_data);
};

// Class for writing a DataSource to a uv_stream_t. Takes care
// not to buffer too much data in memory (happens when you try
// to write too much data to a slow uv_stream_t).
class ExtendedWrite {
  bool _chunked;
  int _active_writes;
  bool _errored;
  bool _completed;
  uv_stream_t *_p_handle;
  std::shared_ptr<DataSource> _p_data_source;

public:
  ExtendedWrite(uv_stream_t *p_handle,
                std::shared_ptr<DataSource> p_data_source, bool chunked)
      : _chunked(chunked), _active_writes(0), _errored(false),
        _completed(false), _p_handle(p_handle), _p_data_source(p_data_source) {}
  virtual ~ExtendedWrite() {}

  virtual void on_write_complete(int status) = 0;

  void begin();
  friend class WriteOp;

protected:
  void next();
};

inline int ip_family_impl(const std::string &ip) {
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

void free_after_close(uv_handle_t *handle) { free(handle); }

class WriteOp {
private:
  ExtendedWrite *p_parent;

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
      : p_parent(parent), prefix(prefix.begin(), prefix.end()), buffer(data),
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
    p_parent->_p_data_source->free_data(buffer);
    p_parent->_active_writes--;

    if (handle.handle->write_queue_size == 0) {
      // Write queue is empty, so we're ready to check for
      // more data and send if it available.
      p_parent->next();
    }

    delete this;
  }
};

uint64_t InMemoryDataSource::size() const { return _buffer.size(); }
uv_buf_t InMemoryDataSource::get_data(size_t bytes_desired) {
  ASSERT_BACKGROUND_THREAD()
  size_t bytes = _buffer.size() - _pos;
  if (bytes_desired < bytes)
    bytes = bytes_desired;

  uv_buf_t mem;
  mem.base = bytes > 0 ? reinterpret_cast<char *>(&_buffer[_pos]) : 0;
  mem.len = bytes;

  _pos += bytes;
  return mem;
}
void InMemoryDataSource::free_data(uv_buf_t buffer) {}
void InMemoryDataSource::close() {
  ASSERT_BACKGROUND_THREAD()
  _buffer.clear();
}

void InMemoryDataSource::add(const std::vector<uint8_t> &more_data) {
  ASSERT_BACKGROUND_THREAD()
  if (_buffer.capacity() < _buffer.size() + more_data.size())
    _buffer.reserve(_buffer.size() + more_data.size());
  _buffer.insert(_buffer.end(), more_data.begin(), more_data.end());
}

static void writecb(uv_write_t *handle, int status) {
  ASSERT_BACKGROUND_THREAD()
  WriteOp *p_write_op = (WriteOp *)handle->data;
  p_write_op->end();
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
    if (_active_writes == 0) {
      _p_data_source->close();
      on_write_complete(_errored ? 1 : 0);
    }
    return;
  }

  uv_buf_t buf;
  try {
    buf = _p_data_source->get_data(65536);
  } catch (std::exception &e) {
    _errored = true;
    if (_active_writes == 0) {
      _p_data_source->close();
      on_write_complete(1);
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
    _p_data_source->free_data(buf);
    next();
    return;
  }

  WriteOp *p_write_op = new WriteOp(this, prefix, buf, suffix);
  _active_writes++;
  auto op_bufs = p_write_op->bufs();
  uv_write(&p_write_op->handle, _p_handle, &op_bufs[0], op_bufs.size(),
           &writecb);
}

#endif
