#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "server/server.hpp"

namespace {

void usage() {
  std::puts(
      "usage: mob-survivor-server [--bind ADDR] [--port N] [--web DIR] [--data DIR]\n"
      "       mob-survivor-server --healthcheck [--bind ADDR] [--port N]\n"
      "\n"
      "Serves the Mob Survivor web client from --web and runs the game over\n"
      "WebSocket at /ws. Health: GET /healthz. Status: GET /api/status.\n"
      "\n"
      "  --bind ADDR  IPv4 address to listen on (MOB_SURVIVOR_BIND, default 0.0.0.0)\n"
      "  --port N     TCP port, 0 for any free one (MOB_SURVIVOR_PORT, default 8080)\n"
      "  --web DIR    the web client directory (MOB_SURVIVOR_WEB, default ./web)\n"
      "  --data DIR   where the leaderboard is kept (MOB_SURVIVOR_DATA, default: memory only)\n"
      "  --healthcheck  probe GET /healthz on the running server (127.0.0.1 when\n"
      "               bound to 0.0.0.0) and exit 0 on 200, 1 otherwise: the\n"
      "               container's healthcheck, since the image has no curl");
}

std::string env_or(const char* name, std::string fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? std::string(value) : fallback;
}

// The container healthcheck: one GET /healthz to the server's own address,
// with a 3 s budget for connect, send and receive.
int healthcheck(const std::string& bind, int port) {
  const std::string host = bind.empty() || bind == "0.0.0.0" ? "127.0.0.1" : bind;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (port <= 0 || inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    std::fprintf(stderr, "mob-survivor-server: healthcheck needs an IPv4 --bind and a --port\n");
    return 1;
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 1;
  timeval timeout{};
  timeout.tv_sec = 3;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
  std::string response;
  if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) {
    const std::string request = "GET /healthz HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(request.size())) {
      char buffer[256];
      ssize_t got = 0;
      while (response.size() < 64 && (got = recv(fd, buffer, sizeof buffer, 0)) > 0) {
        response.append(buffer, static_cast<size_t>(got));
      }
    }
  }
  close(fd);
  const bool ok = response.rfind("HTTP/1.1 200 ", 0) == 0 || response.rfind("HTTP/1.0 200 ", 0) == 0;
  if (!ok) std::fprintf(stderr, "mob-survivor-server: healthcheck failed\n");
  return ok ? 0 : 1;
}

void on_signal(int /*signal*/) { mob_survivor::server::Server::stop(); }

}  // namespace

int main(int argc, char** argv) {
  mob_survivor::server::ServerConfig config;
  config.bind = env_or("MOB_SURVIVOR_BIND", config.bind);
  config.web_root = env_or("MOB_SURVIVOR_WEB", config.web_root);
  config.data_dir = env_or("MOB_SURVIVOR_DATA", config.data_dir);
  std::string port = env_or("MOB_SURVIVOR_PORT", "8080");

  bool probe = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    }
    if (arg == "--healthcheck") {
      probe = true;
    } else if (arg == "--bind" && has_value) {
      config.bind = argv[++i];
    } else if (arg == "--port" && has_value) {
      port = argv[++i];
    } else if (arg == "--web" && has_value) {
      config.web_root = argv[++i];
    } else if (arg == "--data" && has_value) {
      config.data_dir = argv[++i];
    } else {
      std::fprintf(stderr, "mob-survivor-server: unknown or incomplete argument '%s'\n", argv[i]);
      usage();
      return 2;
    }
  }
  char* end = nullptr;
  const long parsed = std::strtol(port.c_str(), &end, 10);
  if (end == port.c_str() || *end != '\0' || parsed < 0 || parsed > 65535) {
    std::fprintf(stderr, "mob-survivor-server: bad port '%s'\n", port.c_str());
    return 2;
  }
  config.port = static_cast<int>(parsed);
  if (probe) return healthcheck(config.bind, config.port);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  mob_survivor::server::Server server(config);
  if (!server.listen()) return 1;
  return server.run();
}
