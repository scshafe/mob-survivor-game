#include <cstdio>
#include <filesystem>
#include <string>

#include "check.hpp"
#include "mob_survivor/match.hpp"
#include "net/base64.hpp"
#include "net/http.hpp"
#include "net/json.hpp"
#include "net/leaderboard.hpp"
#include "net/protocol.hpp"
#include "net/sha1.hpp"
#include "net/websocket.hpp"

using namespace mob_survivor;
using namespace mob_survivor::net;

namespace {

std::string hex(const std::array<std::uint8_t, 20>& digest) {
  std::string out;
  char buffer[3];
  for (const auto byte : digest) {
    std::snprintf(buffer, sizeof buffer, "%02x", byte);
    out += buffer;
  }
  return out;
}

void sha1_matches_known_vectors() {
  CHECK(hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

void base64_matches_known_vectors() {
  CHECK(base64_encode("") == "");
  CHECK(base64_encode("f") == "Zg==");
  CHECK(base64_encode("fo") == "Zm8=");
  CHECK(base64_encode("foo") == "Zm9v");
  CHECK(base64_encode("foobar") == "Zm9vYmFy");
}

void the_handshake_matches_rfc_6455() {
  CHECK(websocket_accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  const std::string raw =
      "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
      "Tailscale-User-Name: Ada Lovelace\r\n\r\nextra";
  HttpRequest request;
  std::size_t consumed = 0;
  CHECK(parse_request(raw, request, consumed) == ParseStatus::Done);
  CHECK(consumed == raw.size() - 5);
  CHECK(request.method == "GET" && request.path() == "/ws");
  CHECK(is_websocket_upgrade(request));
  CHECK(request.header("tailscale-user-name").value_or("") == "Ada Lovelace");
  CHECK(parse_request("GET / HTTP/1.1\r\nHost: x\r\n", request, consumed) == ParseStatus::Incomplete);
  CHECK(parse_request("NONSENSE\r\n\r\n", request, consumed) == ParseStatus::Error);
  CHECK(parse_request(std::string(kMaxRequestHead + 10, 'a'), request, consumed) == ParseStatus::Error);
}

void static_paths_stay_inside_the_web_root() {
  CHECK(static_file_path("/") == "index.html");
  CHECK(static_file_path("/js/main.js") == "js/main.js");
  CHECK(!static_file_path("/../etc/passwd"));
  CHECK(!static_file_path("/js/../../x"));
  CHECK(!static_file_path("/.git/config"));
  CHECK(!static_file_path("/a//b"));
  CHECK(!static_file_path("/a%2e%2e/b"));
  CHECK(!static_file_path("/a\\b"));
  CHECK(mime_type("x.js").starts_with("text/javascript"));
}

void frames_round_trip() {
  FrameDecoder decoder;
  const std::array<std::uint8_t, 4> mask{1, 2, 3, 4};
  std::string wire = encode_frame(Opcode::Text, "hello", mask);
  const std::string big(70000, 'z');
  FrameDecoder big_decoder(100000);
  std::string big_wire = encode_frame(Opcode::Binary, big, mask);
  WsMessage message;
  // Byte by byte: nothing until the last byte arrives.
  std::string buffer;
  for (std::size_t i = 0; i + 1 < wire.size(); ++i) {
    buffer.push_back(wire[i]);
    CHECK(decoder.next(buffer, message) == FrameDecoder::Result::NeedMore);
  }
  buffer.push_back(wire.back());
  CHECK(decoder.next(buffer, message) == FrameDecoder::Result::Message);
  CHECK(message.opcode == Opcode::Text && message.payload == "hello");
  CHECK(buffer.empty());
  CHECK(big_decoder.next(big_wire, message) == FrameDecoder::Result::Message);
  CHECK(message.payload == big);

  // Fragments with a ping in the middle.
  std::string fragmented;
  std::string first = encode_frame(Opcode::Text, "ab", mask);
  first[0] = static_cast<char>(0x01);  // FIN clear
  std::string ping = encode_frame(Opcode::Ping, "p", mask);
  std::string last = encode_frame(Opcode::Continuation, "cd", mask);
  fragmented = first + ping + last;
  FrameDecoder joiner;
  CHECK(joiner.next(fragmented, message) == FrameDecoder::Result::Message);
  CHECK(message.opcode == Opcode::Ping && message.payload == "p");
  CHECK(joiner.next(fragmented, message) == FrameDecoder::Result::Message);
  CHECK(message.opcode == Opcode::Text && message.payload == "abcd");

  // Servers refuse unmasked client frames and oversized messages.
  std::string unmasked = encode_frame(Opcode::Text, "x");
  FrameDecoder strict;
  CHECK(strict.next(unmasked, message) == FrameDecoder::Result::Error);
  std::string huge = encode_frame(Opcode::Text, std::string(2000, 'x'), mask);
  FrameDecoder small(1000);
  CHECK(small.next(huge, message) == FrameDecoder::Result::Error);

  // Server frames: 126 and 127 length forms.
  CHECK(encode_frame(Opcode::Binary, std::string(300, 'a')).size() == 300 + 4);
  CHECK(encode_frame(Opcode::Binary, std::string(70000, 'a')).size() == 70000 + 10);
}

void json_parses_and_writes() {
  const auto value = parse_json(R"({"t":"in","x":12.5,"f":true,"name":"Aé\"b","list":[1,2,{}],"n":null})");
  CHECK(value.has_value());
  CHECK(value->get_string("t") == "in");
  CHECK(value->get_number("x") == 12.5);
  CHECK(value->get_bool("f"));
  CHECK(value->get_string("name") == "A\xc3\xa9\"b");
  CHECK(value->get_number("missing", -1) == -1);
  CHECK(value->find("n") != nullptr && value->find("n")->is_null());
  CHECK(!parse_json("{\"a\":1,}"));
  CHECK(!parse_json("{\"a\":1} x"));
  CHECK(!parse_json("[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]"));
  CHECK(!parse_json("{\"a\":\"\x01\"}"));
  CHECK(!parse_json("1e999"));

  JsonWriter w;
  w.begin_object().field("t", "x").field("n", 3).field("d", 2.5).field("s", "a\"b\n");
  w.key("list").begin_array().value(1).value(true).null().end_array().end_object();
  CHECK(w.str() == R"({"t":"x","n":3,"d":2.5,"s":"a\"b\n","list":[1,true,null]})");
  CHECK(parse_json(w.str()).has_value());
}

void snapshots_have_the_documented_size() {
  Match match(Mode::Campaign, 11, {{0, Team::Blue, false}});
  for (int i = 0; i < 30 * 3 + 5; ++i) match.step(kTickSeconds);
  match.set_input(0, 12.0, true);
  for (int i = 0; i < 30 * 8; ++i) match.step(kTickSeconds);
  const auto events = match.take_events();
  const std::string bytes = encode_snapshot(match, events);
  const World& world = match.world();
  const std::size_t expected = 18 + 16 + 12 + 1 + world.cannons().size() * 9 + 1 + world.level().gates.size() * 4 + 1 +
                               world.level().saws.size() * 2 + 1 + world.bombs().size() * 12 + 1 +
                               world.powerups().size() * 13 + 2 + events.size() * 12 +
                               2 + world.mobs().size() * 11;
  CHECK(bytes.size() == expected);
  CHECK(static_cast<std::uint8_t>(bytes[0]) == kSnapshotKind);
  CHECK(static_cast<std::uint8_t>(bytes[1]) == static_cast<std::uint8_t>(Phase::Playing));
  CHECK(!world.mobs().empty());
  CHECK(!world.powerups().empty());

  const auto level = parse_json(encode_level(match));
  CHECK(level.has_value() && level->get_string("t") == "level" && level->get_number("level") == 1);
  CHECK(level->get_string("layout") == "open");
  CHECK(!encode_cards(match, 0));  // not the upgrade phase
}

void the_leaderboard_ranks_and_persists() {
  const auto path = std::filesystem::temp_directory_path() / "mob_survivor_board_test.tsv";
  std::filesystem::remove(path);
  {
    Leaderboard board(path.string());
    CHECK(board.add({"Ann", 3, 100, 1}) == 0);
    CHECK(board.add({"Bo\tB", 5, 10, 2}) == 0);
    CHECK(board.add({"Cy", 3, 200, 3}) == 1);
    for (int i = 0; i < 10; ++i) board.add({"Filler", 4, i, 10 + i});
    CHECK(board.entries().size() == Leaderboard::kSize);
    CHECK(board.add({"Low", 0, 0, 99}) == -1);
  }
  Leaderboard reloaded(path.string());
  CHECK(reloaded.entries().size() == Leaderboard::kSize);
  CHECK(reloaded.entries().front().names == "Bo B");
  CHECK(reloaded.entries().front().levels == 5);
  CHECK(parse_json(reloaded.to_json()).has_value());
  std::filesystem::remove(path);
}

}  // namespace

int main() {
  sha1_matches_known_vectors();
  base64_matches_known_vectors();
  the_handshake_matches_rfc_6455();
  static_paths_stay_inside_the_web_root();
  frames_round_trip();
  json_parses_and_writes();
  snapshots_have_the_documented_size();
  the_leaderboard_ranks_and_persists();
  return check::finish("net_test");
}
