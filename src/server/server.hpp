#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "net/leaderboard.hpp"
#include "net/lobby.hpp"
#include "net/websocket.hpp"

namespace mob_survivor::server {

struct ServerConfig {
  std::string bind = "0.0.0.0";
  int port = 8080;
  std::string web_root = "web";
  std::string data_dir;  // empty: keep the leaderboard in memory only
};

// The whole game server in one thread: a poll() loop that accepts HTTP,
// serves the web client from `web_root`, upgrades /ws to WebSocket, and steps
// every room at a fixed 30 ticks a second.
class Server final : public net::Sink {
 public:
  explicit Server(ServerConfig config);
  ~Server() override;
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Binds and listens; returns false (and logs why) on failure.
  bool listen();
  // The bound port (useful with port 0).
  [[nodiscard]] int port() const { return port_; }
  // Runs until stop() is called or a signal asks to stop. Returns the exit code.
  int run();

  static void stop();

  void send_text(net::ClientId client, std::string_view text) override;
  void send_binary(net::ClientId client, std::string_view bytes) override;

 private:
  struct Connection {
    int fd = -1;
    bool websocket = false;
    bool closing = false;  // close once `out` is flushed
    std::string in;
    std::string out;
    net::FrameDecoder decoder;
    net::ClientId client = 0;
    double opened = 0.0;
    double last_heard = 0.0;
    double last_ping = 0.0;
  };

  void accept_all(double now);
  void read_from(Connection& connection, double now);
  void write_to(Connection& connection);
  void handle_http(Connection& connection);
  void handle_frames(Connection& connection);
  void serve_static(Connection& connection, const net::HttpRequest& request, bool head_only);
  void queue(Connection& connection, std::string bytes);
  void drop(int fd);
  void sweep(double now);

  ServerConfig config_;
  int listen_fd_ = -1;
  int port_ = 0;
  std::map<int, Connection> connections_;
  std::map<net::ClientId, int> fd_of_client_;
  net::ClientId next_client_ = 1;
  std::unique_ptr<net::Leaderboard> board_;
  std::unique_ptr<net::Lobby> lobby_;
};

}  // namespace mob_survivor::server
