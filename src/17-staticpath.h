#ifndef HTTPSERVER_17_STATICPATH_H
#define HTTPSERVER_17_STATICPATH_H

class StaticPathOptions {
public:
  std::experimental::optional<bool> index_html;
  std::experimental::optional<bool> fallthrough;
  std::experimental::optional<std::string> html_charset;
  std::experimental::optional<ResponseHeaders> headers;
  std::experimental::optional<std::vector<std::string>> validation;
  std::experimental::optional<bool> exclude;
  StaticPathOptions()
      : index_html(std::experimental::nullopt),
        fallthrough(std::experimental::nullopt),
        html_charset(std::experimental::nullopt),
        headers(std::experimental::nullopt),
        validation(std::experimental::nullopt),
        exclude(std::experimental::nullopt) {};
  StaticPathOptions(const list &options);

  void set_options(const list &options);

  list as_robject() const;

  static StaticPathOptions merge(const StaticPathOptions &a,
                                 const StaticPathOptions &b);

  bool validate_request_headers(const RequestHeaders &headers) const;
};

class StaticPath {
public:
  std::string path;
  StaticPathOptions options;

  StaticPath(const list &sp);

  list as_robject() const;
};

class StaticPathManager {
  struct RouteSnapshot {
    std::map<std::string, StaticPath> paths;
    StaticPathOptions defaults;
  };

  std::shared_ptr<RouteSnapshot> snapshot;
  mutable uv_mutex_t snapshot_mutex;
  StaticPathOptions options;

  std::shared_ptr<RouteSnapshot> current_snapshot() const {
    guard lock(snapshot_mutex);
    std::shared_ptr<RouteSnapshot> current = snapshot;
    return current;
  }

  void publish(std::shared_ptr<RouteSnapshot> next) {
    guard lock(snapshot_mutex);
    snapshot = next;
  }

public:
  StaticPathManager();
  StaticPathManager(const list &path_list, const list &options_list);
  ~StaticPathManager() { uv_mutex_destroy(&snapshot_mutex); }

  std::experimental::optional<StaticPath> get(const std::string &path) const;
  std::experimental::optional<StaticPath> get(const strings &path) const;

  void set(const std::string &path, const StaticPath &sp);
  void set(const std::map<std::string, StaticPath> &pmap);
  void set(const list &pmap);

  void remove(const std::string &path);
  void remove(const std::vector<std::string> &paths);
  void remove(const strings &paths);

  std::experimental::optional<std::pair<StaticPath, std::string>>
  match_static_path(const std::string &url_path) const;

  const StaticPathOptions &get_options() const;
  void set_options(const list &opts);

  list paths_as_robject() const;
};

// ============================================================================
// StaticPathOptions
// ============================================================================

StaticPathOptions::StaticPathOptions(const list &options)
    : index_html(std::experimental::nullopt),
      fallthrough(std::experimental::nullopt),
      html_charset(std::experimental::nullopt),
      headers(std::experimental::nullopt),
      validation(std::experimental::nullopt),
      exclude(std::experimental::nullopt) {
  ASSERT_MAIN_THREAD()

  std::string obj_class = std::string(strings(SEXP(options.attr("class")))[0]);
  if (obj_class != "static_path_options") {
    stop("static_path options object must have class 'static_path_options'.");
  }

  SEXP temp;

  temp = options.attr("normalized");
  std::experimental::optional<bool> normalized = optional_as<bool>(temp);
  if (!normalized || !*normalized) {
    stop("static_path_options object must be normalized.");
  }

  // There's probably a more concise way to do this assignment than by using
  // temp.
  temp = options["index_html"];
  index_html = optional_as<bool>(temp);
  temp = options["fallthrough"];
  fallthrough = optional_as<bool>(temp);
  temp = options["html_charset"];
  html_charset = optional_as<std::string>(temp);
  temp = options["headers"];
  headers = optional_as<ResponseHeaders>(temp);
  temp = options["validation"];
  validation = optional_as<std::vector<std::string>>(temp);
  temp = options["exclude"];
  exclude = optional_as<bool>(temp);
}

void StaticPathOptions::set_options(const list &options) {
  ASSERT_MAIN_THREAD()
  SEXP temp;
  if (options.contains("index_html")) {
    temp = options["index_html"];
    if (!Rf_isNull(temp)) {
      index_html = optional_as<bool>(temp);
    }
  }
  if (options.contains("fallthrough")) {
    temp = options["fallthrough"];
    if (!Rf_isNull(temp)) {
      fallthrough = optional_as<bool>(temp);
    }
  }
  if (options.contains("html_charset")) {
    temp = options["html_charset"];
    if (!Rf_isNull(temp)) {
      html_charset = optional_as<std::string>(temp);
    }
  }
  if (options.contains("headers")) {
    temp = options["headers"];
    if (!Rf_isNull(temp)) {
      headers = optional_as<ResponseHeaders>(temp);
    }
  }
  if (options.contains("validation")) {
    temp = options["validation"];
    if (!Rf_isNull(temp)) {
      validation = optional_as<std::vector<std::string>>(temp);
    }
  }
  if (options.contains("exclude")) {
    temp = options["exclude"];
    if (!Rf_isNull(temp)) {
      exclude = optional_as<bool>(temp);
    }
  }
}

list StaticPathOptions::as_robject() const {
  ASSERT_MAIN_THREAD()
  writable::list obj{"index_html"_nm = optional_wrap(index_html),
                     "fallthrough"_nm = optional_wrap(fallthrough),
                     "html_charset"_nm = optional_wrap(html_charset),
                     "headers"_nm = optional_wrap(headers),
                     "validation"_nm = optional_wrap(validation),
                     "exclude"_nm = optional_wrap(exclude)};
  obj.attr("class") = "static_path_options";
  return obj;
}

// Merge StaticPathOptions object `a` with `b`. Values in `a` take precedence.
StaticPathOptions StaticPathOptions::merge(const StaticPathOptions &a,
                                           const StaticPathOptions &b) {
  StaticPathOptions new_sp = a;
  if (new_sp.index_html == std::experimental::nullopt)
    new_sp.index_html = b.index_html;
  if (new_sp.fallthrough == std::experimental::nullopt)
    new_sp.fallthrough = b.fallthrough;
  if (new_sp.html_charset == std::experimental::nullopt)
    new_sp.html_charset = b.html_charset;
  if (new_sp.headers == std::experimental::nullopt)
    new_sp.headers = b.headers;
  if (new_sp.validation == std::experimental::nullopt)
    new_sp.validation = b.validation;
  if (new_sp.exclude == std::experimental::nullopt)
    new_sp.exclude = b.exclude;
  return new_sp;
}

// Check if a set of request headers satisfies the condition specified by
// `validation`.
bool StaticPathOptions::validate_request_headers(
    const RequestHeaders &headers) const {
  if (validation == std::experimental::nullopt) {
    throw std::runtime_error("Cannot validate request headers because "
                             "validation pattern is not set.");
  }

  // Should have the format {"==", "aaa", "bbb"}, or {} if there's no
  // validation pattern.
  const std::vector<std::string> &pattern = *validation;

  if (pattern.size() == 0) {
    return true;
  }

  if (pattern[0] != "==") {
    throw std::runtime_error("Validation only knows the == operator.");
  }

  RequestHeaders::const_iterator it = headers.find(pattern[1]);
  if (it != headers.end() && constant_time_compare(it->second, pattern[2])) {
    return true;
  }

  return false;
}

// ============================================================================
// StaticPath
// ============================================================================

StaticPath::StaticPath(const list &sp) {
  ASSERT_MAIN_THREAD()
  path = as_cpp<std::string>(sp["path"]);

  list options_list(sp["options"]);
  options = StaticPathOptions(options_list);

  if (path.length() == 0) {
    if (!*options.exclude) {
      throw std::runtime_error("Static path must not be empty.");
      // Note that empty paths are OK for excluded paths, but we don't have to
      // mention it in the exception.
    }
  } else if (path.at(path.length() - 1) == '/') {
    throw std::runtime_error("Static path must not have trailing slash.");
  }
}

list StaticPath::as_robject() const {
  ASSERT_MAIN_THREAD()
  writable::list obj{"path"_nm = path, "options"_nm = options.as_robject()};
  obj.attr("class") = "static_path";
  return obj;
}

// ============================================================================
// StaticPathManager
// ============================================================================
StaticPathManager::StaticPathManager()
    : snapshot(std::make_shared<RouteSnapshot>()) {
  uv_mutex_init(&snapshot_mutex);
}

StaticPathManager::StaticPathManager(const list &path_list,
                                     const list &options_list) {
  ASSERT_MAIN_THREAD()
  uv_mutex_init(&snapshot_mutex);
  this->options = StaticPathOptions(options_list);
  std::shared_ptr<RouteSnapshot> initial = std::make_shared<RouteSnapshot>();
  initial->defaults = this->options;

  if (path_list.size() == 0) {
    snapshot = initial;
    return;
  }

  strings names = path_list.names();
  if (names.size() == 0) {
    stop("Error processing static paths: all static paths must be named.");
  }

  for (R_xlen_t i = 0; i < path_list.size(); i++) {
    std::string name = std::string(names[i]);
    if (name == "") {
      stop("Error processing static paths.");
    }

    list sp(path_list[i]);
    StaticPath staticpath(sp);

    initial->paths.insert(std::pair<std::string, StaticPath>(name, staticpath));
  }
  snapshot = initial;
}

// Returns a StaticPath object, which has its options merged with the overall
// ones.
std::experimental::optional<StaticPath>
StaticPathManager::get(const std::string &path) const {
  std::shared_ptr<RouteSnapshot> current = current_snapshot();
  std::map<std::string, StaticPath>::const_iterator it =
      current->paths.find(path);
  if (it == current->paths.end()) {
    return std::experimental::nullopt;
  }

  // Get a copy of the StaticPath object; we'll modify the options in the copy
  // by merging it with the overall options.
  StaticPath sp = it->second;
  sp.options = StaticPathOptions::merge(sp.options, current->defaults);
  return sp;
}

std::experimental::optional<StaticPath>
StaticPathManager::get(const strings &path) const {
  ASSERT_MAIN_THREAD()
  if (path.size() != 1) {
    stop("Can only get a single StaticPath object.");
  }
  return get(std::string(path[0]));
}

void StaticPathManager::set(const std::string &path, const StaticPath &sp) {
  std::shared_ptr<RouteSnapshot> next =
      std::make_shared<RouteSnapshot>(*current_snapshot());
  next->paths.erase(path);
  next->paths.insert(std::make_pair(path, sp));
  publish(next);
}

void StaticPathManager::set(const std::map<std::string, StaticPath> &pmap) {
  std::shared_ptr<RouteSnapshot> next =
      std::make_shared<RouteSnapshot>(*current_snapshot());
  for (std::map<std::string, StaticPath>::const_iterator it = pmap.begin();
       it != pmap.end(); ++it) {
    next->paths.erase(it->first);
    next->paths.insert(std::make_pair(it->first, it->second));
  }
  publish(next);
}

void StaticPathManager::set(const list &pmap) {
  ASSERT_MAIN_THREAD()
  std::map<std::string, StaticPath> pmap2 = to_map<StaticPath, list>(pmap);
  set(pmap2);
}

void StaticPathManager::remove(const std::string &path) {
  std::shared_ptr<RouteSnapshot> next =
      std::make_shared<RouteSnapshot>(*current_snapshot());
  next->paths.erase(path);
  publish(next);
}

void StaticPathManager::remove(const std::vector<std::string> &paths) {
  std::vector<std::string>::const_iterator it;
  for (it = paths.begin(); it != paths.end(); it++) {
    remove(*it);
  }
}

void StaticPathManager::remove(const strings &paths) {
  ASSERT_MAIN_THREAD()
  for (R_xlen_t i = 0; i < paths.size(); i++) {
    remove(std::string(paths[i]));
  }
}

// Given a URL path, this returns a pair where the first element is a matching
// StaticPath object, and the second element is the portion of the url_path that
// comes after the match for the static path.
//
// For example, if:
// - The input url_path is "/foo/bar/page.html"
// - There is a StaticPath object (call it `s`) for which s.path == "/foo"
// Then:
// - This function returns a pair consisting of <s, "bar/page.html">
//
// If there are multiple potential static path matches, for example "/foo" and
// "/foo/bar", then this will match the most specific (longest) path.
//
// If url_path has a trailing "/", it is stripped off. If the url_path matches
// a static path in its entirety (e.g., the url_path is "/foo" or "/foo/" and
// there is a static path "/foo"), then the returned pair consists of the
// matching StaticPath object and an empty string "".
//
// If no matching static path is found, then it returns
// std::experimental::nullopt.
//
std::experimental::optional<std::pair<StaticPath, std::string>>
StaticPathManager::match_static_path(const std::string &url_path) const {

  if (url_path.empty()) {
    return std::experimental::nullopt;
  }

  if (url_path.find('\\') != std::string::npos) {
    return std::experimental::nullopt;
  }

  std::string path = url_path;

  // Read one immutable route snapshot for the complete lookup. Updates publish
  // a replacement snapshot and never block active request lookups.
  std::shared_ptr<RouteSnapshot> current = current_snapshot();

  std::string pre_slash;
  std::string post_slash;

  // Strip off a trailing slash. A path like "/foo/bar/" => "/foo/bar".
  // One exception: don't alter it if the path is just "/".
  if (path.length() > 1 && path.at(path.length() - 1) == '/') {
    path = path.substr(0, path.length() - 1);
  }

  pre_slash = path;
  post_slash = "";

  size_t found_idx = path.length() + 1;

  // This loop searches the snapshot for pre_slash, the part before
  // the last split-on '/'. If found, it returns a pair with the part before
  // the slash, and the part after the slash. If not found, it splits on the
  // previous '/' and searches again, and so on, until there are no more to
  // split on.
  while (true) {
    // Check if the part before the split-on '/' is a static_path.
    std::map<std::string, StaticPath>::const_iterator it =
        current->paths.find(pre_slash);

    if (it != current->paths.end()) {
      StaticPath sp = it->second;
      sp.options = StaticPathOptions::merge(sp.options, current->defaults);
      return std::pair<StaticPath, std::string>(sp, post_slash);
    }

    if (found_idx == 0) {
      // We get here after checking the leading '/'.
      return std::experimental::nullopt;
    }

    // Split the string on '/'
    found_idx = path.find_last_of('/', found_idx - 1);

    if (found_idx == std::string::npos) {
      // This is an extra check that could only be hit if the first character
      // of the URL is not a slash. Shouldn't be possible to get here because
      // the http parser will throw an "invalid URL" error when it encounters
      // such a URL, but we'll check just in case.
      return std::experimental::nullopt;
    }

    pre_slash = path.substr(0, found_idx);
    if (pre_slash == "") {
      // Special case if we've hit the leading slash.
      pre_slash = "/";
    }
    post_slash = path.substr(found_idx + 1);
  }
}

const StaticPathOptions &StaticPathManager::get_options() const {
  return options;
}

void StaticPathManager::set_options(const list &opts) {
  options.set_options(opts);
  std::shared_ptr<RouteSnapshot> next =
      std::make_shared<RouteSnapshot>(*current_snapshot());
  next->defaults = options;
  publish(next);
}

// Returns a list of R objects that reflect the StaticPaths, without merging
// the overall options.
list StaticPathManager::paths_as_robject() const {
  ASSERT_MAIN_THREAD()
  std::shared_ptr<RouteSnapshot> current = current_snapshot();

  R_xlen_t n = static_cast<R_xlen_t>(current->paths.size());
  writable::list obj(n);
  writable::strings nms(n);

  R_xlen_t i = 0;
  std::map<std::string, StaticPath>::const_iterator it;
  for (it = current->paths.begin(); it != current->paths.end(); ++it, ++i) {
    nms[i] = it->first;
    obj[i] = it->second.as_robject();
  }
  obj.attr("names") = nms;

  return obj;
}

#endif
