#ifndef HTTPSERVER_12_FS_H
#define HTTPSERVER_12_FS_H

std::string basename(const std::string &path);

std::string find_extension(const std::string &filename);

bool is_directory(const std::string &filename);

bool path_exists(const std::string &filename);

// Return true only when `candidate` resolves inside `root`. Both paths must
// exist; resolving them before opening the file prevents a configured static
// root from being escaped through a symlink.
bool is_path_within(const std::string &root, const std::string &candidate);

#ifdef _WIN32
#else
#endif

// Given a filename, return the extension.
std::string find_extension(const std::string &filename) {
  size_t found_idx = filename.find_last_of('.');

  if (found_idx <= 0) {
    return "";
  } else {
    return filename.substr(found_idx + 1);
  }
}

// Given a filename, return the extension.
std::string basename(const std::string &path) {
  size_t found_idx = path.find_last_of("/\\");

  if (found_idx == std::string::npos) {
    return path;
  } else {
    return path.substr(found_idx + 1);
  }
}

// filename is assumed to be UTF-8.
bool is_directory(const std::string &filename) {
#ifdef _WIN32

  DWORD file_attr = GetFileAttributesW(utf8_to_wide(filename).data());
  if (file_attr == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  if (file_attr & FILE_ATTRIBUTE_DIRECTORY) {
    return true;
  }

  return false;

#else

  struct stat sb;

  if (stat(filename.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  } else {
    return false;
  }
}

#endif

bool path_exists(const std::string &filename) {
#ifdef _WIN32
  return GetFileAttributesW(utf8_to_wide(filename).data()) !=
         INVALID_FILE_ATTRIBUTES;
#else
  struct stat sb;
  return stat(filename.c_str(), &sb) == 0;
#endif
}

bool is_path_within(const std::string &root, const std::string &candidate) {
#ifdef _WIN32
  char root_buf[MAX_PATH];
  char candidate_buf[MAX_PATH];
  DWORD root_len = GetFullPathNameA(root.c_str(), MAX_PATH, root_buf, NULL);
  DWORD candidate_len =
      GetFullPathNameA(candidate.c_str(), MAX_PATH, candidate_buf, NULL);
  if (root_len == 0 || candidate_len == 0 || root_len >= MAX_PATH ||
      candidate_len >= MAX_PATH) {
    return false;
  }

  std::string root_path(root_buf, root_len);
  std::string candidate_path(candidate_buf, candidate_len);
  while (root_path.size() > 1 &&
         (root_path.back() == '/' || root_path.back() == '\\')) {
    root_path.pop_back();
  }
  if (candidate_path.size() < root_path.size() ||
      _strnicmp(candidate_path.c_str(), root_path.c_str(), root_path.size()) !=
          0) {
    return false;
  }
  return candidate_path.size() == root_path.size() ||
         candidate_path[root_path.size()] == '/' ||
         candidate_path[root_path.size()] == '\\';
#else
  char root_buf[PATH_MAX];
  char candidate_buf[PATH_MAX];
  if (realpath(root.c_str(), root_buf) == NULL ||
      realpath(candidate.c_str(), candidate_buf) == NULL) {
    return false;
  }

  std::string root_path(root_buf);
  std::string candidate_path(candidate_buf);
  if (candidate_path.size() < root_path.size() ||
      candidate_path.compare(0, root_path.size(), root_path) != 0) {
    return false;
  }
  return candidate_path.size() == root_path.size() ||
         candidate_path[root_path.size()] == '/';
#endif
}

#endif
