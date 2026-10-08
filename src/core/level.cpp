#include "mob_survivor/level.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "mob_survivor/rng.hpp"

namespace mob_survivor {

namespace {

constexpr double kGateMargin = 0.6;

int roll_mul(Rng& rng, int level) {
  const double roll = rng.unit();
  if (level >= 4 && roll < 0.06) return 4;
  if (roll < 0.32) return 3;
  return 2;
}

int roll_add(Rng& rng, int level) { return rng.range(2, 5) + std::min(level, 16) / 4; }

GateOp roll_op(Rng& rng, bool allow_half) {
  const double roll = rng.unit();
  if (allow_half && roll < 0.14) return GateOp::Half;
  return roll < 0.57 ? GateOp::Mul : GateOp::Add;
}

void fill_value(Gate& gate, Rng& rng, int level) {
  switch (gate.op) {
    case GateOp::Mul:
      gate.value = roll_mul(rng, level);
      break;
    case GateOp::Add:
      gate.value = roll_add(rng, level);
      break;
    case GateOp::Half:
      gate.value = 2;
      break;
  }
}

// One row of gates on the line y. A row is either one sliding gate, one wide
// gate, or two or three gates side by side; at most one of them is a "/2".
void add_row(std::vector<Gate>& gates, double y, int level, bool allow_moving, bool allow_half,
             std::uint8_t teams, Rng& rng) {
  const int count = level <= 1 ? 2 : rng.range(1, 3);
  if (count == 1) {
    Gate gate;
    gate.y = y;
    gate.teams = teams;
    gate.op = rng.chance(0.65) ? GateOp::Mul : GateOp::Add;
    fill_value(gate, rng, level);
    if (allow_moving && rng.chance(0.7)) {
      gate.width = rng.uniform(4.5, 6.0);
      gate.x = kFieldWidth / 2.0;
      gate.amplitude = kFieldWidth / 2.0 - gate.width / 2.0 - kGateMargin;
      gate.amplitude *= rng.uniform(0.6, 1.0);
      gate.speed = rng.uniform(0.55, 1.15);
      gate.phase = rng.uniform(0.0, 2.0 * std::numbers::pi);
    } else {
      gate.width = rng.uniform(6.0, 9.0);
      gate.x = rng.uniform(gate.width / 2.0 + kGateMargin, kFieldWidth - gate.width / 2.0 - kGateMargin);
    }
    gates.push_back(gate);
    return;
  }
  const double slot = kFieldWidth / count;
  bool half_used = !allow_half;
  for (int i = 0; i < count; ++i) {
    Gate gate;
    gate.y = y;
    gate.teams = teams;
    gate.width = std::clamp(slot - 1.2, 3.5, 7.0);
    gate.x = slot * (i + 0.5);
    gate.op = roll_op(rng, !half_used);
    if (gate.op == GateOp::Half) half_used = true;
    fill_value(gate, rng, level);
    gates.push_back(gate);
  }
  // Never a row of nothing but "/2": the last gate of a row stays positive.
  if (gates.back().op == GateOp::Half) {
    gates.back().op = GateOp::Mul;
    fill_value(gates.back(), rng, level);
  }
}

std::uint64_t mix(std::uint64_t seed, std::uint64_t salt) {
  Rng rng(seed ^ (salt * 0x9e3779b97f4a7c15ULL));
  return rng.next();
}

}  // namespace

double gate_x_at(const Gate& gate, double seconds) {
  if (gate.amplitude == 0.0) return gate.x;
  return gate.x + gate.amplitude * std::sin(gate.phase + gate.speed * seconds);
}

double saw_x_at(const Saw& saw, double seconds) {
  if (saw.amplitude == 0.0) return saw.x;
  return saw.x + saw.amplitude * std::sin(saw.phase + saw.speed * seconds);
}

LevelSpec make_campaign_level(int number, int players, std::uint64_t seed) {
  number = std::max(number, 1);
  players = std::clamp(players, 1, 4);
  Rng rng(mix(seed, static_cast<std::uint64_t>(number)));

  LevelSpec level;
  level.number = number;
  level.mode = Mode::Campaign;

  const int rows = 2 + std::min(2, (number - 1) / 2);
  const double first_row = 9.0;
  const double last_row = 29.5;
  const int shared_row = number >= 6 ? rng.range(0, rows - 1) : -1;
  for (int r = 0; r < rows; ++r) {
    const double y = first_row + (last_row - first_row) * r / (rows - 1);
    const std::uint8_t teams =
        r == shared_row ? static_cast<std::uint8_t>(team_bit(Team::Blue) | team_bit(Team::Red))
                        : team_bit(Team::Blue);
    add_row(level.gates, y, number, number >= 2, number >= 3 && r != shared_row, teams, rng);
  }

  const int saws = number >= 3 ? 1 + (number >= 7 ? 1 : 0) + (number >= 12 ? 1 : 0) : 0;
  for (int s = 0; s < saws; ++s) {
    Saw saw;
    // Between two gate rows, so the saw guards the payoff of the row before.
    const int gap = rng.range(0, rows - 2);
    const double a = first_row + (last_row - first_row) * gap / (rows - 1);
    const double b = first_row + (last_row - first_row) * (gap + 1) / (rows - 1);
    saw.y = (a + b) / 2.0 + rng.uniform(-0.8, 0.8);
    saw.radius = rng.uniform(0.8, 1.1);
    saw.x = kFieldWidth / 2.0;
    saw.amplitude = rng.uniform(6.0, kFieldWidth / 2.0 - 1.5);
    saw.speed = rng.uniform(0.7, 1.5);
    saw.phase = rng.uniform(0.0, 2.0 * std::numbers::pi);
    level.saws.push_back(saw);
  }

  PowerUpSpec& powerups = level.powerups;
  powerups.first = 7.0;
  powerups.interval = 16.0;
  powerups.hp = 10 + 2 * std::min(number, 10);
  powerups.y_min = first_row + 2.5;
  powerups.y_max = last_row - 2.5;

  const double hp_scale = 1.0 + 0.35 * (players - 1);
  const double size_scale = 1.0 + 0.3 * (players - 1);
  level.base_hp[team_index(Team::Blue)] = 100;
  level.base_hp[team_index(Team::Red)] =
      static_cast<int>(std::lround((600.0 + 150.0 * (number - 1)) * hp_scale));

  AiSpec& ai = level.ai;
  ai.enabled = true;
  ai.first_wave = 2.5;
  ai.wave_interval = std::max(1.6, 3.6 - 0.15 * (number - 1));
  ai.wave_size = static_cast<int>(std::lround((8.0 + 2.5 * number) * size_scale));
  ai.wave_growth = 0.5 * size_scale;
  ai.runner_percent = number >= 3 ? std::min(40, 8 * (number - 2)) : 0;
  ai.giant_every = number >= 2 ? std::max(3, 7 - number / 2) : 0;
  ai.giant_hp = 10 + 3 * number;
  if (number % 5 == 0) {
    level.boss = true;
    ai.first_boss = 6.0;
    ai.boss_interval = std::max(20.0, 40.0 - number);
    ai.boss_hp = static_cast<int>(std::lround((60.0 + 15.0 * number) * hp_scale));
  }
  return level;
}

LevelSpec make_versus_level(std::uint64_t seed) {
  Rng rng(mix(seed, 0xfeedULL));
  LevelSpec level;
  level.number = 1;
  level.mode = Mode::Versus;
  level.base_hp = {4000, 4000};
  level.time_limit = 240.0;
  level.frenzy_at = 150.0;
  // Near the centre line, as likely on either side, so neither team is favoured.
  level.powerups.first = 15.0;
  level.powerups.interval = 22.0;
  level.powerups.hp = 40;
  level.powerups.y_min = kFieldLength / 2.0 + 3.0;
  level.powerups.y_max = kFieldLength / 2.0 + 6.5;
  level.powerups.mirror = true;

  const auto both = static_cast<std::uint8_t>(team_bit(Team::Blue) | team_bit(Team::Red));
  std::vector<Gate> half;
  add_row(half, rng.uniform(9.5, 11.5), 3, true, true, both, rng);
  add_row(half, rng.uniform(15.0, 17.0), 3, true, true, both, rng);
  for (const Gate& gate : half) {
    level.gates.push_back(gate);
    // The point mirror through the field's centre: Red meets the same gates
    // in the same order, on its own side.
    Gate mirror = gate;
    mirror.x = kFieldWidth - gate.x;
    mirror.y = kFieldLength - gate.y;
    mirror.phase = gate.phase + std::numbers::pi;
    level.gates.push_back(mirror);
  }

  Saw saw;
  saw.y = kFieldLength / 2.0;
  saw.x = kFieldWidth / 2.0;
  saw.radius = 1.0;
  saw.amplitude = kFieldWidth / 2.0 - 2.0;
  saw.speed = 0.9;
  saw.phase = 0.0;
  level.saws.push_back(saw);
  return level;
}

}  // namespace mob_survivor
