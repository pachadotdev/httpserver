#ifndef HTTPSERVER_16_GZIPDATASOURCE_H
#define HTTPSERVER_16_GZIPDATASOURCE_H

enum GDState { Streaming, Finishing, Done };

class GZipDataSource : public DataSource {
  std::shared_ptr<DataSource> _p_data;
  z_stream _zstrm;
  uv_buf_t _input_buf;
  GDState _state;

public:
  GZipDataSource(std::shared_ptr<DataSource> p_data);

  ~GZipDataSource();

  uint64_t size() const;
  uv_buf_t get_data(size_t bytes_desired);
  void free_data(uv_buf_t buffer);
  void close();

private:
  void deflateNext();
  bool free_input_buffer(bool force = false);
};

GZipDataSource::GZipDataSource(std::shared_ptr<DataSource> p_data)
    : _p_data(p_data), _state(Streaming) {

  _zstrm = {};
  _input_buf = {};
  int res =
      deflateInit2(&_zstrm, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (res != Z_OK) {
    if (_zstrm.msg) {
      throw std::runtime_error(_zstrm.msg);
    } else {
      throw std::runtime_error("zlib initialization failed");
    }
  }
}

GZipDataSource::~GZipDataSource() {
  free_input_buffer(true);
  // ignore errors on destruction
  deflateEnd(&_zstrm);
}

uint64_t GZipDataSource::size() const {
  debug_log("GZipDataSource::size() was called, this should never happen\n",
            LOG_WARN);
  return 0;
}

uv_buf_t GZipDataSource::get_data(size_t bytes_desired) {
  if (_state == Done) {
    // GZip stream written, nothing more to do
    return {};
  }

  // Prepare the output area to be written to
  Bytef *output_buf = (Bytef *)malloc(bytes_desired);
  _zstrm.next_out = output_buf;
  _zstrm.avail_out = bytes_desired;

  // There's room to write, and things we need to write: if Streaming, then
  // there's potentially more data; and if Finishing, then we need to write
  // the gzip footer.
  while (_zstrm.avail_out > 0 && _state != Done) {
    if (_state == Streaming && _zstrm.avail_in == 0) {
      free_input_buffer();

      _input_buf = _p_data->get_data(bytes_desired);
      _zstrm.next_in = (Bytef *)_input_buf.base;
      _zstrm.avail_in = _input_buf.len;

      if (_input_buf.len == 0) {
        _state = Finishing;
      }
    }

    deflateNext();
  }

  free_input_buffer();

  uv_buf_t ret = {};
  ret.base = (char *)output_buf;
  ret.len = bytes_desired - _zstrm.avail_out;
  return ret;
}

void GZipDataSource::free_data(uv_buf_t buffer) { free(buffer.base); }

void GZipDataSource::close() { _p_data->close(); }

// Attempt to deflate more data, reading from _zstrm.next_in and writing to
// _zstrm.next_out. Both reads and (potentially) writes _state.
void GZipDataSource::deflateNext() {
  int res = deflate(&_zstrm, (_state == Finishing) ? Z_FINISH : Z_NO_FLUSH);
  if (res == Z_STREAM_END) {
    _state = Done;
  } else if (res != Z_OK) {
    throw std::runtime_error("deflate failed!");
  }
}

// Use force=true to free the buffer even if _zstrm might still be using it
bool GZipDataSource::free_input_buffer(bool force) {
  if ((force || _zstrm.avail_in == 0) && _input_buf.base) {
    _p_data->free_data(_input_buf);
    _input_buf = {};
    _zstrm.next_in = Z_NULL;
    _zstrm.avail_in = 0;
    return true;
  } else {
    return false;
  }
}

#endif
