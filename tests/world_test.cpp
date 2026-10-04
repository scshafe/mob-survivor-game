#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "mob_survivor/world.hpp"

namespace {

int failures = 0;

void expect_near(double actual, double expected, const char* what) {
  if (std::fabs(actual - expected) > 1e-9) {
    std::fprintf(stderr, "FAIL %s: got %.12f, want %.12f\n", what, actual, expected);
    ++failures;
  }
}

void a_mob_walks_toward_the_player() {
  mob_survivor::World world({0.0, 0.0});
  world.spawn({{10.0, 0.0}, 2.0});
  world.step(1.0);
  expect_near(world.mobs()[0].position.x, 8.0, "x after one second");
  expect_near(world.mobs()[0].position.y, 0.0, "y unchanged");
}

void a_mob_never_overshoots() {
  mob_survivor::World world({0.0, 0.0});
  world.spawn({{1.0, 0.0}, 5.0});
  world.step(1.0);
  expect_near(world.mobs()[0].position.x, 0.0, "stops on the player");
}

void stepping_is_deterministic() {
  mob_survivor::World a({3.0, -2.0});
  mob_survivor::World b({3.0, -2.0});
  for (auto* world : {&a, &b}) {
    world->spawn({{-7.0, 4.0}, 1.5});
    for (int i = 0; i < 100; ++i) world->step(1.0 / 60.0);
  }
  expect_near(a.mobs()[0].position.x, b.mobs()[0].position.x, "same x");
  expect_near(a.mobs()[0].position.y, b.mobs()[0].position.y, "same y");
  if (a.ticks() != 100) {
    std::fprintf(stderr, "FAIL ticks: %llu\n", static_cast<unsigned long long>(a.ticks()));
    ++failures;
  }
}

}  // namespace

int main() {
  a_mob_walks_toward_the_player();
  a_mob_never_overshoots();
  stepping_is_deterministic();
  if (failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("world_test: ok");
  return EXIT_SUCCESS;
}
