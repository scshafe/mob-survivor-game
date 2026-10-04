#include <algorithm>

#include "check.hpp"
#include "mob_survivor/level.hpp"
#include "mob_survivor/world.hpp"

using namespace mob_survivor;

namespace {

// An empty arena: no gates, no saws, no AI, one Blue cannon in slot 0.
WorldConfig bare(std::vector<Gate> gates = {}) {
  WorldConfig config;
  config.level.gates = std::move(gates);
  config.level.base_hp = {100, 100};
  config.cannons.push_back({0, Team::Blue, {}});
  config.seed = 7;
  return config;
}

Mob grunt(Team team, double x, double y, int hp = 1) {
  Mob mob;
  mob.team = team;
  mob.position = {x, y};
  mob.hp = hp;
  return mob;
}

void mobs_march_toward_the_enemy() {
  World world(bare());
  world.spawn(grunt(Team::Blue, 5.0, 10.0));
  world.spawn(grunt(Team::Red, 19.0, 30.0));
  world.step(1.0);
  CHECK_NEAR(world.mobs()[0].position.y, 10.0 + mob_speed(MobKind::Grunt), 1e-9);
  CHECK_NEAR(world.mobs()[1].position.y, 30.0 - mob_speed(MobKind::Grunt), 1e-9);
}

void a_mob_reaching_the_far_end_hits_the_base() {
  World world(bare());
  world.spawn(grunt(Team::Blue, 12.0, kFieldLength - kBaseDepth - 0.1, 4));
  world.step(0.1);
  CHECK(world.mobs().empty());
  CHECK(world.base(Team::Red).hp == 96);
  const auto events = world.take_events();
  CHECK(std::any_of(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::BaseHit && e.value == 4; }));
}

void enemies_trade_hit_points_on_contact() {
  World world(bare());
  world.spawn(grunt(Team::Blue, 12.0, 20.0, 5));
  world.spawn(grunt(Team::Red, 12.0, 20.2, 3));
  world.step(kTickSeconds);
  CHECK(world.mobs().size() == 1);
  CHECK(world.mobs()[0].team == Team::Blue);
  CHECK(world.mobs()[0].hp == 2);
}

void a_multiply_gate_multiplies_once() {
  Gate gate;
  gate.x = 12.0;
  gate.y = 10.0;
  gate.width = 4.0;
  gate.op = GateOp::Mul;
  gate.value = 3;
  World world(bare({gate}));
  world.spawn(grunt(Team::Blue, 12.0, 9.95));
  for (int i = 0; i < 30; ++i) world.step(kTickSeconds);
  int total = 0;
  for (const Mob& mob : world.mobs()) total += mob.hp;
  CHECK(total == 3);
  // The clones inherited the gate, so none multiplied again.
  for (const Mob& mob : world.mobs()) CHECK((mob.gates_passed & 1U) != 0);
}

void an_add_gate_adds_and_misses_outside_its_span() {
  Gate gate;
  gate.x = 6.0;
  gate.y = 10.0;
  gate.width = 4.0;
  gate.op = GateOp::Add;
  gate.value = 5;
  World world(bare({gate}));
  world.spawn(grunt(Team::Blue, 6.5, 9.95));   // inside the span
  world.spawn(grunt(Team::Blue, 18.0, 9.95));  // far outside it
  world.step(kTickSeconds);
  int total = 0;
  for (const Mob& mob : world.mobs()) total += mob.hp;
  CHECK(total == 2 + 5);
}

void gates_ignore_teams_they_do_not_serve() {
  Gate gate;
  gate.x = 12.0;
  gate.y = 10.0;
  gate.width = 6.0;
  gate.op = GateOp::Mul;
  gate.value = 4;
  gate.teams = team_bit(Team::Blue);
  World world(bare({gate}));
  world.spawn(grunt(Team::Red, 12.0, 10.05));
  world.step(kTickSeconds);
  CHECK(world.mobs().size() == 1);
}

void the_cannon_fires_and_charges_a_giant() {
  World world(bare());
  world.set_input(0, 6.0, true);
  for (int i = 0; i < 30 * 12; ++i) world.step(kTickSeconds);
  const Cannon* cannon = world.cannon(0);
  CHECK(cannon != nullptr);
  CHECK_NEAR(cannon->x, 6.0, 1e-9);
  CHECK(cannon->charge >= 1.0);
  CHECK(world.tally(0).shots > 50);
  world.request_giant(0);
  world.step(kTickSeconds);
  const bool giant = std::any_of(world.mobs().begin(), world.mobs().end(),
                                 [](const Mob& mob) { return mob.kind == MobKind::Giant; });
  CHECK(giant);
  CHECK(world.cannon(0)->charge < 0.1);
}

void a_bomb_clears_enemies_in_its_radius() {
  World world(bare());
  for (int i = 0; i < 5; ++i) world.spawn(grunt(Team::Red, 10.0 + i * 0.4, 30.0));
  world.spawn(grunt(Team::Red, 20.0, 30.0));
  world.request_bomb(0, {11.0, 30.0 - mob_speed(MobKind::Grunt) * kBombFuse});
  for (int i = 0; i < 30; ++i) world.step(kTickSeconds);
  CHECK(world.team_mob_count(Team::Red) == 1);
  CHECK(world.cannon(0)->bomb_cooldown > 0.0);
  // On cooldown: a second request does nothing.
  world.request_bomb(0, {20.0, 26.0});
  world.step(kTickSeconds);
  CHECK(world.bombs().empty());
}

void a_disconnected_cannon_stops_firing() {
  World world(bare());
  world.set_input(0, 12.0, true);
  world.set_connected(0, false);
  for (int i = 0; i < 30; ++i) world.step(kTickSeconds);
  CHECK(world.mobs().empty());
}

void the_ai_sends_waves() {
  WorldConfig config;
  config.level = make_campaign_level(1, 1, 3);
  config.cannons.push_back({0, Team::Blue, {}});
  World world(config);
  for (int i = 0; i < 30 * 5; ++i) world.step(kTickSeconds);
  CHECK(world.team_mob_count(Team::Red) > 0);
}

void the_mob_cap_holds() {
  // Two full-width x4 rows turn every shot into 16 mobs: far past the cap.
  std::vector<Gate> gates(2);
  for (std::size_t g = 0; g < gates.size(); ++g) {
    gates[g].x = kFieldWidth / 2.0;
    gates[g].y = 8.0 + 4.0 * static_cast<double>(g);
    gates[g].width = kFieldWidth;
    gates[g].op = GateOp::Mul;
    gates[g].value = 4;
  }
  World world(bare(gates));
  world.set_input(0, 12.0, true);
  std::size_t most = 0;
  for (int i = 0; i < 30 * 20; ++i) {
    world.step(kTickSeconds);
    most = std::max(most, world.team_mob_count(Team::Blue));
  }
  CHECK(most <= kMaxMobsPerTeam);
  CHECK(most > kMaxMobsPerTeam / 2);
}

void a_destroyed_base_ends_the_level() {
  World world(bare());
  world.spawn(grunt(Team::Blue, 12.0, kFieldLength - kBaseDepth - 0.05, 150));
  world.step(kTickSeconds);
  CHECK(world.outcome() == Outcome::BlueWins);
  const auto ticks = world.ticks();
  const auto elapsed = world.elapsed();
  world.step(kTickSeconds);
  CHECK(world.ticks() == ticks + 1);
  CHECK(world.elapsed() == elapsed);  // frozen once decided
}

void stepping_is_deterministic() {
  auto run = [] {
    WorldConfig config;
    config.level = make_campaign_level(4, 2, 99);
    config.cannons.push_back({0, Team::Blue, {}});
    config.cannons.push_back({1, Team::Blue, {}});
    config.seed = 1234;
    World world(config);
    for (int i = 0; i < 30 * 20; ++i) {
      world.set_input(0, 6.0 + (i % 90) / 10.0, true);
      world.set_input(1, 18.0 - (i % 60) / 10.0, i % 120 < 100);
      if (i == 300) world.request_bomb(0, {12.0, 25.0});
      world.step(kTickSeconds);
    }
    return world;
  };
  const World a = run();
  const World b = run();
  CHECK(a.mobs().size() == b.mobs().size());
  bool same = a.mobs().size() == b.mobs().size();
  for (std::size_t i = 0; same && i < a.mobs().size(); ++i) {
    same = a.mobs()[i].id == b.mobs()[i].id && a.mobs()[i].position.x == b.mobs()[i].position.x &&
           a.mobs()[i].position.y == b.mobs()[i].position.y && a.mobs()[i].hp == b.mobs()[i].hp;
  }
  CHECK(same);
  CHECK(a.base(Team::Red).hp == b.base(Team::Red).hp);
  CHECK(a.ticks() == 600);
}

void levels_are_reproducible_and_sane() {
  for (int level = 1; level <= 25; ++level) {
    const LevelSpec a = make_campaign_level(level, 2, 42);
    const LevelSpec b = make_campaign_level(level, 2, 42);
    CHECK(a.gates.size() == b.gates.size());
    CHECK(!a.gates.empty());
    CHECK(a.gates.size() <= kMaxGates);
    for (std::size_t g = 0; g < a.gates.size(); ++g) {
      CHECK(a.gates[g].x == b.gates[g].x);
      const double reach = a.gates[g].width / 2.0 + a.gates[g].amplitude;
      CHECK(a.gates[g].x - reach >= 0.0);
      CHECK(a.gates[g].x + reach <= kFieldWidth);
      CHECK(a.gates[g].value >= 1);
    }
    CHECK(a.base_hp[1] > a.base_hp[0]);
    CHECK(a.boss == (level % 5 == 0));
  }
}

void versus_arenas_are_point_symmetric() {
  const LevelSpec level = make_versus_level(5);
  CHECK(level.gates.size() % 2 == 0);
  for (std::size_t g = 0; g + 1 < level.gates.size(); g += 2) {
    const Gate& a = level.gates[g];
    const Gate& b = level.gates[g + 1];
    CHECK_NEAR(a.x + b.x, kFieldWidth, 1e-9);
    CHECK_NEAR(a.y + b.y, kFieldLength, 1e-9);
    CHECK(a.op == b.op && a.value == b.value);
    for (double t = 0.0; t < 20.0; t += 1.3) CHECK_NEAR(gate_x_at(a, t) + gate_x_at(b, t), kFieldWidth, 1e-9);
  }
}

}  // namespace

int main() {
  mobs_march_toward_the_enemy();
  a_mob_reaching_the_far_end_hits_the_base();
  enemies_trade_hit_points_on_contact();
  a_multiply_gate_multiplies_once();
  an_add_gate_adds_and_misses_outside_its_span();
  gates_ignore_teams_they_do_not_serve();
  the_cannon_fires_and_charges_a_giant();
  a_bomb_clears_enemies_in_its_radius();
  a_disconnected_cannon_stops_firing();
  the_ai_sends_waves();
  the_mob_cap_holds();
  a_destroyed_base_ends_the_level();
  stepping_is_deterministic();
  levels_are_reproducible_and_sane();
  versus_arenas_are_point_symmetric();
  return check::finish("world_test");
}
