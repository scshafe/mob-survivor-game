#include <map>
#include <string>
#include <vector>

#include "check.hpp"
#include "net/json.hpp"
#include "net/leaderboard.hpp"
#include "net/lobby.hpp"

using namespace mob_survivor;
using namespace mob_survivor::net;

namespace {

// Records everything the lobby sends, per client.
class Recorder final : public Sink {
 public:
  void send_text(ClientId client, std::string_view text) override { texts[client].emplace_back(text); }
  void send_binary(ClientId client, std::string_view bytes) override { binaries[client].emplace_back(bytes); }

  // The last text message of type `t` sent to `client`, parsed.
  std::optional<JsonValue> last(ClientId client, std::string_view t) const {
    const auto found = texts.find(client);
    if (found == texts.end()) return std::nullopt;
    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) {
      auto value = parse_json(*it);
      if (value && value->get_string("t") == t) return value;
    }
    return std::nullopt;
  }

  std::size_t count(ClientId client, std::string_view t) const {
    std::size_t n = 0;
    const auto found = texts.find(client);
    if (found == texts.end()) return 0;
    for (const auto& text : found->second) {
      auto value = parse_json(text);
      if (value && value->get_string("t") == t) ++n;
    }
    return n;
  }

  std::map<ClientId, std::vector<std::string>> texts;
  std::map<ClientId, std::vector<std::string>> binaries;
};

void run(Lobby& lobby, double seconds) {
  for (int i = 0; i < static_cast<int>(seconds * 30); ++i) lobby.tick(kTickSeconds);
}

std::string code_of(const Recorder& recorder, ClientId client) {
  const auto room = recorder.last(client, "room");
  return room ? std::string(room->get_string("code")) : std::string();
}

void a_client_is_welcomed() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 1);
  lobby.connect(1, "Ada Lovelace");
  const auto welcome = out.last(1, "welcome");
  CHECK(welcome && welcome->get_string("name") == "Ada Lovelace" && welcome->get_string("token").size() == 32);
  CHECK(out.last(1, "board").has_value());
  CHECK(out.last(1, "rooms").has_value());
  lobby.message(1, R"({"t":"hello","name":"  \u0007Zed the Destroyer of Worlds  "})");
  CHECK(out.last(1, "session")->get_string("name") == "Zed the Destroye");
  lobby.message(1, "not json");
  CHECK(out.last(1, "error").has_value());
}

void solo_play_starts_at_once_and_streams_snapshots() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 2);
  lobby.connect(1, "Solo");
  lobby.message(1, R"({"t":"create","mode":"campaign","public":false,"start":true})");
  const auto room = out.last(1, "room");
  CHECK(room && room->get_string("phase") == "countdown" && !room->get_bool("public", true));
  CHECK(out.last(1, "level").has_value());
  run(lobby, 1.0);
  CHECK(out.binaries[1].size() == 15);
  // Private rooms are not listed.
  lobby.connect(2, "Other");
  lobby.message(2, R"({"t":"rooms"})");
  CHECK(out.texts[2].back() == R"({"t":"rooms","rooms":[]})");
  CHECK(lobby.room_count() == 1);
}

void friends_join_by_code_and_the_host_starts() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 3);
  lobby.connect(1, "Host");
  lobby.connect(2, "Friend");
  lobby.message(1, R"({"t":"create","mode":"campaign"})");
  const std::string code = code_of(out, 1);
  CHECK(code.size() == 4);
  std::string lower = code;
  for (char& c : lower) c = static_cast<char>(c + ('a' - 'A'));
  lobby.message(2, R"({"t":"join","code":")" + lower + "\"}");
  CHECK(code_of(out, 2) == code);
  // Only the host may start.
  lobby.message(2, R"({"t":"start"})");
  CHECK(out.last(2, "room")->get_string("phase") == "lobby");
  lobby.message(1, R"({"t":"start"})");
  CHECK(out.last(2, "room")->get_string("phase") == "countdown");
  CHECK(out.last(2, "level").has_value());
  lobby.message(1, R"({"t":"emote","e":2})");
  CHECK(out.last(2, "emote")->get_number("e") == 2);
  // A late joiner watches.
  lobby.connect(3, "Late");
  lobby.message(3, R"({"t":"join","code":")" + code + "\"}");
  CHECK(out.last(3, "level").has_value());
  run(lobby, 0.2);
  CHECK(!out.binaries[3].empty());
}

void versus_fills_empty_teams_with_bots() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 4);
  lobby.connect(1, "Blue");
  lobby.message(1, R"({"t":"create","mode":"versus"})");
  lobby.message(1, R"({"t":"start"})");
  const auto room = out.last(1, "room");
  CHECK(room && room->get_string("phase") == "countdown");
  run(lobby, 300.0);
  const auto over = out.last(1, "over");
  CHECK(over.has_value());
  CHECK(over && over->get_string("mode") == "versus" && over->get_string("outcome") != "none");
  lobby.message(1, R"({"t":"again"})");
  CHECK(out.last(1, "room")->get_string("phase") == "lobby");
}

void teams_hold_at_most_two() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 5);
  lobby.connect(1, "A");
  lobby.message(1, R"({"t":"create","mode":"versus"})");
  const std::string code = code_of(out, 1);
  for (ClientId id = 2; id <= 5; ++id) {
    lobby.connect(id, "P");
    lobby.message(id, R"({"t":"join","code":")" + code + "\"}");
  }
  int blue = 0;
  int red = 0;
  int spectators = 0;
  // JsonValue does not expose arrays, so count the members in the raw text.
  CHECK(out.last(5, "room").has_value());
  const std::string& text = out.texts[5].back();
  for (std::size_t at = 0; (at = text.find("\"team\":", at)) != std::string::npos; ++at) {
    const bool seated = text.find("\"seated\":true", at) < text.find('}', at);
    if (!seated) {
      ++spectators;
    } else if (text[at + 7] == '0') {
      ++blue;
    } else {
      ++red;
    }
  }
  CHECK(blue == 2 && red == 2 && spectators == 1);
  lobby.message(5, R"({"t":"team","team":1})");
  CHECK(out.last(5, "error").has_value());
}

void a_dropped_player_can_rejoin_with_their_token() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 6);
  lobby.connect(1, "Host");
  lobby.connect(2, "Wobbly");
  lobby.message(1, R"({"t":"create","mode":"campaign"})");
  const std::string code = code_of(out, 1);
  lobby.message(2, R"({"t":"join","code":")" + code + "\"}");
  lobby.message(1, R"({"t":"start"})");
  const std::string token(out.last(2, "welcome")->get_string("token"));
  lobby.disconnect(2);
  run(lobby, 1.0);
  lobby.connect(7, "Wobbly again");
  lobby.message(7, R"({"t":"hello","name":"Wobbly","token":")" + token + "\"}");
  const auto session = out.last(7, "session");
  CHECK(session && session->get_string("rejoined") == code);
  CHECK(out.last(7, "level").has_value());
  CHECK(code_of(out, 7) == code);
  // A made-up token rejoins nothing.
  lobby.connect(8, "Mallory");
  lobby.message(8, R"({"t":"hello","name":"M","token":"0123"})");
  CHECK(out.last(8, "session")->get_string("rejoined").empty());
}

void the_host_role_moves_on_and_empty_rooms_close() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 7);
  lobby.connect(1, "First");
  lobby.connect(2, "Second");
  lobby.message(1, R"({"t":"create","mode":"campaign"})");
  const std::string code = code_of(out, 1);
  lobby.message(2, R"({"t":"join","code":")" + code + "\"}");
  lobby.message(1, R"({"t":"leave"})");
  CHECK(out.last(2, "room")->get_number("host") == 2);
  lobby.disconnect(2);
  CHECK(lobby.room_count() == 0);
}

void a_lost_campaign_reaches_the_leaderboard() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 8);
  lobby.set_clock(1700000000.0);
  lobby.connect(1, "Idle");
  lobby.message(1, R"({"t":"create","mode":"campaign","start":true})");
  run(lobby, 90.0);
  const auto over = out.last(1, "over");
  CHECK(over && over->get_string("outcome") == "red" && over->get_number("rank") == 0);
  CHECK(board.entries().size() == 1 && board.entries()[0].names == "Idle");
}

void floods_are_dropped() {
  Recorder out;
  Leaderboard board;
  Lobby lobby(out, board, 9);
  lobby.connect(1, "Spam");
  for (int i = 0; i < 500; ++i) lobby.message(1, R"({"t":"ping","n":1})");
  CHECK(out.count(1, "pong") == 120);
  run(lobby, 1.0);
  lobby.message(1, R"({"t":"ping","n":2})");
  CHECK(out.last(1, "pong")->get_number("n") == 2);
}

}  // namespace

int main() {
  a_client_is_welcomed();
  solo_play_starts_at_once_and_streams_snapshots();
  friends_join_by_code_and_the_host_starts();
  versus_fills_empty_teams_with_bots();
  teams_hold_at_most_two();
  a_dropped_player_can_rejoin_with_their_token();
  the_host_role_moves_on_and_empty_rooms_close();
  a_lost_campaign_reaches_the_leaderboard();
  floods_are_dropped();
  return check::finish("lobby_test");
}
