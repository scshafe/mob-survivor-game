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
  // Gates that change what mobs are rather than how many:
  Fuse = 3,    // every N hit points of grunts that walk in merge into one giant
  Runner = 4,  // grunts that walk through become fast runners
  Armor = 5,   // every mob that walks through survives its next hit
};
// The hit points a fused giant gets for the N it took in.
[[nodiscard]] constexpr int fused_giant_hp(int consumed) { return consumed + consumed / 5; }

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

// A level's named shape. Open is the plain lane; the others add walls,
// conveyor belts or teleport pads between the gate rows.
enum class Layout : std::uint8_t {
  Open = 0,
  Hourglass = 1,    // walls pinch the middle of the lane to a gap
  TwinLanes = 2,    // a wall down the centre splits the lane, with a gap halfway
  Conveyor = 3,     // belts across the lane carry mobs sideways
  Teleporters = 4,  // pairs of pads: a mob stepping on one comes out of the other
};
inline constexpr int kLayoutCount = 5;

// A block mobs cannot walk into: they are pushed out sideways and walk on
// along it. Power-ups, bombs and saw blades pass over walls.
struct Wall {
  double x0 = 0.0;
  double x1 = 0.0;
  double y0 = 0.0;
  double y1 = 0.0;
};

// A strip across the lane that carries every mob on it sideways at `push`
// units a second (+ toward larger x). Phasing mobs are not carried.
struct Conveyor {
  double y0 = 0.0;
  double y1 = 0.0;
  double push = 0.0;
};

// Two pads on the same line: a mob that steps on either comes out of the
// other, once per pair.
struct Teleporter {
  Vec2 a;
  Vec2 b;
  double radius = 1.0;
};
inline constexpr std::size_t kMaxTeleporters = 8;

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

enum class PowerUpKind : std::uint8_t {
  Shot = 0,    // the breaker fires one more mob every volley for the rest of the level
  Freeze = 1,  // the breaker's enemies march at half speed for a while
  Flip = 2,    // for a while every gate counts as "/2" for the breaker's enemies
  Shield = 3,  // the breaker's base soaks up some damage
  Magnet = 4,  // for a while the breaker's mobs drift toward the gate ahead
};
inline constexpr int kPowerUpKinds = 5;

// How power-ups appear: one drifts in from a side wall every so
// often, on a line between y_min and y_max, and leaves by the other wall.
struct PowerUpSpec {
  double first = 0.0;     // seconds before the first one (0: none this level)
  double interval = 0.0;  // seconds between the next ones, give or take 15%
  int hp = 12;            // mobs it takes to break
  double y_min = 0.0;
  double y_max = 0.0;
  bool mirror = false;    // versus: just as likely on the far side of the centre line
  double speed_min = 1.4;
  double speed_max = 2.4;
  // How often each kind turns up, by PowerUpKind. A flip on a level where it
  // could hurt nobody (one side has no gates) becomes a "+1 shot".
  std::array<double, kPowerUpKinds> weights{0.4, 0.15, 0.15, 0.15, 0.15};
};

struct LevelSpec {
  int number = 1;
  Mode mode = Mode::Campaign;
  std::vector<Gate> gates;
  std::vector<Saw> saws;
  Layout layout = Layout::Open;
  std::vector<Wall> walls;
  std::vector<Conveyor> conveyors;
  std::vector<Teleporter> teleporters;
  AiSpec ai;
  PowerUpSpec powerups;
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
