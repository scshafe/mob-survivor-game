#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mob_survivor/bot.hpp"
#include "mob_survivor/rng.hpp"
#include "mob_survivor/types.hpp"
#include "mob_survivor/upgrades.hpp"
#include "mob_survivor/world.hpp"

namespace mob_survivor {

enum class Phase : std::uint8_t {
  Lobby = 0,      // not used by Match itself; rooms report it before a match exists
  Countdown = 1,  // the level is laid out; nobody moves yet
  Playing = 2,
  Upgrade = 3,    // campaign only: each player picks one of three cards
  GameOver = 4,
};

inline constexpr double kCountdownSeconds = 3.0;
inline constexpr double kUpgradeSeconds = 20.0;
inline constexpr double kOutroSeconds = 2.0;

struct SeatSpec {
  int slot = 0;
  Team team = Team::Blue;
  bool bot = false;
};

struct MatchSeat {
  SeatSpec spec;
  PlayerStats stats;
  CardCounts cards{};
  std::vector<CardId> offer;
  bool picked = true;
  bool connected = true;
  Tally total;  // over every finished level
  BotState bot;
};

// A whole game: a campaign (level after level against the AI, with upgrade
// cards between levels, until the players' base falls) or one versus round.
// Deterministic like World: same seed, seats and inputs, same match.
class Match {
 public:
  Match(Mode mode, std::uint64_t seed, std::vector<SeatSpec> seats);

  void step(double dt_seconds);

  void set_input(int slot, double target_x, bool firing);
  void request_giant(int slot);
  void request_bomb(int slot, Vec2 target);
  bool pick_card(int slot, int choice);
  void set_connected(int slot, bool connected);

  [[nodiscard]] Mode mode() const { return mode_; }
  [[nodiscard]] Phase phase() const { return phase_; }
  [[nodiscard]] double phase_time_left() const;
  [[nodiscard]] int level() const { return level_; }
  // Changes whenever a new level (a new World) starts.
  [[nodiscard]] std::uint32_t level_serial() const { return level_serial_; }
  [[nodiscard]] const World& world() const { return *world_; }
  [[nodiscard]] const std::vector<MatchSeat>& seats() const { return seats_; }
  [[nodiscard]] const MatchSeat* seat(int slot) const;
  // GameOver only: who won. A campaign always ends with RedWins.
  [[nodiscard]] Outcome outcome() const { return outcome_; }
  [[nodiscard]] int levels_cleared() const { return levels_cleared_; }
  [[nodiscard]] Tally total_tally(int slot) const;
  [[nodiscard]] std::uint64_t ticks() const { return ticks_; }
  // The current level's events since the last call (see World::take_events).
  std::vector<Event> take_events() { return world_->take_events(); }

 private:
  MatchSeat* find_seat(int slot);
  void start_level();
  void finish_level();
  void begin_upgrade();
  void drive_bots(double dt);

  Mode mode_;
  std::uint64_t seed_;
  Rng rng_;
  std::vector<MatchSeat> seats_;
  std::unique_ptr<World> world_;
  Phase phase_ = Phase::Countdown;
  double phase_timer_ = kCountdownSeconds;
  double outro_ = -1.0;
  int level_ = 1;
  std::uint32_t level_serial_ = 0;
  int levels_cleared_ = 0;
  int base_bonus_ = 0;
  Outcome outcome_ = Outcome::None;
  std::uint64_t ticks_ = 0;
};

}  // namespace mob_survivor
