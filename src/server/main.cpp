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
      "\n"
      "Serves the Mob Survivor web client from --web and runs the game over\n"
      "WebSocket at /ws. Health: GET /healthz. Status: GET /api/status.\n"
      "\n"
      "  --bind ADDR  IPv4 address to listen on (MOB_SURVIVOR_BIND, default 0.0.0.0)\n"
      "  --port N     TCP port, 0 for any free one (MOB_SURVIVOR_PORT, default 8080)\n"
      "  --web DIR    the web client directory (MOB_SURVIVOR_WEB, default ./web)\n"
      "  --data DIR   where the leaderboard is kept (MOB_SURVIVOR_DATA, default: memory only)");
}

std::string env_or(const char* name, std::string fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? std::string(value) : fallback;
}

void on_signal(int /*signal*/) { mob_survivor::server::Server::stop(); }

}  // namespace

int main(int argc, char** argv) {
  mob_survivor::server::ServerConfig config;
  config.bind = env_or("MOB_SURVIVOR_BIND", config.bind);
  config.web_root = env_or("MOB_SURVIVOR_WEB", config.web_root);
  config.data_dir = env_or("MOB_SURVIVOR_DATA", config.data_dir);
  std::string port = env_or("MOB_SURVIVOR_PORT", "8080");

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--help" || arg == "-h") {
      usage();
      return 0;
    }
    if (arg == "--bind" && has_value) {
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

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  mob_survivor::server::Server server(config);
  if (!server.listen()) return 1;
  return server.run();
}
