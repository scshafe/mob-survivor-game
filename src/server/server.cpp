#include "server/server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

#include "mob_survivor/types.hpp"
#include "net/http.hpp"
#include "net/json.hpp"

namespace mob_survivor::server {

namespace {

std::atomic<bool> g_stop{false};

constexpr std::size_t kMaxConnections = 512;
constexpr std::size_t kMaxWebSocketInput = 256 * 1024;
constexpr std::size_t kMaxOutput = 4 * 1024 * 1024;
constexpr double kHttpTimeout = 10.0;
constexpr double kPingEvery = 20.0;
constexpr double kSilenceLimit = 60.0;
constexpr int kMaxCatchUpTicks = 5;

double monotonic_seconds() {
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

double unix_seconds() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void log_event(std::string_view event, std::string_view detail = {}) {
  net::JsonWriter w;
  w.begin_object().field("ts", unix_seconds()).field("event", event);
  if (!detail.empty()) w.field("detail", detail);
  w.end_object();
  std::fprintf(stderr, "%s\n", w.str().c_str());
  std::fflush(stderr);
}

bool set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

Server::Server(ServerConfig config) : config_(std::move(config)) {
  std::string board_path;
  if (!config_.data_dir.empty()) {
    std::error_code error;
    std::filesystem::create_directories(config_.data_dir, error);
    board_path = config_.data_dir + "/leaderboard.tsv";
  }
  board_ = std::make_unique<net::Leaderboard>(board_path);
  daily_ = std::make_unique<net::DailyBoard>(config_.data_dir);
  std::random_device entropy;
  const std::uint64_t seed = (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy() ^
                             static_cast<std::uint64_t>(unix_seconds() * 1000.0);
  lobby_ = std::make_unique<net::Lobby>(*this, *board_, *daily_, seed, unix_seconds());
}

Server::~Server() {
  for (auto& [fd, connection] : connections_) close(fd);
  if (listen_fd_ >= 0) close(listen_fd_);
}

void Server::stop() { g_stop = true; }

bool Server::listen() {
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    log_event("listen_failed", std::strerror(errno));
    return false;
  }
  const int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(config_.port));
  if (inet_pton(AF_INET, config_.bind.c_str(), &address.sin_addr) != 1) {
    log_event("listen_failed", "bad bind address " + config_.bind);
    return false;
  }
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 ||
      ::listen(listen_fd_, 128) != 0 || !set_nonblocking(listen_fd_)) {
    log_event("listen_failed", std::strerror(errno));
    return false;
  }
  socklen_t length = sizeof address;
  getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
  port_ = ntohs(address.sin_port);
  net::JsonWriter w;
  w.begin_object().field("ts", unix_seconds()).field("event", "listening");
  w.field("bind", config_.bind).field("port", port_).field("web", config_.web_root).end_object();
  std::fprintf(stderr, "%s\n", w.str().c_str());
  std::fflush(stderr);
  return true;
}

void Server::queue(Connection& connection, std::string bytes) {
  if (connection.out.size() + bytes.size() > kMaxOutput) {
    // A client this far behind cannot catch up; let it reconnect.
    connection.closing = true;
    connection.out.clear();
    shutdown(connection.fd, SHUT_RDWR);
    return;
  }
  connection.out += bytes;
}

void Server::send_text(net::ClientId client, std::string_view text) {
  const auto found = fd_of_client_.find(client);
  if (found == fd_of_client_.end()) return;
  auto& connection = connections_.at(found->second);
  if (!connection.closing) queue(connection, net::encode_frame(net::Opcode::Text, text));
}

void Server::send_binary(net::ClientId client, std::string_view bytes) {
  const auto found = fd_of_client_.find(client);
  if (found == fd_of_client_.end()) return;
  auto& connection = connections_.at(found->second);
  if (!connection.closing) queue(connection, net::encode_frame(net::Opcode::Binary, bytes));
}

void Server::accept_all(double now) {
  while (true) {
    const int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return;
    if (connections_.size() >= kMaxConnections || !set_nonblocking(fd)) {
      close(fd);
      continue;
    }
    const int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    Connection connection;
    connection.fd = fd;
    connection.opened = now;
    connection.last_heard = now;
    connection.last_ping = now;
    connections_.emplace(fd, std::move(connection));
  }
}

void Server::drop(int fd) {
  const auto found = connections_.find(fd);
  if (found == connections_.end()) return;
  const net::ClientId client = found->second.client;
  close(fd);
  connections_.erase(found);
  if (client != 0) {
    fd_of_client_.erase(client);
    lobby_->disconnect(client);
  }
}

void Server::read_from(Connection& connection, double now) {
  char buffer[16384];
  while (true) {
    const ssize_t got = recv(connection.fd, buffer, sizeof buffer, 0);
    if (got > 0) {
      connection.in.append(buffer, static_cast<std::size_t>(got));
      connection.last_heard = now;
      const std::size_t limit = connection.websocket ? kMaxWebSocketInput : net::kMaxRequestHead + 1;
      if (connection.in.size() > limit) {
        connection.closing = true;
        connection.out.clear();
        return;
      }
      continue;
    }
    if (got == 0) {
      connection.closing = true;
      connection.out.clear();
      return;
    }
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      connection.closing = true;
      connection.out.clear();
    }
    return;
  }
}

void Server::write_to(Connection& connection) {
  while (!connection.out.empty()) {
    const ssize_t sent = ::send(connection.fd, connection.out.data(), connection.out.size(), MSG_NOSIGNAL);
    if (sent > 0) {
      connection.out.erase(0, static_cast<std::size_t>(sent));
      continue;
    }
    if (sent < 0 && errno == EINTR) continue;
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    connection.closing = true;
    connection.out.clear();
    return;
  }
}

void Server::serve_static(Connection& connection, const net::HttpRequest& request, bool head_only) {
  const auto relative = net::static_file_path(request.path());
  if (!relative) {
    queue(connection, net::http_response(404, "text/plain; charset=utf-8", "Not found\n", {}, head_only));
    return;
  }
  const std::filesystem::path path = std::filesystem::path(config_.web_root) / *relative;
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    queue(connection, net::http_response(404, "text/plain; charset=utf-8", "Not found\n", {}, head_only));
    return;
  }
  std::ifstream file(path, std::ios::binary);
  std::ostringstream body;
  body << file.rdbuf();
  queue(connection, net::http_response(200, net::mime_type(*relative), body.str(), "Cache-Control: no-cache\r\n",
                                       head_only));
}

void Server::handle_http(Connection& connection) {
  net::HttpRequest request;
  std::size_t consumed = 0;
  const auto status = net::parse_request(connection.in, request, consumed);
  if (status == net::ParseStatus::Incomplete) return;
  connection.closing = true;
  if (status == net::ParseStatus::Error) {
    queue(connection, net::http_response(400, "text/plain; charset=utf-8", "Bad request\n"));
    return;
  }
  connection.in.erase(0, consumed);
  const std::string_view path = request.path();

  if (path == "/ws") {
    if (!net::is_websocket_upgrade(request)) {
      queue(connection, net::http_response(426, "text/plain; charset=utf-8", "WebSocket only\n",
                                           "Upgrade: websocket\r\n"));
      return;
    }
    connection.closing = false;
    connection.websocket = true;
    connection.client = next_client_++;
    fd_of_client_[connection.client] = connection.fd;
    queue(connection, net::websocket_handshake_response(*request.header("sec-websocket-key")));
    // Behind `tailscale serve`, the tailnet user's display name arrives as a
    // header; it only suggests a name, which the player can change.
    const std::string suggested(request.header("tailscale-user-name").value_or(""));
    lobby_->connect(connection.client, suggested);
    handle_frames(connection);
    return;
  }

  const bool head_only = request.method == "HEAD";
  if (request.method != "GET" && !head_only) {
    queue(connection, net::http_response(405, "text/plain; charset=utf-8", "Method not allowed\n", "Allow: GET, HEAD\r\n"));
    return;
  }
  if (path == "/healthz") {
    queue(connection, net::http_response(200, "text/plain; charset=utf-8", "ok\n", {}, head_only));
    return;
  }
  if (path == "/api/status") {
    queue(connection, net::http_response(200, "application/json", lobby_->status_json(), "Cache-Control: no-store\r\n",
                                         head_only));
    return;
  }
  serve_static(connection, request, head_only);
}

void Server::handle_frames(Connection& connection) {
  net::WsMessage message;
  while (!connection.closing) {
    const auto result = connection.decoder.next(connection.in, message);
    if (result == net::FrameDecoder::Result::NeedMore) return;
    if (result == net::FrameDecoder::Result::Error) {
      queue(connection, net::encode_frame(net::Opcode::Close, std::string("\x03\xea", 2)));  // 1002
      connection.closing = true;
      return;
    }
    switch (message.opcode) {
      case net::Opcode::Text:
        lobby_->message(connection.client, message.payload);
        break;
      case net::Opcode::Ping:
        queue(connection, net::encode_frame(net::Opcode::Pong, message.payload));
        break;
      case net::Opcode::Close:
        queue(connection, net::encode_frame(net::Opcode::Close, message.payload.substr(0, 2)));
        connection.closing = true;
        break;
      case net::Opcode::Binary:
      case net::Opcode::Pong:
      case net::Opcode::Continuation:
        break;
    }
  }
}

void Server::sweep(double now) {
  std::vector<int> doomed;
  for (auto& [fd, connection] : connections_) {
    if (!connection.websocket && now - connection.opened > kHttpTimeout) {
      doomed.push_back(fd);
      continue;
    }
    if (connection.websocket && !connection.closing) {
      if (now - connection.last_heard > kSilenceLimit) {
        doomed.push_back(fd);
        continue;
      }
      if (now - connection.last_ping > kPingEvery) {
        connection.last_ping = now;
        queue(connection, net::encode_frame(net::Opcode::Ping, "k"));
      }
    }
    if (connection.closing && connection.out.empty()) doomed.push_back(fd);
  }
  for (const int fd : doomed) drop(fd);
}

int Server::run() {
  const double tick = kTickSeconds;
  double next_tick = monotonic_seconds() + tick;
  std::vector<pollfd> fds;
  while (!g_stop) {
    fds.clear();
    fds.push_back({listen_fd_, POLLIN, 0});
    for (const auto& [fd, connection] : connections_) {
      short events = POLLIN;
      if (!connection.out.empty()) events = static_cast<short>(events | POLLOUT);
      fds.push_back({fd, events, 0});
    }
    const double wait = next_tick - monotonic_seconds();
    const int timeout_ms = wait <= 0.0 ? 0 : static_cast<int>(wait * 1000.0) + 1;
    const int ready = poll(fds.data(), fds.size(), timeout_ms);
    if (ready < 0 && errno != EINTR) {
      log_event("poll_failed", std::strerror(errno));
      return 1;
    }
    const double now = monotonic_seconds();
    if (ready > 0) {
      for (const pollfd& entry : fds) {
        if (entry.revents == 0) continue;
        if (entry.fd == listen_fd_) {
          accept_all(now);
          continue;
        }
        auto found = connections_.find(entry.fd);
        if (found == connections_.end()) continue;
        Connection& connection = found->second;
        if ((entry.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
          read_from(connection, now);
          if (connection.websocket) {
            handle_frames(connection);
          } else if (!connection.closing) {
            handle_http(connection);
          }
        }
        if (!connection.out.empty()) write_to(connection);
      }
    }

    int ticks = 0;
    while (monotonic_seconds() >= next_tick && ticks < kMaxCatchUpTicks) {
      lobby_->set_clock(unix_seconds());
      lobby_->tick(tick);
      next_tick += tick;
      ++ticks;
    }
    if (monotonic_seconds() >= next_tick) next_tick = monotonic_seconds() + tick;  // fell behind: skip ahead
    for (auto& [fd, connection] : connections_) {
      if (!connection.out.empty()) write_to(connection);
    }
    sweep(now);
  }
  log_event("stopping");
  return 0;
}

}  // namespace mob_survivor::server
