#include <cmath>

#include "check.hpp"
#include "mob_survivor/bot.hpp"
#include "mob_survivor/replay.hpp"

using namespace mob_survivor;

namespace {

// A fingerprint of a match's state: anything that drifts shows up here.
double fingerprint(const Match& match) {
  const World& world = match.world();
  double sum = static_cast<double>(match.ticks()) + match.level() * 1000.0 + world.base(Team::Red).hp;
  for (const Mob& mob : world.mobs()) sum += mob.position.x * 3.0 + mob.position.y * 7.0 + mob.hp + mob.id;
  return sum;
}

// A campaign played by the bot brain through an InputLog, as a player's
// inputs would arrive: aims every tick (most of them unchanged), giants,
// bombs, phases, card picks and a dropped connection.
Replay record(std::uint64_t seed, int seconds, double& fingerprint_out, int& levels_out) {
  Replay replay;
  replay.mode = Mode::Campaign;
  replay.seed = seed;
  replay.seats = {{0, Team::Blue, false}};
  Match match(replay.mode, replay.seed, replay.seats);
  InputLog log;
  BotState brain;
  Rng rng(seed * 31 + 7);
  for (int t = 0; t < seconds * 30 && match.phase() != Phase::GameOver; ++t) {
    if (match.phase() == Phase::Playing && match.world().outcome() == Outcome::None) {
      const BotCommand command = bot_think(match.world(), 0, 0.8, kTickSeconds, brain, rng);
      log.aim(match, 0, command.target_x, command.firing);
      if (command.giant) log.giant(match, 0);
      if (command.bomb) log.bomb(match, 0, *command.bomb);
      if (command.phase) log.phase(match, 0);
    }
    if (match.phase() == Phase::Upgrade && !match.seat(0)->picked) log.pick(match, 0, static_cast<int>(t % 3));
    if (t == 30 * 20) log.connect(match, 0, false);
    if (t == 30 * 22) log.connect(match, 0, true);
    match.step(kTickSeconds);
  }
  replay.inputs = log.inputs();
  fingerprint_out = fingerprint(match);
  levels_out = match.levels_cleared();
  return replay;
}

void a_replay_plays_the_same_match() {
  double expected = 0.0;
  int levels = 0;
  const Replay replay = record(4, 150, expected, levels);
  CHECK(levels >= 1);  // it got through at least one card pick
  CHECK(!replay.inputs.empty());
  const auto ticks = static_cast<std::uint64_t>(150 * 30);
  auto played = play_replay(replay, ticks);
  CHECK(played->ticks() == ticks || played->phase() == Phase::GameOver);
  CHECK(played->levels_cleared() == levels);
  CHECK(fingerprint(*played) == expected);
}

void aims_that_change_nothing_are_not_recorded() {
  Match match(Mode::Campaign, 1, {{0, Team::Blue, false}});
  InputLog log;
  log.aim(match, 0, 12.0, true);  // the countdown: the cannon already aims at 12 and cannot fire
  CHECK(log.inputs().empty());
  for (int i = 0; i < 100; ++i) match.step(kTickSeconds);
  CHECK(match.phase() == Phase::Playing);
  for (int i = 0; i < 10; ++i) log.aim(match, 0, 7.004, true);  // rounds to 7.00 every time
  CHECK(log.inputs().size() == 1);
  if (log.inputs().empty()) return;
  CHECK(log.inputs().front().a == 700 && log.inputs().front().b == 1);
  log.aim(match, 0, 12.5, true);
  log.aim(match, 0, 12.5, false);
  CHECK(log.inputs().size() == 3);
  log.clear();
  CHECK(log.inputs().empty());
}

void a_different_input_changes_the_match() {
  double expected = 0.0;
  int levels = 0;
  Replay replay = record(9, 40, expected, levels);
  for (ReplayInput& input : replay.inputs) {
    if (input.kind == ReplayInput::Kind::Aim) input.a = 300;  // hug the left wall all match
  }
  auto played = play_replay(replay, 40 * 30);
  CHECK(fingerprint(*played) != expected);
}

}  // namespace

int main() {
  a_replay_plays_the_same_match();
  aims_that_change_nothing_are_not_recorded();
  a_different_input_changes_the_match();
  return check::finish("replay_test");
}
