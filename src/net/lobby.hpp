#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mob_survivor/match.hpp"
#include "mob_survivor/rng.hpp"
#include "net/json.hpp"
#include "net/leaderboard.hpp"

namespace mob_survivor::net {

using ClientId = std::uint32_t;

// Where the lobby's messages go. The server implements it over WebSockets;
// tests implement it with a recorder.
class Sink {
 public:
  virtual ~Sink() = default;
  virtual void send_text(ClientId client, std::string_view text) = 0;
  virtual void send_binary(ClientId client, std::string_view bytes) = 0;
};

inline constexpr std::size_t kMaxSeats = 4;
inline constexpr std::size_t kMaxSeatsPerVersusTeam = 2;
inline constexpr std::size_t kMaxRoomMembers = 12;
inline constexpr std::size_t kMaxRooms = 200;
inline constexpr double kReconnectGraceSeconds = 60.0;
inline constexpr int kSnapshotEveryTicks = 2;  // 15 snapshots a second at 30 ticks

// Every room, every connected client and the messages between them. It knows
// nothing about sockets: the server feeds it connects, messages and ticks.
// Single-threaded by design: the server calls it from its one event loop.
class Lobby {
 public:
  Lobby(Sink& sink, Leaderboard& board, std::uint64_t seed, double clock_seconds = 0.0);

  void connect(ClientId client, std::string_view suggested_name);
  void disconnect(ClientId client);
  void message(ClientId client, std::string_view text);
  // Advances every room by one simulation tick.
  void tick(double dt_seconds);
  // Wall-clock unix seconds, for leaderboard dates (the server sets it).
  void set_clock(double unix_seconds) { clock_ = unix_seconds; }

  [[nodiscard]] std::size_t room_count() const { return rooms_.size(); }
  [[nodiscard]] std::size_t client_count() const { return clients_.size(); }
  [[nodiscard]] std::string status_json() const;

 private:
  struct Member {
    ClientId client = 0;  // 0: a bot, or a player who dropped
    std::string name;
    std::string token;
    Team team = Team::Blue;
    bool bot = false;
    bool seated = true;  // has (or will have) a cannon; else a spectator
    int slot = -1;       // the cannon, while a match runs
    bool connected = true;
    double gone_for = 0.0;
  };

  struct Room {
    std::string code;
    Mode mode = Mode::Campaign;
    bool is_public = true;
    ClientId host = 0;
    std::vector<Member> members;
    std::unique_ptr<Match> match;
    std::uint32_t sent_serial = 0;
    Phase sent_phase = Phase::Lobby;
    std::vector<bool> sent_picks;
    int tick = 0;
    double empty_for = 0.0;
  };

  struct Client {
    std::string name;
    std::string token;
    std::string room;  // code, empty when in no room
    double budget = 120.0;
  };

  void handle(ClientId id, Client& client, const JsonValue& message);
  void hello(ClientId id, Client& client, const JsonValue& message);
  void create_room(ClientId id, Client& client, const JsonValue& message);
  void join_room(ClientId id, Client& client, std::string_view code);
  void leave_room(ClientId id, Client& client);
  void start_match(Room& room);
  void back_to_lobby(Room& room);
  void add_bot(Room& room, Team team);
  void tick_room(Room& room, double dt);
  void finish_match(Room& room);

  Room* room_of(const Client& client);
  Member* member_of(Room& room, ClientId id);
  Member* seated_by_slot(Room& room, int slot);
  [[nodiscard]] std::size_t seated_count(const Room& room, std::optional<Team> team = std::nullopt) const;
  [[nodiscard]] std::size_t humans_connected(const Room& room) const;
  Team open_team(const Room& room) const;
  void promote_host(Room& room);

  void send(ClientId id, std::string_view text);
  void send_error(ClientId id, std::string_view text);
  void broadcast(const Room& room, std::string_view text);
  void broadcast_binary(const Room& room, std::string_view bytes);
  void send_room(const Room& room);
  [[nodiscard]] std::string room_json(const Room& room, ClientId you) const;
  [[nodiscard]] std::string rooms_json() const;
  void send_match_state(const Room& room, ClientId id);
  void send_picks(Room& room);

  std::string make_code();
  std::string make_token();
  std::string clean_name(std::string_view raw, std::string_view fallback);

  Sink& sink_;
  Leaderboard& board_;
  Rng rng_;
  double clock_;
  std::map<ClientId, Client> clients_;
  std::map<std::string, Room> rooms_;
  std::uint64_t matches_started_ = 0;
};

}  // namespace mob_survivor::net
