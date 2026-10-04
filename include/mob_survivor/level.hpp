#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "mob_survivor/types.hpp"

namespace mob_survivor {

enum class GateOp : std::uint8_t {
  Add = 0,   // "+N": N more mobs for every mob that walks through
  Mul = 1,   // "xN": every mob that walks through becomes N
  Half = 2,  // "/2": half of the mobs that walk through are lost
};

// A gate spans [x - width/2, x + width/2] on the line y. A mob of a team in
// `teams` that crosses the line within that span is changed once per gate.
struct Gate {
  double x = 0.0;
  double y = 0.0;
  double width = 4.0;
  GateOp op = GateOp::Add;
  int value = 1;
  std::uint8_t teams = team_bit(Team::Blue);
  // Moving gates slide along x: x(t) = x + amplitude * sin(phase + speed * t).
  double amplitude = 0.0;
  double speed = 0.0;
  double phase = 0.0;
};

// A spinning blade that slides along y = const and cuts any mob it touches.
struct Saw {
  double x = 0.0;
  double y = 0.0;
  double radius = 0.8;
  double amplitude = 0.0;
  double speed = 0.0;
  double phase = 0.0;
};

// How the AI base (Red, in the campaign) sends its waves.
struct AiSpec {
  bool enabled = false;
  double first_wave = 2.0;      // seconds before the first wave
  double wave_interval = 4.0;   // seconds between waves
  int wave_size = 6;            // grunts in the first wave
  double wave_growth = 0.34;    // extra grunts per wave
  int runner_percent = 0;       // share of a wave that are fast runners
  int giant_every = 0;          // a giant joins every Nth wave (0: never)
  int giant_hp = 12;
  double boss_interval = 0.0;   // a brute every N seconds (0: no boss)
  double first_boss = 0.0;
  int boss_hp = 0;
};

struct LevelSpec {
  int number = 1;
  Mode mode = Mode::Campaign;
  std::vector<Gate> gates;
  std::vector<Saw> saws;
  AiSpec ai;
  std::array<int, kTeamCount> base_hp{60, 60};
  double time_limit = 0.0;  // 0: no limit
  double frenzy_at = 0.0;   // 0: no frenzy; else every gate gets stronger then
  bool boss = false;
};

inline constexpr std::size_t kMaxGates = 32;

[[nodiscard]] double gate_x_at(const Gate& gate, double seconds);
[[nodiscard]] double saw_x_at(const Saw& saw, double seconds);

// A campaign level for `players` co-op players. Same arguments, same level.
[[nodiscard]] LevelSpec make_campaign_level(int number, int players, std::uint64_t seed);

// A versus arena: point-symmetric, so neither side has an advantage.
[[nodiscard]] LevelSpec make_versus_level(std::uint64_t seed);

}  // namespace mob_survivor
