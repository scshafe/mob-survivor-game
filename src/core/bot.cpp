#include "mob_survivor/bot.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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
    case GateOp::Fuse:
      return 9.0;
    case GateOp::Armor:
      return 8.0;
    case GateOp::Runner:
      return 5.0;
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

// Where to aim so a grunt fired now meets a drifting power-up, if it can.
std::optional<double> powerup_aim(const World& world, const Cannon& cannon) {
  if (cannon.shots_per_volley >= kMaxShotsPerVolley) return std::nullopt;
  const double y = cannon_y(cannon.team);
  const double dir = team_direction(cannon.team);
  std::optional<double> best;
  double best_time = std::numeric_limits<double>::max();
  for (const PowerUp& powerup : world.powerups()) {
    const double time = (powerup.position.y - y) * dir / (mob_speed(MobKind::Grunt) * cannon.stats.speed_scale);
    const double x = powerup.position.x + powerup.velocity * time;
    if (time <= 0.0 || x < 1.0 || x > kFieldWidth - 1.0 || time >= best_time) continue;
    best_time = time;
    best = x;
  }
  return best;
}

// How many of this player's mobs are within a few units of running into a
// saw blade or a "/2" gate that would hurt them.
int mobs_near_hazards(const World& world, const Cannon& cannon) {
  const double dir = team_direction(cannon.team);
  int count = 0;
  for (const Mob& mob : world.mobs()) {
    if (mob.owner != cannon.slot || mob.hp <= 0) continue;
    bool near = false;
    for (std::size_t s = 0; s < world.level().saws.size() && !near; ++s) {
      const double ahead = (world.level().saws[s].y - mob.position.y) * dir;
      near = ahead > 0.0 && ahead < 4.0;
    }
    const auto& gates = world.level().gates;
    for (std::size_t g = 0; g < gates.size() && !near; ++g) {
      const Gate& gate = gates[g];
      if (gate.op != GateOp::Half || (gate.teams & team_bit(cannon.team)) == 0) continue;
      const double ahead = (gate.y - mob.position.y) * dir;
      near = ahead > 0.0 && ahead < 4.0 && std::fabs(mob.position.x - world.gate_x(g)) < gate.width / 2.0 + 0.5;
    }
    if (near) count += mob.hp;
  }
  return count;
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
    const std::optional<double> powerup = threat == nullptr ? powerup_aim(world, *cannon) : std::nullopt;
    if (threat != nullptr) {
      state.aim = threat->position.x;
    } else if (powerup) {
      state.aim = *powerup;
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
  command.phase = cannon->phase_time <= 0.0 && cannon->phase_cooldown <= 0.0 && mobs_near_hazards(world, *cannon) >= 12;

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
