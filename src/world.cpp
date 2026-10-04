#include "mob_survivor/world.hpp"

#include <algorithm>
#include <cmath>

namespace mob_survivor {

World::World(Vec2 player_position) : player_(player_position) {}

void World::spawn(Mob mob) { mobs_.push_back(mob); }

void World::step(double dt_seconds) {
  for (Mob& mob : mobs_) {
    const double dx = player_.x - mob.position.x;
    const double dy = player_.y - mob.position.y;
    const double distance = std::hypot(dx, dy);
    if (distance == 0.0) {
      continue;
    }
    // Never overshoot the player in one step.
    const double travel = std::min(mob.speed * dt_seconds, distance);
    mob.position.x += dx / distance * travel;
    mob.position.y += dy / distance * travel;
  }
  ++ticks_;
}

}  // namespace mob_survivor
