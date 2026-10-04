#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mob_survivor {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

// A mob walks straight at the player at its own speed.
struct Mob {
  Vec2 position;
  double speed = 1.0;
};

// The whole simulation state. step() is deterministic: the same state and dt
// always give the same result, so tests and replays can rely on it.
class World {
 public:
  explicit World(Vec2 player_position = {});

  void spawn(Mob mob);
  void step(double dt_seconds);

  [[nodiscard]] const Vec2& player() const { return player_; }
  [[nodiscard]] const std::vector<Mob>& mobs() const { return mobs_; }
  [[nodiscard]] std::uint64_t ticks() const { return ticks_; }

 private:
  Vec2 player_;
  std::vector<Mob> mobs_;
  std::uint64_t ticks_ = 0;
};

}  // namespace mob_survivor
