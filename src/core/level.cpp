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

GateOp roll_op(Rng& rng, bool allow_half, bool allow_special) {
  const double roll = rng.unit();
  if (allow_half && roll < 0.14) return GateOp::Half;
  if (allow_special && roll > 0.82) {
    const double which = rng.unit();
    return which < 0.34 ? GateOp::Fuse : (which < 0.67 ? GateOp::Runner : GateOp::Armor);
  }
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
    case GateOp::Fuse:
      gate.value = 10;
      break;
    case GateOp::Runner:
    case GateOp::Armor:
      gate.value = 1;  // unused
      break;
  }
}

// What a row of gates may hold.
struct RowRules {
  int level = 1;
  bool moving = false;   // a lone gate may slide
  bool half = false;     // one gate may be a "/2"
  bool special = false;  // one gate may be a fuse, runner or armor gate
  int count = 0;         // gates in the row; 0: one to three, at random
  std::uint8_t teams = team_bit(Team::Blue);
};

// One row of gates on the line y. A row is either one sliding gate, one wide
// gate, or two or three gates side by side; at most one of them is a "/2" and
// at most one changes what mobs are (fuse, runner, armor).
void add_row(std::vector<Gate>& gates, double y, const RowRules& rules, Rng& rng) {
  const int level = rules.level;
  const bool allow_moving = rules.moving;
  const bool allow_half = rules.half;
  const bool allow_special = rules.special;
  const std::uint8_t teams = rules.teams;
  const int count = rules.count > 0 ? rules.count : (level <= 1 ? 2 : rng.range(1, 3));
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
  bool special_used = !allow_special;
  for (int i = 0; i < count; ++i) {
    Gate gate;
    gate.y = y;
    gate.teams = teams;
    gate.width = std::clamp(slot - 1.2, 3.5, 7.0);
    gate.x = slot * (i + 0.5);
    gate.op = roll_op(rng, !half_used, !special_used);
    if (gate.op == GateOp::Half) half_used = true;
    if (gate.op == GateOp::Fuse || gate.op == GateOp::Runner || gate.op == GateOp::Armor) special_used = true;
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

// The middle of each gap between consecutive gate rows (`rows` sorted).
std::vector<double> gap_middles(const std::vector<double>& rows) {
  std::vector<double> middles;
  for (std::size_t r = 1; r < rows.size(); ++r) middles.push_back((rows[r - 1] + rows[r]) / 2.0);
  return middles;
}

// The gap middle nearest the centre line: where hourglass walls and the
// twin lanes' crossing go.
double centre_gap(const std::vector<double>& rows) {
  double best = kFieldLength / 2.0;
  double distance = kFieldLength;
  for (const double middle : gap_middles(rows)) {
    if (std::fabs(middle - kFieldLength / 2.0) < distance) {
      distance = std::fabs(middle - kFieldLength / 2.0);
      best = middle;
    }
  }
  return best;
}

constexpr double kPinchHalfHeight = 1.5;  // hourglass walls are 3 units deep
constexpr double kPinchGap = 8.0;         // and leave a gap this wide
constexpr double kLaneWall = 0.8;         // half the twin lanes' wall width
constexpr double kLaneCrossing = 1.2;     // half the gap where the lanes meet
constexpr double kBeltHalfHeight = 0.8;
constexpr double kPadInset = 3.5;

// Puts the layout's walls, belts and pads between the gate rows `rows`
// (sorted). With `mirror` (versus) everything is point-symmetric through the
// centre of the field, like the gates.
void lay_out(LevelSpec& level, const std::vector<double>& rows, bool mirror, Rng& rng) {
  const double centre = centre_gap(rows);
  const auto middles = gap_middles(rows);
  switch (level.layout) {
    case Layout::Open:
      break;
    case Layout::Hourglass: {
      const double edge = (kFieldWidth - kPinchGap) / 2.0;
      level.walls.push_back({0.0, edge, centre - kPinchHalfHeight, centre + kPinchHalfHeight});
      level.walls.push_back({kFieldWidth - edge, kFieldWidth, centre - kPinchHalfHeight, centre + kPinchHalfHeight});
      break;
    }
    case Layout::TwinLanes: {
      const double mid = kFieldWidth / 2.0;
      level.walls.push_back({mid - kLaneWall, mid + kLaneWall, rows.front() - 1.5, centre - kLaneCrossing});
      level.walls.push_back({mid - kLaneWall, mid + kLaneWall, centre + kLaneCrossing, rows.back() + 1.5});
      break;
    }
    case Layout::Conveyor: {
      const double push = rng.uniform(2.2, 3.0) * (rng.chance(0.5) ? 1.0 : -1.0);
      if (mirror) {
        const double y = middles.front();
        level.conveyors.push_back({y - kBeltHalfHeight, y + kBeltHalfHeight, push});
        level.conveyors.push_back({kFieldLength - y - kBeltHalfHeight, kFieldLength - y + kBeltHalfHeight, -push});
      } else {
        for (std::size_t g = 0; g < middles.size() && g < 3; ++g) {
          const double sign = g % 2 == 0 ? 1.0 : -1.0;
          level.conveyors.push_back({middles[g] - kBeltHalfHeight, middles[g] + kBeltHalfHeight, push * sign});
        }
      }
      break;
    }
    case Layout::Teleporters: {
      const double left = kPadInset;
      const double right = kFieldWidth - kPadInset;
      if (mirror) {
        const double y = middles.front();
        level.teleporters.push_back({{left, y}, {right, y}, 1.0});
        level.teleporters.push_back({{right, kFieldLength - y}, {left, kFieldLength - y}, 1.0});
      } else {
        level.teleporters.push_back({{left, middles.front()}, {right, middles.front()}, 1.0});
        if (middles.size() >= 2) level.teleporters.push_back({{left, middles.back()}, {right, middles.back()}, 1.0});
      }
      break;
    }
  }
}

// A saw on an hourglass's walls keeps to the gap between them.
void fit_saws(LevelSpec& level) {
  if (level.layout != Layout::Hourglass) return;
  for (Saw& saw : level.saws) {
    for (const Wall& wall : level.walls) {
      if (saw.y + saw.radius < wall.y0 || saw.y - saw.radius > wall.y1) continue;
      saw.x = kFieldWidth / 2.0;
      saw.amplitude = std::max(0.0, kPinchGap / 2.0 - saw.radius - 0.2);
    }
  }
}

Layout roll_layout(Rng& rng) { return static_cast<Layout>(rng.range(0, kLayoutCount - 1)); }

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

  // The first two levels are open; after that any layout, open included.
  level.layout = number >= 3 ? roll_layout(rng) : Layout::Open;
  const int rows = 2 + std::min(2, (number - 1) / 2);
  const double first_row = 9.0;
  const double last_row = 29.5;
  const int shared_row = number >= 6 ? rng.range(0, rows - 1) : -1;
  std::vector<double> row_ys;
  for (int r = 0; r < rows; ++r) {
    const double y = first_row + (last_row - first_row) * r / (rows - 1);
    row_ys.push_back(y);
    RowRules rules;
    rules.level = number;
    rules.moving = number >= 2 && level.layout != Layout::TwinLanes;
    rules.half = number >= 3 && r != shared_row;
    rules.special = number >= 2;
    // Twin lanes: one gate a lane, so none straddles the centre wall.
    rules.count = level.layout == Layout::TwinLanes ? 2 : 0;
    rules.teams = r == shared_row ? static_cast<std::uint8_t>(team_bit(Team::Blue) | team_bit(Team::Red))
                                  : team_bit(Team::Blue);
    add_row(level.gates, y, rules, rng);
  }
  lay_out(level, row_ys, false, rng);

  const int saws = number >= 3 ? 1 + (number >= 8 ? 1 : 0) + (number >= 15 ? 1 : 0) : 0;
  for (int s = 0; s < saws; ++s) {
    Saw saw;
    // Between two gate rows, so the saw guards the payoff of the row before.
    const int gap = rng.range(0, rows - 2);
    const double a = first_row + (last_row - first_row) * gap / (rows - 1);
    const double b = first_row + (last_row - first_row) * (gap + 1) / (rows - 1);
    saw.y = (a + b) / 2.0 + rng.uniform(-0.8, 0.8);
    saw.radius = rng.uniform(0.7, 0.95);
    saw.x = kFieldWidth / 2.0;
    saw.amplitude = rng.uniform(6.0, kFieldWidth / 2.0 - 1.5);
    saw.speed = rng.uniform(0.7, 1.5);
    saw.phase = rng.uniform(0.0, 2.0 * std::numbers::pi);
    level.saws.push_back(saw);
  }
  fit_saws(level);

  PowerUpSpec& powerups = level.powerups;
  powerups.first = 6.0;
  powerups.interval = 9.0;
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
  level.base_hp = {4600, 4600};
  level.time_limit = 240.0;
  level.frenzy_at = 150.0;
  // Near the centre line, as likely on either side, so neither team is favoured.
  level.powerups.first = 15.0;
  level.powerups.interval = 22.0;
  level.powerups.hp = 40;
  level.powerups.y_min = kFieldLength / 2.0 + 3.0;
  level.powerups.y_max = kFieldLength / 2.0 + 6.5;
  level.powerups.mirror = true;

  level.layout = roll_layout(rng);
  RowRules rules;
  rules.level = 3;
  rules.moving = level.layout != Layout::TwinLanes;
  rules.half = true;
  rules.special = true;
  rules.count = level.layout == Layout::TwinLanes ? 2 : 0;
  rules.teams = static_cast<std::uint8_t>(team_bit(Team::Blue) | team_bit(Team::Red));
  std::vector<Gate> half;
  const double near_row = rng.uniform(9.5, 11.5);
  const double far_row = rng.uniform(15.0, 17.0);
  add_row(half, near_row, rules, rng);
  add_row(half, far_row, rules, rng);
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
  saw.radius = 0.85;
  saw.amplitude = kFieldWidth / 2.0 - 2.0;
  saw.speed = 0.9;
  saw.phase = 0.0;
  level.saws.push_back(saw);
  lay_out(level, {near_row, far_row, kFieldLength - far_row, kFieldLength - near_row}, true, rng);
  fit_saws(level);
  return level;
}

}  // namespace mob_survivor
