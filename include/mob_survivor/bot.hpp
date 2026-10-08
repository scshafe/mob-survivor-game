#pragma once

#include <optional>

#include "mob_survivor/rng.hpp"
#include "mob_survivor/types.hpp"
#include "mob_survivor/world.hpp"

namespace mob_survivor {

struct BotState {
  double think_timer = 0.0;
  double aim = kFieldWidth / 2.0;
};

struct BotCommand {
  double target_x = kFieldWidth / 2.0;
  bool firing = true;
  bool giant = false;
  bool phase = false;
  std::optional<Vec2> bomb;
};

// A computer player for the cannon in `slot`: it aims at the best gate of the
// next row (or at whatever is about to reach its base), holds fire, launches
// giants when charged, bombs the nearest threat and phases a crowd that is
// about to meet a saw or a "/2" gate. `skill` in [0, 1] sets
// how well it aims. Deterministic: it reads only the world and `rng`.
[[nodiscard]] BotCommand bot_think(const World& world, int slot, double skill, double dt, BotState& state,
                                   Rng& rng);

}  // namespace mob_survivor
