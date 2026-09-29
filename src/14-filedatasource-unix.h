#ifndef HTTPSERVER_14_FILEDATASOURCE_UNIX_H
#define HTTPSERVER_14_FILEDATASOURCE_UNIX_H

#ifndef _WIN32


FileDataSourceResult FileDataSource::initialize(const std::string &path,
                                                bool owned) {
  // This can be called from either the main thread or background thread.

  _fd = open(path.c_str(), O_RDONLY);
  if (_fd == -1) {
    if (errno == ENOENT) {
      _lastErrorMessage = "File does not exist: " + path + "\n";
      return FDS_NOT_EXIST;
    } else {
      _lastErrorMessage =
          "Error opening file " + path + ": " + to_string(errno) + "\n";
      return FDS_ERROR;
    }
  } else {
    struct stat info = {};
    if (fstat(_fd, &info)) {
      _lastErrorMessage =
          "Error opening path " + path + ": " + to_string(errno) + "\n";
      ::close(_fd);
      return FDS_ERROR;
    }

    if (S_ISDIR(info.st_mode)) {
      _lastErrorMessage = "File data source is a directory: " + path + "\n";
      ::close(_fd);
      return FDS_ISDIR;
    }

    _length = info.st_size;

    if (owned && unlink(path.c_str())) {
      // Print this (on either main or background thread), since we're not
      // returning 1 to indicate an error.
      err_printf("Couldn't delete temp file %s: %d\n", path.c_str(), errno);
      // It's OK to continue
    }

    return FDS_OK;
  }
}

uint64_t FileDataSource::size() const { return _length; }

uv_buf_t FileDataSource::get_data(size_t bytes_desired) {
  ASSERT_BACKGROUND_THREAD()
  if (bytes_desired == 0)
    return uv_buf_init(NULL, 0);

  char *buffer = (char *)malloc(bytes_desired);
  if (!buffer) {
    throw std::runtime_error("Couldn't allocate buffer");
  }

  ssize_t bytes_read = read(_fd, buffer, bytes_desired);
  if (bytes_read == -1) {
    err_printf("Error reading: %d\n", errno);
    free(buffer);
    throw std::runtime_error("File read failed");
  }

  return uv_buf_init(buffer, bytes_read);
}

void FileDataSource::free_data(uv_buf_t buffer) { free(buffer.base); }

time_t FileDataSource::get_mtime() {
  struct stat res;
  int retval = fstat(_fd, &res);
  if (retval == -1) {
    return 0;
  }
  return res.st_mtime;
}

void FileDataSource::close() {
  if (_fd != -1)
    ::close(_fd);
  _fd = -1;
}

std::string FileDataSource::last_error_message() const {
  return _lastErrorMessage;
}

#endif // #ifndef _WIN32

#endif
