#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mob_survivor::net {

inline constexpr std::size_t kMaxRequestHead = 16 * 1024;

struct HttpRequest {
  std::string method;
  std::string target;
  std::string version;
  // Header names are lower-cased; values have surrounding blanks trimmed.
  std::vector<std::pair<std::string, std::string>> headers;

  [[nodiscard]] std::optional<std::string_view> header(std::string_view lower_name) const;
  // The target without its query string.
  [[nodiscard]] std::string_view path() const;
};

enum class ParseStatus { Incomplete, Done, Error };

// Parses one request head (request line and headers, up to the blank line)
// from the front of `buffer`. On Done, `consumed` is the head's length.
[[nodiscard]] ParseStatus parse_request(std::string_view buffer, HttpRequest& request, std::size_t& consumed);

[[nodiscard]] std::string http_response(int status, std::string_view content_type, std::string_view body,
                                        std::string_view extra_headers = {}, bool head_only = false);

[[nodiscard]] std::string_view status_reason(int status);
[[nodiscard]] std::string_view mime_type(std::string_view path);

// Maps a request path to a file below the web root: "/" is "index.html";
// anything with "..", a backslash, a NUL, an empty segment or a hidden
// segment is refused (nullopt).
[[nodiscard]] std::optional<std::string> static_file_path(std::string_view path);

// True when `token` appears in a comma-separated header value, ignoring case.
[[nodiscard]] bool header_has_token(std::string_view value, std::string_view token);

}  // namespace mob_survivor::net
