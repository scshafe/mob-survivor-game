#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "mob_survivor/match.hpp"
#include "mob_survivor/types.hpp"

namespace mob_survivor {

// One input a player gave a match, stamped with the match tick it was given
// at (it takes effect at the next step, as live inputs do).
struct ReplayInput {
  enum class Kind : std::uint8_t {
    Aim = 0,      // a: target x in hundredths of a unit, b: 1 while firing
    Giant = 1,
    Bomb = 2,     // a, b: target x, y in hundredths
    Phase = 3,
    Pick = 4,     // a: the card chosen
    Connect = 5,  // a: 1 connected, 0 dropped
  };
  std::uint32_t tick = 0;
  Kind kind = Kind::Aim;
  int slot = 0;
  int a = 0;
  int b = 0;
};

// Everything needed to play a match again exactly: Match is deterministic,
// so the same mode, seed, seats and inputs at the same ticks (stepped at
// kTickSeconds) give the same match.
struct Replay {
  Mode mode = Mode::Campaign;
  std::uint64_t seed = 0;
  std::vector<SeatSpec> seats;
  std::vector<ReplayInput> inputs;
};

// Gives a match its players' inputs and keeps a record of them. Aims are
// rounded to hundredths of a unit before they reach the match, so the record
// replays exactly. An aim that would change nothing (the cannon already has
// that target and firing state) is neither applied nor recorded, which keeps
// the record small and replays the same, since the replayed cannon is in the
// same state at that tick.
class InputLog {
 public:
  void aim(Match& match, int slot, double x, bool firing);
  void giant(Match& match, int slot);
  void bomb(Match& match, int slot, Vec2 target);
  void phase(Match& match, int slot);
  bool pick(Match& match, int slot, int choice);
  void connect(Match& match, int slot, bool connected);
  void clear();

  [[nodiscard]] const std::vector<ReplayInput>& inputs() const { return inputs_; }

 private:
  void record(const Match& match, ReplayInput input);

  std::vector<ReplayInput> inputs_;
};

// Plays a replay back, one kTickSeconds step at a time.
class ReplayPlayer {
 public:
  explicit ReplayPlayer(Replay replay);

  // Applies the inputs due at this tick, then steps the match.
  void step();
  [[nodiscard]] Match& match() { return *match_; }
  [[nodiscard]] const Match& match() const { return *match_; }
  [[nodiscard]] bool done() const { return match_->phase() == Phase::GameOver; }
  // Hands over the match; the player is spent after this.
  [[nodiscard]] std::unique_ptr<Match> release() { return std::move(match_); }

 private:
  Replay replay_;
  std::unique_ptr<Match> match_;
  std::size_t next_ = 0;
};

// Applies one recorded input to a match.
void apply_input(Match& match, const ReplayInput& input);

// Plays a whole replay to its end (or `max_ticks`) and returns the match.
[[nodiscard]] std::unique_ptr<Match> play_replay(const Replay& replay, std::uint64_t max_ticks);

}  // namespace mob_survivor
