#include <cstdio>

#include "mob_survivor/world.hpp"

// Placeholder entry point until the renderer exists: runs one second of a
// tiny simulation and prints where the mob ended up.
int main() {
  mob_survivor::World world({0.0, 0.0});
  world.spawn({{10.0, 0.0}, 2.0});
  for (int frame = 0; frame < 60; ++frame) {
    world.step(1.0 / 60.0);
  }
  const auto& mob = world.mobs().front();
  std::printf("mob-survivor-game: %llu ticks, mob at (%.2f, %.2f)\n",
              static_cast<unsigned long long>(world.ticks()), mob.position.x, mob.position.y);
  return 0;
}
