#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mob_survivor/match.hpp"
#include "mob_survivor/world.hpp"

namespace mob_survivor::net {

// Bumped whenever a message changes shape; the client checks it on welcome.
inline constexpr int kProtocolVersion = 1;

// The first byte of every binary message.
inline constexpr std::uint8_t kSnapshotKind = 1;

[[nodiscard]] std::string_view mode_name(Mode mode);
[[nodiscard]] std::optional<Mode> parse_mode(std::string_view name);
[[nodiscard]] std::string_view phase_name(Phase phase);
[[nodiscard]] std::string_view outcome_name(Outcome outcome);
[[nodiscard]] std::string_view gate_op_name(GateOp op);

// The binary snapshot of a running match (layout in docs/protocol.md).
[[nodiscard]] std::string encode_snapshot(const Match& match, const std::vector<Event>& events);

// The static layout of the current level, sent once per level (JSON).
[[nodiscard]] std::string encode_level(const Match& match);

// One player's upgrade offer (JSON), or nullopt when the slot has none.
[[nodiscard]] std::optional<std::string> encode_cards(const Match& match, int slot);

}  // namespace mob_survivor::net
