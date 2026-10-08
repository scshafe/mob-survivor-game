#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "mob_survivor/level.hpp"
#include "mob_survivor/rng.hpp"
#include "mob_survivor/types.hpp"
#include "mob_survivor/upgrades.hpp"

namespace mob_survivor {

enum class MobKind : std::uint8_t {
  Grunt = 0,   // the cannon's ammunition; may stand for a squad (hp > 1)
  Runner = 1,  // fast, frail AI grunt
  Giant = 2,   // a charged shot: slow, tough, tramples grunts
  Brute = 3,   // the boss levels' giant
};

struct Mob {
  std::uint32_t id = 0;
  Team team = Team::Blue;
  MobKind kind = MobKind::Grunt;
  int owner = -1;  // the player slot that fired it, or -1 for the AI
  Vec2 position;
  double drift = 0.0;  // sideways velocity from crowding
  int hp = 1;          // a grunt with hp > 1 is a squad of that many
  std::uint32_t gates_passed = 0;  // bit i: gate i already changed this mob
  bool armored = false;            // its next hit (enemy, saw or bomb) does no harm
  std::uint8_t teleported = 0;     // bit i: teleporter pair i already moved this mob
  double saw_cooldown = 0.0;
};

[[nodiscard]] double mob_radius(const Mob& mob);
[[nodiscard]] double mob_speed(MobKind kind);

struct Cannon {
  int slot = 0;
  Team team = Team::Blue;
  double x = kFieldWidth / 2.0;
  double target_x = kFieldWidth / 2.0;
  bool firing = false;
  bool connected = true;
  double fire_timer = 0.0;
  double charge = 0.0;  // 0..1; a giant is ready at 1
  double bomb_cooldown = 0.0;
  int shots_per_volley = 1;  // raised for the rest of the level by breaking power-ups
  PlayerStats stats;
  double phase_cooldown = 0.0;  // until Phase can be used again
  double phase_time = 0.0;      // > 0: this player's mobs ignore the map
  double magnet_time = 0.0;     // > 0: this player's mobs drift toward the gate ahead
  bool giant_requested = false;
  bool phase_requested = false;
  std::optional<Vec2> bomb_request;
};

inline constexpr double kCannonSpeed = 20.0;
inline constexpr double kShotsPerGiant = 45.0;
inline constexpr double kBombCooldown = 13.0;
inline constexpr double kBombFuse = 0.8;
inline constexpr double kBombRadius = 2.6;
inline constexpr int kBombDamage = 6;
inline constexpr std::size_t kMaxMobsPerTeam = 300;
inline constexpr int kMaxShotsPerVolley = 3;
// Phase: for a few seconds a player's mobs ignore the map (saw blades and
// "/2" gates) but still fight enemy mobs and hit the base as usual.
inline constexpr double kPhaseSeconds = 5.0;
inline constexpr double kPhaseCooldown = 20.0;  // counted from when Phase ends
inline constexpr double kSawCooldown = 0.8;     // a mob a saw cut is safe from it for this long
inline constexpr double kPowerUpRadius = 0.9;
inline constexpr std::size_t kMaxPowerUps = 3;
inline constexpr double kFreezeSeconds = 6.0;   // enemy mobs march at half speed
inline constexpr double kFlipSeconds = 7.0;     // every gate counts as "/2" for the enemy
inline constexpr double kMagnetSeconds = 8.0;   // your mobs drift toward the next gate
inline constexpr double kShieldShare = 0.15;    // of the base's max health, soaked up before the base

struct Base {
  Team team = Team::Blue;
  int hp = 0;
  int max_hp = 0;
};

// Team-wide effects from power-ups.
struct TeamEffects {
  double frozen = 0.0;   // seconds this team's mobs still march at half speed
  double flipped = 0.0;  // seconds every gate still counts as "/2" for this team
  int shield = 0;        // damage this team's base still soaks up
};

struct Bomb {
  Team team = Team::Blue;
  int owner = -1;
  Vec2 from;
  Vec2 target;
  double fuse = kBombFuse;
  double radius = kBombRadius;
  int damage = kBombDamage;
};

// A target drifting across the lane. A player's grunts and runners that run
// into it are spent on it, one hit point for one; the player whose mob breaks
// it gets its effect. The AI's mobs and giants pass by it.
struct PowerUp {
  std::uint32_t id = 0;
  PowerUpKind kind = PowerUpKind::Shot;
  Vec2 position;
  double velocity = 0.0;  // along x, units a second
  int hp = 0;
  int max_hp = 0;
};

enum class EventType : std::uint8_t {
  GatePass = 1,     // value: mobs gained (or lost, negative) at gate `index`
  BaseHit = 2,      // value: damage dealt to `team`'s base
  BombBlast = 3,    // value: mobs destroyed
  GiantLaunch = 4,  // value: the giant's hp
  SawCut = 5,
  Frenzy = 6,       // every gate just got stronger
  BossSpawn = 7,
  PowerUp = 8,      // a power-up broke; index: the player who broke it, value: its PowerUpKind
  Phase = 9,        // index: the player whose mobs just started to phase
  Teleport = 10,    // value: mobs moved this step; index: pad (2 x pair, +1 for pad b) they left; position: where they came out
};

struct Event {
  EventType type = EventType::GatePass;
  Team team = Team::Blue;
  Vec2 position;
  int value = 0;
  int index = -1;
};

// What each player did, for the scoreboard.
struct Tally {
  int shots = 0;
  int gate_mobs = 0;
  int kills = 0;
  int base_damage = 0;
  int giants = 0;
  int bombs = 0;
};

enum class Outcome : std::uint8_t { None = 0, BlueWins = 1, RedWins = 2, Draw = 3 };

struct CannonSeat {
  int slot = 0;
  Team team = Team::Blue;
  PlayerStats stats;
};

struct WorldConfig {
  LevelSpec level;
  std::vector<CannonSeat> cannons;
  std::uint64_t seed = 1;
};

// One level being played: cannons, mobs, gates, saws, bombs and two bases.
// step() is deterministic: the same config and the same inputs, applied
// between the same steps, give the same world, so tests and replays can rely
// on it. It holds no rendering, input or platform code.
class World {
 public:
  explicit World(WorldConfig config);

  // Inputs: they take effect at the next step.
  void set_input(int slot, double target_x, bool firing);
  void request_giant(int slot);
  void request_bomb(int slot, Vec2 target);
  void request_phase(int slot);
  void set_connected(int slot, bool connected);

  void step(double dt_seconds);

  // Adds a mob as-is (it gets a fresh id). Used by tests and scenarios.
  std::uint32_t spawn(Mob mob);

  [[nodiscard]] const LevelSpec& level() const { return level_; }
  [[nodiscard]] const std::vector<Mob>& mobs() const { return mobs_; }
  [[nodiscard]] const std::vector<Cannon>& cannons() const { return cannons_; }
  [[nodiscard]] const Cannon* cannon(int slot) const;
  [[nodiscard]] const std::array<Base, kTeamCount>& bases() const { return bases_; }
  [[nodiscard]] const Base& base(Team team) const { return bases_.at(team_index(team)); }
  [[nodiscard]] const std::vector<Bomb>& bombs() const { return bombs_; }
  [[nodiscard]] const std::vector<PowerUp>& powerups() const { return powerups_; }
  [[nodiscard]] const TeamEffects& effects(Team team) const { return effects_.at(team_index(team)); }
  [[nodiscard]] double gate_x(std::size_t index) const;
  // How far fuse gate `index` has filled toward its next giant for `team`.
  [[nodiscard]] int fuse_fill(std::size_t index, Team team) const;
  [[nodiscard]] double saw_x(std::size_t index) const;
  [[nodiscard]] std::uint64_t ticks() const { return ticks_; }
  [[nodiscard]] double elapsed() const { return elapsed_; }
  [[nodiscard]] bool frenzy() const { return frenzy_; }
  [[nodiscard]] Outcome outcome() const { return outcome_; }
  [[nodiscard]] std::size_t team_mob_count(Team team) const;
  [[nodiscard]] Tally tally(int slot) const;
  // Whether this mob ignores the map just now (its player's Phase is on).
  [[nodiscard]] bool phased(const Mob& mob) const;

  // Events since the last call, oldest first.
  std::vector<Event> take_events();

 private:
  Cannon* find_cannon(int slot);
  bool add_mob(Mob mob);
  void emit(Event event);
  void step_cannons(double dt);
  void step_ai(double dt);
  void step_bombs(double dt);
  void step_powerups(double dt);
  void break_powerup(const PowerUp& powerup, Cannon& breaker);
  PowerUpKind roll_powerup_kind();
  void step_mobs(double dt);
  void apply_gate(std::size_t gate_index, Mob& mob, std::vector<Mob>& born);
  void resolve_contacts(double dt);
  void apply_layout(Mob& mob, double radius, bool immune, double dt);
  void settle_outcome();
  Tally& tally_for(int slot);

  LevelSpec level_;
  Rng rng_;
  std::vector<Mob> mobs_;
  std::vector<Cannon> cannons_;
  std::array<Base, kTeamCount> bases_{};
  std::vector<Bomb> bombs_;
  std::vector<PowerUp> powerups_;
  std::array<TeamEffects, kTeamCount> effects_{};
  bool flip_useful_ = false;  // both teams have gates, so a flip can hurt someone
  std::vector<Event> events_;
  std::vector<std::pair<int, Tally>> tallies_;
  Tally unowned_tally_;
  std::uint32_t next_id_ = 1;
  std::uint64_t ticks_ = 0;
  double elapsed_ = 0.0;
  bool frenzy_ = false;
  Outcome outcome_ = Outcome::None;

  // AI spawner state.
  double wave_timer_ = 0.0;
  double boss_timer_ = 0.0;
  int wave_index_ = 0;
  double powerup_timer_ = 0.0;

  // Gate effects collected during one step, emitted as one event per gate.
  std::vector<int> gate_gain_;
  // Hit points each fuse gate has taken in toward its next giant, per team.
  std::vector<int> fuse_fill_;
  // Mobs moved by each teleporter pad during one step, emitted as one event a pad.
  std::vector<int> teleport_count_;

  // Scratch space for the contact grid, kept to avoid reallocating.
  std::vector<int> cell_of_;
  std::vector<int> cell_start_;
  std::vector<int> cell_items_;
};

}  // namespace mob_survivor
