#include "net/replay_file.hpp"

#include <sstream>

#include "net/protocol.hpp"

namespace mob_survivor::net {

std::string encode_replay(const Replay& replay) {
  std::ostringstream out;
  out << "mob-survivor-replay 1\n";
  out << "mode " << mode_name(replay.mode) << '\n';
  out << "seed " << replay.seed << '\n';
  for (const SeatSpec& seat : replay.seats) {
    out << "seat " << seat.slot << ' ' << team_index(seat.team) << ' ' << (seat.bot ? 1 : 0) << '\n';
  }
  for (const ReplayInput& input : replay.inputs) {
    out << "i " << input.tick << ' ' << static_cast<int>(input.kind) << ' ' << input.slot << ' ' << input.a << ' '
        << input.b << '\n';
  }
  return out.str();
}

std::optional<Replay> decode_replay(std::string_view text) {
  std::istringstream in{std::string(text)};
  std::string line;
  if (!std::getline(in, line) || line != "mob-survivor-replay 1") return std::nullopt;
  Replay replay;
  bool have_mode = false;
  bool have_seed = false;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream fields(line);
    std::string tag;
    fields >> tag;
    if (tag == "mode") {
      std::string name;
      fields >> name;
      const auto mode = parse_mode(name);
      if (!mode) return std::nullopt;
      replay.mode = *mode;
      have_mode = true;
    } else if (tag == "seed") {
      fields >> replay.seed;
      have_seed = !fields.fail();
    } else if (tag == "seat") {
      int slot = 0;
      int team = 0;
      int bot = 0;
      fields >> slot >> team >> bot;
      if (fields.fail() || slot < 0 || slot > 255 || team < 0 || team > 1) return std::nullopt;
      replay.seats.push_back({slot, static_cast<Team>(team), bot != 0});
    } else if (tag == "i") {
      ReplayInput input;
      std::uint32_t tick = 0;
      int kind = 0;
      fields >> tick >> kind >> input.slot >> input.a >> input.b;
      if (fields.fail() || kind < 0 || kind > static_cast<int>(ReplayInput::Kind::Connect)) return std::nullopt;
      if (!replay.inputs.empty() && tick < replay.inputs.back().tick) return std::nullopt;
      input.tick = tick;
      input.kind = static_cast<ReplayInput::Kind>(kind);
      replay.inputs.push_back(input);
    } else {
      return std::nullopt;
    }
  }
  if (!have_mode || !have_seed || replay.seats.empty()) return std::nullopt;
  return replay;
}

}  // namespace mob_survivor::net
