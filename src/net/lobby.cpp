#include "net/lobby.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "net/protocol.hpp"

namespace mob_survivor::net {

namespace {

constexpr std::size_t kMaxMessageBytes = 4096;
constexpr double kMessageBudget = 120.0;
constexpr double kMessageRefillPerSecond = 60.0;
constexpr std::size_t kMaxNameCodePoints = 16;
constexpr int kEmoteCount = 6;
constexpr std::array<std::string_view, 6> kBotNames{"Bot Rex", "Bot Ada", "Bot Max", "Bot Zoe", "Bot Kit", "Bot Ivy"};

}  // namespace

Lobby::Lobby(Sink& sink, Leaderboard& board, DailyBoard& daily, std::uint64_t seed, double clock_seconds)
    : sink_(sink), board_(board), daily_(daily), rng_(seed), clock_(clock_seconds) {}

namespace {

std::string_view kind_name(int kind) {
  switch (kind) {
    case 1:
      return "daily";
    case 2:
      return "replay";
    default:
      return "room";
  }
}

}  // namespace

// ---------------------------------------------------------------- utilities

void Lobby::send(ClientId id, std::string_view text) { sink_.send_text(id, text); }

void Lobby::send_error(ClientId id, std::string_view text) {
  JsonWriter w;
  w.begin_object().field("t", "error").field("message", text).end_object();
  send(id, w.str());
}

void Lobby::broadcast(const Room& room, std::string_view text) {
  for (const Member& member : room.members) {
    if (member.client != 0) send(member.client, text);
  }
}

void Lobby::broadcast_binary(const Room& room, std::string_view bytes) {
  for (const Member& member : room.members) {
    if (member.client != 0) sink_.send_binary(member.client, bytes);
  }
}

std::string Lobby::make_code() {
  static constexpr std::string_view kLetters = "ABCDEFGHJKMNPQRSTUVWXYZ";
  for (int attempt = 0; attempt < 1000; ++attempt) {
    std::string code;
    for (int i = 0; i < 4; ++i) code.push_back(kLetters[static_cast<std::size_t>(rng_.range(0, static_cast<int>(kLetters.size()) - 1))]);
    if (!rooms_.contains(code)) return code;
  }
  return "ZZZZ";
}

std::string Lobby::make_token() {
  char buffer[33];
  std::snprintf(buffer, sizeof buffer, "%016llx%016llx", static_cast<unsigned long long>(rng_.next()),
                static_cast<unsigned long long>(rng_.next()));
  return buffer;
}

std::string Lobby::clean_name(std::string_view raw, std::string_view fallback) {
  std::string out;
  std::size_t code_points = 0;
  while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20U) raw.remove_prefix(1);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const auto byte = static_cast<unsigned char>(raw[i]);
    if (byte < 0x20U || byte == 0x7FU) continue;
    const bool continuation = (byte & 0xC0U) == 0x80U;
    if (!continuation) {
      if (code_points == kMaxNameCodePoints) break;
      ++code_points;
    }
    out.push_back(static_cast<char>(byte));
  }
  // Drop a trailing partial UTF-8 sequence cut off by the length limit.
  while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0U) == 0xC0U) out.pop_back();
  const auto first = out.find_first_not_of(' ');
  if (first == std::string::npos) return std::string(fallback);
  const auto last = out.find_last_not_of(' ');
  return out.substr(first, last - first + 1);
}

Lobby::Room* Lobby::room_of(const Client& client) {
  if (client.room.empty()) return nullptr;
  const auto found = rooms_.find(client.room);
  return found == rooms_.end() ? nullptr : &found->second;
}

Lobby::Member* Lobby::member_of(Room& room, ClientId id) {
  for (Member& member : room.members) {
    if (member.client == id && id != 0) return &member;
  }
  return nullptr;
}

Lobby::Member* Lobby::seated_by_slot(Room& room, int slot) {
  for (Member& member : room.members) {
    if (member.seated && member.slot == slot) return &member;
  }
  return nullptr;
}

std::size_t Lobby::seated_count(const Room& room, std::optional<Team> team) const {
  return static_cast<std::size_t>(std::count_if(room.members.begin(), room.members.end(), [&](const Member& member) {
    return member.seated && (!team || member.team == *team);
  }));
}

std::size_t Lobby::humans_connected(const Room& room) const {
  return static_cast<std::size_t>(std::count_if(room.members.begin(), room.members.end(),
                                                [](const Member& member) { return member.client != 0; }));
}

Team Lobby::open_team(const Room& room) const {
  if (room.mode == Mode::Campaign) return Team::Blue;
  return seated_count(room, Team::Red) < seated_count(room, Team::Blue) ? Team::Red : Team::Blue;
}

void Lobby::promote_host(Room& room) {
  if (member_of(room, room.host) != nullptr) return;
  room.host = 0;
  for (const Member& member : room.members) {
    if (member.client != 0) {
      room.host = member.client;
      return;
    }
  }
}

// ---------------------------------------------------------------- messages out

std::string Lobby::room_json(const Room& room, ClientId you) const {
  JsonWriter w;
  w.begin_object();
  w.field("t", "room");
  w.field("code", room.code);
  w.field("mode", mode_name(room.mode));
  w.field("public", room.is_public);
  w.field("kind", kind_name(static_cast<int>(room.kind)));
  if (room.kind == Room::Kind::Daily) w.field("day", room.day);
  if (room.kind == Room::Kind::Replay) w.field("replayOf", room.playback_name);
  w.field("phase", phase_name(room.match ? room.match->phase() : Phase::Lobby));
  w.field("host", room.host);
  w.field("you", you);
  w.field("maxSeats", static_cast<int>(kMaxSeats));
  w.key("members").begin_array();
  for (const Member& member : room.members) {
    w.begin_object();
    w.field("id", member.client);
    w.field("name", member.name);
    w.field("team", team_index(member.team));
    w.field("bot", member.bot);
    w.field("seated", member.seated);
    w.field("slot", member.slot);
    w.field("connected", member.bot || member.connected);
    w.field("host", member.client != 0 && member.client == room.host);
    w.end_object();
  }
  w.end_array();
  w.end_object();
  return w.take();
}

void Lobby::send_room(const Room& room) {
  for (const Member& member : room.members) {
    if (member.client != 0) send(member.client, room_json(room, member.client));
  }
}

std::string Lobby::rooms_json() const {
  JsonWriter w;
  w.begin_object();
  w.field("t", "rooms");
  w.key("rooms").begin_array();
  for (const auto& [code, room] : rooms_) {
    if (!room.is_public) continue;
    std::string host_name;
    for (const Member& member : room.members) {
      if (member.client == room.host && member.client != 0) host_name = member.name;
    }
    w.begin_object();
    w.field("code", code);
    w.field("mode", mode_name(room.mode));
    w.field("phase", phase_name(room.match ? room.match->phase() : Phase::Lobby));
    w.field("seated", static_cast<int>(seated_count(room)));
    w.field("members", static_cast<int>(room.members.size()));
    w.field("maxSeats", static_cast<int>(kMaxSeats));
    w.field("host", host_name);
    if (room.match) w.field("level", room.match->level());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  return w.take();
}

std::string Lobby::status_json() const {
  JsonWriter w;
  w.begin_object();
  w.field("ok", true);
  w.field("protocol", kProtocolVersion);
  w.field("rooms", static_cast<int>(rooms_.size()));
  w.field("clients", static_cast<int>(clients_.size()));
  w.field("matchesStarted", static_cast<std::int64_t>(matches_started_));
  w.end_object();
  return w.take();
}

void Lobby::send_match_state(const Room& room, ClientId id) {
  if (!room.match) return;
  send(id, encode_level(*room.match));
  for (const Member& member : room.members) {
    if (member.client == id && member.seated && member.slot >= 0) {
      if (auto cards = encode_cards(*room.match, member.slot)) send(id, *cards);
    }
  }
}

void Lobby::send_picks(Room& room) {
  if (!room.match) return;
  std::vector<bool> picks;
  JsonWriter w;
  w.begin_object().field("t", "picks").key("picked").begin_array();
  for (const MatchSeat& seat : room.match->seats()) {
    picks.push_back(seat.picked);
    if (seat.picked) w.value(seat.spec.slot);
  }
  w.end_array().end_object();
  if (picks == room.sent_picks) return;
  room.sent_picks = std::move(picks);
  broadcast(room, w.str());
}

// ---------------------------------------------------------------- connections

void Lobby::connect(ClientId id, std::string_view suggested_name) {
  Client client;
  client.name = clean_name(suggested_name, "Player " + std::to_string(id % 1000));
  client.token = make_token();
  client.budget = kMessageBudget;
  JsonWriter w;
  w.begin_object();
  w.field("t", "welcome").field("v", kProtocolVersion).field("id", id);
  w.field("name", client.name).field("token", client.token);
  w.end_object();
  clients_[id] = std::move(client);
  send(id, w.str());
  send(id, board_.to_json());
  send(id, rooms_json());
}

void Lobby::disconnect(ClientId id) {
  const auto found = clients_.find(id);
  if (found == clients_.end()) return;
  if (Room* room = room_of(found->second)) {
    for (auto it = room->members.begin(); it != room->members.end(); ++it) {
      if (it->client != id) continue;
      if (room->match && it->seated && it->slot >= 0) {
        // Keep the seat so the player can come back (see hello).
        it->client = 0;
        it->connected = false;
        it->gone_for = 0.0;
        room->log.connect(*room->match, it->slot, false);
      } else {
        room->members.erase(it);
      }
      break;
    }
    promote_host(*room);
    if (humans_connected(*room) == 0 && !room->match) {
      const std::string code = room->code;
      rooms_.erase(code);
    } else {
      send_room(*room);
    }
  }
  clients_.erase(found);
}

void Lobby::message(ClientId id, std::string_view text) {
  const auto found = clients_.find(id);
  if (found == clients_.end() || text.size() > kMaxMessageBytes) return;
  Client& client = found->second;
  if (client.budget < 1.0) return;  // flooding: drop until the budget refills
  client.budget -= 1.0;
  const auto parsed = parse_json(text);
  if (!parsed || !parsed->is_object()) {
    send_error(id, "Malformed message.");
    return;
  }
  handle(id, client, *parsed);
}

void Lobby::handle(ClientId id, Client& client, const JsonValue& message) {
  const std::string_view type = message.get_string("t");
  if (type == "hello") return hello(id, client, message);
  if (type == "rooms") return send(id, rooms_json());
  if (type == "board") return send(id, board_.to_json());
  if (type == "create") return create_room(id, client, message);
  if (type == "daily") return start_daily(id, client);
  if (type == "dailyboard") return send(id, daily_.to_json(today(), client.name));
  if (type == "watch") return watch_replay(id, client, message.get_string("replay"));
  if (type == "join") return join_room(id, client, message.get_string("code"));
  if (type == "leave") {
    leave_room(id, client);
    return send(id, rooms_json());
  }
  if (type == "ping") {
    JsonWriter w;
    w.begin_object().field("t", "pong").field("n", message.get_number("n")).end_object();
    return send(id, w.str());
  }

  Room* room = room_of(client);
  if (room == nullptr) return;
  Member* member = member_of(*room, id);
  if (member == nullptr) return;
  const bool is_host = room->host == id;

  if (type == "emote") {
    const int emote = static_cast<int>(message.get_number("e", -1));
    if (emote < 0 || emote >= kEmoteCount) return;
    JsonWriter w;
    w.begin_object().field("t", "emote").field("id", id).field("slot", member->slot).field("e", emote);
    w.field("name", member->name).end_object();
    return broadcast(*room, w.str());
  }

  if (!room->match) {
    // A daily run has one seat; nobody plays in a replay.
    const bool fixed = room->kind != Room::Kind::Open;
    if (fixed && type != "spectate" && type != "start" && type != "team") return;
    if (type == "team" && room->kind == Room::Kind::Replay) return;
    if (type == "team" && room->kind == Room::Kind::Daily && !member->seated && seated_count(*room) >= 1) {
      return send_error(id, "A daily run is for one player.");
    }
    if (type == "start" && room->kind == Room::Kind::Replay) return;
    if (type == "team") {
      // Also how a spectator takes a free seat; a campaign has only Blue.
      const Team team =
          room->mode == Mode::Versus && message.get_number("team") >= 1.0 ? Team::Red : Team::Blue;
      if (member->team == team && member->seated) return;
      if (room->mode == Mode::Versus && seated_count(*room, team) >= kMaxSeatsPerVersusTeam) {
        return send_error(id, "That team is full.");
      }
      if (!member->seated && seated_count(*room) >= kMaxSeats) return send_error(id, "Every seat is taken.");
      member->team = team;
      member->seated = true;
      return send_room(*room);
    }
    if (type == "spectate") {
      member->seated = false;
      return send_room(*room);
    }
    if (!is_host) return;
    if (type == "bot") {
      const Team team = room->mode == Mode::Versus && message.get_number("team") >= 1.0 ? Team::Red : Team::Blue;
      add_bot(*room, team);
      return send_room(*room);
    }
    if (type == "unbot") {
      for (auto it = room->members.end(); it != room->members.begin();) {
        --it;
        if (it->bot) {
          room->members.erase(it);
          break;
        }
      }
      return send_room(*room);
    }
    if (type == "start") return start_match(*room);
    return;
  }

  Match& match = *room->match;
  const int slot = member->seated ? member->slot : -1;
  InputLog& log = room->log;
  if (type == "in") {
    if (slot >= 0) log.aim(match, slot, message.get_number("x", kFieldWidth / 2.0), message.get_bool("f"));
    return;
  }
  if (type == "giant") {
    if (slot >= 0) log.giant(match, slot);
    return;
  }
  if (type == "phase") {
    if (slot >= 0) log.phase(match, slot);
    return;
  }
  if (type == "bomb") {
    if (slot >= 0) log.bomb(match, slot, {message.get_number("x", -1.0), message.get_number("y", -1.0)});
    return;
  }
  if (type == "pick") {
    if (slot >= 0 && log.pick(match, slot, static_cast<int>(message.get_number("i", -1)))) {
      if (auto cards = encode_cards(match, slot)) send(id, *cards);
      send_picks(*room);
    }
    return;
  }
  if (type == "again" && is_host && match.phase() == Phase::GameOver && room->kind != Room::Kind::Replay) {
    back_to_lobby(*room);
    // A daily run goes again at once: the same seed, as practice.
    if (room->kind == Room::Kind::Daily) start_match(*room);
    return;
  }
}

void Lobby::hello(ClientId id, Client& client, const JsonValue& message) {
  client.name = clean_name(message.get_string("name"), client.name);
  const std::string_view token = message.get_string("token");
  JsonWriter w;
  if (!token.empty() && token.size() <= 64 && client.room.empty()) {
    for (auto& [code, room] : rooms_) {
      for (Member& member : room.members) {
        if (member.bot || member.client != 0 || member.token != token) continue;
        member.client = id;
        member.connected = true;
        member.gone_for = 0.0;
        member.name = client.name;
        client.token = std::string(token);
        client.room = code;
        if (room.match && member.seated && member.slot >= 0) room.log.connect(*room.match, member.slot, true);
        promote_host(room);
        w.begin_object().field("t", "session").field("name", client.name).field("token", client.token);
        w.field("rejoined", code).end_object();
        send(id, w.str());
        send_room(room);
        send_match_state(room, id);
        return;
      }
    }
  }
  if (Room* room = room_of(client)) {
    if (Member* member = member_of(*room, id)) {
      member->name = client.name;
      send_room(*room);
    }
  }
  w.begin_object().field("t", "session").field("name", client.name).field("token", client.token).end_object();
  send(id, w.str());
  send(id, daily_.to_json(today(), client.name));
}

void Lobby::create_room(ClientId id, Client& client, const JsonValue& message) {
  const auto mode = parse_mode(message.get_string("mode", "campaign"));
  if (!mode) return send_error(id, "Unknown mode.");
  if (rooms_.size() >= kMaxRooms) return send_error(id, "The server is full; try again soon.");
  leave_room(id, client);
  Room room;
  room.code = make_code();
  room.mode = *mode;
  room.is_public = message.get_bool("public", true);
  room.host = id;
  Member member;
  member.client = id;
  member.name = client.name;
  member.token = client.token;
  member.team = Team::Blue;
  room.members.push_back(member);
  client.room = room.code;
  const std::string code = room.code;
  Room& stored = rooms_.emplace(code, std::move(room)).first->second;
  // A versus room with "bots" gets one opponent bot up front.
  if (stored.mode == Mode::Versus && message.get_bool("bots")) add_bot(stored, Team::Red);
  send_room(stored);
  if (message.get_bool("start")) start_match(stored);
}

void Lobby::join_room(ClientId id, Client& client, std::string_view raw_code) {
  std::string code;
  for (const char c : raw_code) {
    if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
      code.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
  }
  const auto found = rooms_.find(code);
  if (found == rooms_.end()) return send_error(id, "No room with code " + code + ".");
  Room& room = found->second;
  if (client.room == code) return send(id, room_json(room, id));
  if (room.members.size() >= kMaxRoomMembers) return send_error(id, "That room is full.");
  leave_room(id, client);
  Member member;
  member.client = id;
  member.name = client.name;
  member.token = client.token;
  member.team = open_team(room);
  member.seated = !room.match && seated_count(room) < kMaxSeats &&
                  (room.mode == Mode::Campaign || seated_count(room, member.team) < kMaxSeatsPerVersusTeam);
  room.members.push_back(member);
  client.room = code;
  if (room.host == 0) room.host = id;
  send_room(room);
  send_match_state(room, id);
}

void Lobby::leave_room(ClientId id, Client& client) {
  Room* room = room_of(client);
  client.room.clear();
  if (room == nullptr) return;
  for (auto it = room->members.begin(); it != room->members.end(); ++it) {
    if (it->client != id) continue;
    if (room->match && it->seated && it->slot >= 0) {
      // The cannon stays (idle) until the match ends; nobody can reclaim it.
      it->client = 0;
      it->connected = false;
      it->token.clear();
      room->log.connect(*room->match, it->slot, false);
    } else {
      room->members.erase(it);
    }
    break;
  }
  promote_host(*room);
  if (humans_connected(*room) == 0) {
    const std::string code = room->code;
    rooms_.erase(code);
    return;
  }
  send_room(*room);
}

// ---------------------------------------------------------------- matches

void Lobby::add_bot(Room& room, Team team) {
  if (room.match || room.members.size() >= kMaxRoomMembers || seated_count(room) >= kMaxSeats) return;
  if (room.mode == Mode::Campaign) team = Team::Blue;
  if (room.mode == Mode::Versus && seated_count(room, team) >= kMaxSeatsPerVersusTeam) return;
  const auto bots = std::count_if(room.members.begin(), room.members.end(), [](const Member& m) { return m.bot; });
  Member bot;
  bot.bot = true;
  bot.name = std::string(kBotNames.at(static_cast<std::size_t>(bots) % kBotNames.size()));
  bot.team = team;
  bot.connected = true;
  room.members.push_back(bot);
}

void Lobby::start_match(Room& room) {
  if (room.match) return;
  if (seated_count(room) == 0) return;
  if (room.mode == Mode::Versus) {
    if (seated_count(room, Team::Blue) == 0) add_bot(room, Team::Blue);
    if (seated_count(room, Team::Red) == 0) add_bot(room, Team::Red);
    if (seated_count(room, Team::Blue) == 0 || seated_count(room, Team::Red) == 0) return;
  }
  std::vector<SeatSpec> seats;
  int slot = 0;
  for (Member& member : room.members) {
    member.slot = -1;
    if (!member.seated) continue;
    member.slot = slot++;
    seats.push_back({member.slot, room.mode == Mode::Campaign ? Team::Blue : member.team, member.bot});
  }
  const std::uint64_t seed = room.kind == Room::Kind::Daily ? daily_seed(room.day) : rng_.next();
  room.log.clear();
  room.ghost.reset();
  if (room.kind == Room::Kind::Daily) {
    // Today's best run plays alongside, as a ghost.
    if (const DailyBoard::Run* best = daily_.best(room.day); best != nullptr && !best->replay.empty()) {
      if (const Replay* replay = daily_.replay(best->replay)) {
        room.ghost = std::make_unique<ReplayPlayer>(*replay);
        room.ghost_name = best->name;
      }
    }
  }
  room.match = std::make_unique<Match>(room.mode, seed, std::move(seats));
  for (const Member& member : room.members) {
    if (member.slot >= 0 && !member.bot && !member.connected) room.log.connect(*room.match, member.slot, false);
  }
  ++matches_started_;
  room.tick = 0;
  room.sent_serial = room.match->level_serial();
  room.sent_phase = room.match->phase();
  room.sent_picks.clear();
  send_room(room);
  broadcast(room, encode_level(*room.match));
}

void Lobby::back_to_lobby(Room& room) {
  room.match.reset();
  std::erase_if(room.members, [](const Member& member) { return !member.bot && member.client == 0; });
  for (Member& member : room.members) {
    member.slot = -1;
    member.seated = false;
  }
  for (Member& member : room.members) {
    if (seated_count(room) >= kMaxSeats) break;
    if (room.mode == Mode::Versus && seated_count(room, member.team) >= kMaxSeatsPerVersusTeam) {
      const Team other = other_team(member.team);
      if (seated_count(room, other) >= kMaxSeatsPerVersusTeam) continue;
      member.team = other;
    }
    member.seated = true;
  }
  room.sent_phase = Phase::Lobby;
  promote_host(room);
  send_room(room);
}

void Lobby::finish_match(Room& room) {
  const Match& match = *room.match;
  int rank = -1;
  int daily_place = -1;
  std::string replay_id;
  if (room.kind == Room::Kind::Daily) {
    std::string name;
    int kills = 0;
    for (const Member& member : room.members) {
      if (member.slot < 0) continue;
      kills += match.total_tally(member.slot).kills;
      name = member.name;
    }
    char suffix[12];
    std::snprintf(suffix, sizeof suffix, "%08llx", static_cast<unsigned long long>(rng_.next() & 0xffffffffULL));
    replay_id = room.day + "-" + suffix;
    Replay replay;
    replay.mode = match.mode();
    replay.seed = daily_seed(room.day);
    for (const MatchSeat& seat : match.seats()) replay.seats.push_back(seat.spec);
    replay.inputs = room.log.inputs();
    daily_place = daily_.add({room.day, name, match.levels_cleared(), kills, static_cast<std::int64_t>(clock_), replay_id},
                             replay);
  } else if (room.kind == Room::Kind::Open && match.mode() == Mode::Campaign) {
    std::string names;
    int kills = 0;
    for (const Member& member : room.members) {
      if (member.slot < 0) continue;
      kills += match.total_tally(member.slot).kills;
      if (!names.empty()) names += " & ";
      names += member.name;
    }
    rank = board_.add({names, match.levels_cleared(), kills, static_cast<std::int64_t>(clock_)});
  }
  JsonWriter w;
  w.begin_object();
  w.field("t", "over");
  w.field("mode", mode_name(match.mode()));
  w.field("outcome", outcome_name(match.outcome()));
  w.field("levels", match.levels_cleared());
  w.field("rank", rank);
  if (room.kind == Room::Kind::Daily) {
    w.key("daily").begin_object();
    w.field("day", room.day).field("scored", daily_place >= 0).field("place", daily_place);
    w.field("replay", daily_place >= 0 ? replay_id : std::string());
    w.end_object();
  }
  if (room.kind == Room::Kind::Replay) w.field("replayOf", room.playback_name);
  w.key("players").begin_array();
  for (const Member& member : room.members) {
    if (member.slot < 0) continue;
    const Tally tally = match.total_tally(member.slot);
    w.begin_object();
    w.field("slot", member.slot).field("name", member.name).field("team", team_index(member.team));
    w.field("bot", member.bot).field("shots", tally.shots).field("gateMobs", tally.gate_mobs);
    w.field("kills", tally.kills).field("baseDamage", tally.base_damage).field("giants", tally.giants);
    w.field("bombs", tally.bombs);
    w.end_object();
  }
  w.end_array();
  w.end_object();
  broadcast(room, w.str());
  if (rank >= 0) broadcast(room, board_.to_json());
  if (room.kind == Room::Kind::Daily) {
    for (const Member& member : room.members) {
      if (member.client != 0) send(member.client, daily_.to_json(room.day, member.name));
    }
  }
}

void Lobby::send_ghost(const Room& room) {
  if (!room.ghost) return;
  const Match& ghost = room.ghost->match();
  const World& world = ghost.world();
  const Base& enemy = world.base(Team::Red);
  JsonWriter w;
  w.begin_object();
  w.field("t", "ghost");
  w.field("name", room.ghost_name);
  w.field("level", ghost.level());
  w.field("cleared", ghost.levels_cleared());
  w.field("enemy", enemy.max_hp > 0 ? static_cast<double>(enemy.hp) / enemy.max_hp : 0.0);
  w.field("x", world.cannons().empty() ? kFieldWidth / 2.0 : world.cannons().front().x);
  w.field("over", room.ghost->done());
  w.end_object();
  broadcast(room, w.str());
}

void Lobby::start_daily(ClientId id, Client& client) {
  if (rooms_.size() >= kMaxRooms) return send_error(id, "The server is full; try again soon.");
  leave_room(id, client);
  daily_.prune(today());
  Room room;
  room.kind = Room::Kind::Daily;
  room.day = today();
  room.code = make_code();
  room.mode = Mode::Campaign;
  room.is_public = false;
  room.host = id;
  Member member;
  member.client = id;
  member.name = client.name;
  member.token = client.token;
  room.members.push_back(member);
  client.room = room.code;
  const std::string code = room.code;
  Room& stored = rooms_.emplace(code, std::move(room)).first->second;
  send_room(stored);
  send(id, daily_.to_json(stored.day, client.name));
  start_match(stored);
}

void Lobby::watch_replay(ClientId id, Client& client, std::string_view replay_id) {
  const Replay* replay = daily_.replay(replay_id);
  const DailyBoard::Run* run = daily_.run_with_replay(replay_id);
  if (replay == nullptr || run == nullptr) return send_error(id, "That replay is gone.");
  if (rooms_.size() >= kMaxRooms) return send_error(id, "The server is full; try again soon.");
  leave_room(id, client);
  Room room;
  room.kind = Room::Kind::Replay;
  room.code = make_code();
  room.mode = replay->mode;
  room.is_public = false;
  room.host = id;
  room.playback = *replay;
  room.playback_name = run->name;
  // The recorded players hold their seats; the watcher only watches.
  for (const SeatSpec& seat : replay->seats) {
    Member player;
    player.name = seat.bot ? std::string(kBotNames.at(0)) : run->name;
    player.bot = seat.bot;
    player.team = seat.team;
    player.slot = seat.slot;
    player.seated = true;
    player.connected = true;
    room.members.push_back(player);
  }
  Member watcher;
  watcher.client = id;
  watcher.name = client.name;
  watcher.token = client.token;
  watcher.seated = false;
  room.members.push_back(watcher);
  client.room = room.code;
  const std::string code = room.code;
  Room& stored = rooms_.emplace(code, std::move(room)).first->second;
  stored.match = std::make_unique<Match>(replay->mode, replay->seed, replay->seats);
  ++matches_started_;
  stored.tick = 0;
  stored.sent_serial = stored.match->level_serial();
  stored.sent_phase = stored.match->phase();
  send_room(stored);
  broadcast(stored, encode_level(*stored.match));
}

void Lobby::tick_room(Room& room, double dt) {
  if (humans_connected(room) == 0) {
    room.empty_for += dt;
  } else {
    room.empty_for = 0.0;
  }
  for (Member& member : room.members) {
    if (!member.bot && member.client == 0) {
      member.gone_for += dt;
      // Past the grace period nobody can reclaim the seat.
      if (member.gone_for > kReconnectGraceSeconds) member.token.clear();
    }
  }
  if (!room.match) return;
  Match& match = *room.match;
  if (room.playback) {
    const auto& inputs = room.playback->inputs;
    while (room.playback_next < inputs.size() && inputs[room.playback_next].tick <= match.ticks()) {
      apply_input(match, inputs[room.playback_next]);
      ++room.playback_next;
    }
  }
  match.step(dt);
  ++room.tick;
  if (room.ghost && !room.ghost->done()) room.ghost->step();
  if (room.ghost && room.tick % kGhostEveryTicks == 0) send_ghost(room);

  if (match.level_serial() != room.sent_serial) {
    room.sent_serial = match.level_serial();
    broadcast(room, encode_level(match));
  }
  if (match.phase() != room.sent_phase) {
    room.sent_phase = match.phase();
    send_room(room);
    if (match.phase() == Phase::Upgrade) {
      room.sent_picks.clear();
      for (const Member& member : room.members) {
        if (member.client == 0 || member.slot < 0) continue;
        if (auto cards = encode_cards(match, member.slot)) send(member.client, *cards);
      }
    }
    if (match.phase() == Phase::GameOver) finish_match(room);
  }
  if (match.phase() == Phase::Upgrade) send_picks(room);

  const int every = match.phase() == Phase::GameOver ? 15 : kSnapshotEveryTicks;
  if (room.tick % every == 0) broadcast_binary(room, encode_snapshot(match, match.take_events()));
}

void Lobby::tick(double dt) {
  for (auto& [id, client] : clients_) {
    client.budget = std::min(kMessageBudget, client.budget + kMessageRefillPerSecond * dt);
  }
  for (auto& [code, room] : rooms_) tick_room(room, dt);
  std::erase_if(rooms_, [](const auto& entry) { return entry.second.empty_for > kReconnectGraceSeconds; });
}

}  // namespace mob_survivor::net
