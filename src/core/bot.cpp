#include "mob_survivor/bot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mob_survivor {

namespace {

double gate_score(const Gate& gate) {
  switch (gate.op) {
    case GateOp::Mul:
      return (gate.value - 1) * 10.0;
    case GateOp::Add:
      return gate.value * 1.0;
    case GateOp::Half:
      return -8.0;
  }
  return 0.0;
}

// The enemy mob nearest to this team's base, if any is within `range` of it.
const Mob* nearest_threat(const World& world, Team team, double range) {
  const double home = team == Team::Blue ? 0.0 : kFieldLength;
  const Mob* best = nullptr;
  double best_distance = range;
  for (const Mob& mob : world.mobs()) {
    if (mob.team == team || mob.hp <= 0) continue;
    const double distance = std::fabs(mob.position.y - home);
    if (distance < best_distance) {
      best_distance = distance;
      best = &mob;
    }
  }
  return best;
}

}  // namespace

BotCommand bot_think(const World& world, int slot, double skill, double dt, BotState& state, Rng& rng) {
  BotCommand command;
  const Cannon* cannon = world.cannon(slot);
  if (cannon == nullptr) return command;
  skill = std::clamp(skill, 0.0, 1.0);
  const Team team = cannon->team;
  const double y = cannon_y(team);
  const double dir = team_direction(team);

  state.think_timer -= dt;
  if (state.think_timer <= 0.0) {
    state.think_timer = rng.uniform(0.3, 0.6) + (1.0 - skill) * 0.4;
    const Mob* threat = nearest_threat(world, team, 9.0);
    if (threat != nullptr) {
      state.aim = threat->position.x;
    } else {
      // The nearest row ahead whose gates help this team.
      double row_distance = std::numeric_limits<double>::max();
      for (const Gate& gate : world.level().gates) {
        const double ahead = (gate.y - y) * dir;
        if (ahead > 0.0 && (gate.teams & team_bit(team)) != 0) row_distance = std::min(row_distance, ahead);
      }
      double best_score = -std::numeric_limits<double>::max();
      const auto& gates = world.level().gates;
      for (std::size_t g = 0; g < gates.size(); ++g) {
        const Gate& gate = gates[g];
        const double ahead = (gate.y - y) * dir;
        if (std::fabs(ahead - row_distance) > 0.5 || (gate.teams & team_bit(team)) == 0) continue;
        const double score = gate_score(gate);
        if (score > best_score) {
          best_score = score;
          // Lead a sliding gate by the time a grunt takes to get there.
          const double arrival = world.elapsed() + ahead / mob_speed(MobKind::Grunt);
          state.aim = gate_x_at(gate, arrival);
        }
      }
    }
    state.aim += rng.uniform(-1.0, 1.0) * (1.0 - skill) * 2.5;
  }
  command.target_x = std::clamp(state.aim, 0.5, kFieldWidth - 0.5);
  command.firing = true;
  command.giant = cannon->charge >= 1.0;

  if (cannon->bomb_cooldown <= 0.0) {
    if (const Mob* threat = nearest_threat(world, team, 14.0)) {
      int crowd = 0;
      for (const Mob& mob : world.mobs()) {
        if (mob.team == team) continue;
        const double dx = mob.position.x - threat->position.x;
        const double dy = mob.position.y - threat->position.y;
        if (dx * dx + dy * dy < kBombRadius * kBombRadius) crowd += mob.hp;
      }
      if (crowd >= 6 || threat->kind == MobKind::Giant || threat->kind == MobKind::Brute) {
        const double lead = -dir * mob_speed(threat->kind) * kBombFuse;
        command.bomb = Vec2{threat->position.x, threat->position.y + lead};
      }
    }
  }
  return command;
}

}  // namespace mob_survivor
