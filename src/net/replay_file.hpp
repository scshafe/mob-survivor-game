#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mob_survivor/replay.hpp"

namespace mob_survivor::net {

// A replay as text, one line a record, for the data directory:
//
//   mob-survivor-replay 1
//   mode campaign
//   seed 1234567890123
//   seat <slot> <team> <bot>
//   i <tick> <kind> <slot> <a> <b>
[[nodiscard]] std::string encode_replay(const Replay& replay);
// nullopt on anything malformed.
[[nodiscard]] std::optional<Replay> decode_replay(std::string_view text);

}  // namespace mob_survivor::net
