#include "net/http.hpp"

#include <algorithm>
#include <cctype>

namespace mob_survivor::net {

namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

}  // namespace

std::optional<std::string_view> HttpRequest::header(std::string_view lower_name) const {
  for (const auto& [name, value] : headers) {
    if (name == lower_name) return value;
  }
  return std::nullopt;
}

std::string_view HttpRequest::path() const {
  const std::string_view whole(target);
  const auto query = whole.find('?');
  return query == std::string_view::npos ? whole : whole.substr(0, query);
}

ParseStatus parse_request(std::string_view buffer, HttpRequest& request, std::size_t& consumed) {
  const auto end = buffer.find("\r\n\r\n");
  if (end == std::string_view::npos) {
    return buffer.size() > kMaxRequestHead ? ParseStatus::Error : ParseStatus::Incomplete;
  }
  if (end + 4 > kMaxRequestHead) return ParseStatus::Error;
  const std::string_view head = buffer.substr(0, end);
  const auto line_end = head.find("\r\n");
  const std::string_view line = head.substr(0, line_end);

  const auto first_space = line.find(' ');
  const auto second_space = first_space == std::string_view::npos ? first_space : line.find(' ', first_space + 1);
  if (first_space == std::string_view::npos || second_space == std::string_view::npos) return ParseStatus::Error;
  request = HttpRequest{};
  request.method = std::string(line.substr(0, first_space));
  request.target = std::string(line.substr(first_space + 1, second_space - first_space - 1));
  request.version = std::string(line.substr(second_space + 1));
  if (request.method.empty() || request.target.empty() || request.target.front() != '/' ||
      !request.version.starts_with("HTTP/1.")) {
    return ParseStatus::Error;
  }

  std::string_view rest = line_end == std::string_view::npos ? std::string_view{} : head.substr(line_end + 2);
  while (!rest.empty()) {
    const auto next = rest.find("\r\n");
    const std::string_view field = rest.substr(0, next);
    rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next + 2);
    const auto colon = field.find(':');
    if (colon == std::string_view::npos || colon == 0) return ParseStatus::Error;
    request.headers.emplace_back(lower(field.substr(0, colon)), std::string(trim(field.substr(colon + 1))));
    if (request.headers.size() > 100) return ParseStatus::Error;
  }
  consumed = end + 4;
  return ParseStatus::Done;
}

std::string_view status_reason(int status) {
  switch (status) {
    case 101:
      return "Switching Protocols";
    case 200:
      return "OK";
    case 304:
      return "Not Modified";
    case 400:
      return "Bad Request";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 413:
      return "Content Too Large";
    case 426:
      return "Upgrade Required";
    case 503:
      return "Service Unavailable";
    default:
      return "Error";
  }
}

std::string http_response(int status, std::string_view content_type, std::string_view body,
                          std::string_view extra_headers, bool head_only) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " " + std::string(status_reason(status)) + "\r\n";
  out += "Content-Type: " + std::string(content_type) + "\r\n";
  out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  out += "X-Content-Type-Options: nosniff\r\n";
  out += "Connection: close\r\n";
  out += extra_headers;
  out += "\r\n";
  if (!head_only) out += body;
  return out;
}

std::string_view mime_type(std::string_view path) {
  if (ends_with(path, ".html")) return "text/html; charset=utf-8";
  if (ends_with(path, ".js") || ends_with(path, ".mjs")) return "text/javascript; charset=utf-8";
  if (ends_with(path, ".css")) return "text/css; charset=utf-8";
  if (ends_with(path, ".json")) return "application/json";
  if (ends_with(path, ".webmanifest")) return "application/manifest+json";
  if (ends_with(path, ".svg")) return "image/svg+xml";
  if (ends_with(path, ".png")) return "image/png";
  if (ends_with(path, ".ico")) return "image/x-icon";
  if (ends_with(path, ".txt")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

std::optional<std::string> static_file_path(std::string_view path) {
  if (path.empty() || path.front() != '/') return std::nullopt;
  if (path == "/") return std::string("index.html");
  std::string_view rest = path.substr(1);
  std::string out;
  while (true) {
    const auto slash = rest.find('/');
    const std::string_view segment = rest.substr(0, slash);
    if (segment.empty() || segment.front() == '.' || segment.find('\\') != std::string_view::npos ||
        segment.find('\0') != std::string_view::npos || segment.find('%') != std::string_view::npos) {
      return std::nullopt;
    }
    if (!out.empty()) out += '/';
    out += segment;
    if (slash == std::string_view::npos) break;
    rest = rest.substr(slash + 1);
  }
  return out;
}

bool header_has_token(std::string_view value, std::string_view token) {
  const std::string wanted = lower(token);
  while (!value.empty()) {
    const auto comma = value.find(',');
    if (lower(trim(value.substr(0, comma))) == wanted) return true;
    if (comma == std::string_view::npos) break;
    value.remove_prefix(comma + 1);
  }
  return false;
}

}  // namespace mob_survivor::net
