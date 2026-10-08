#include "mob_survivor/replay.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mob_survivor {

namespace {

int hundredths(double value) {
  if (!std::isfinite(value)) return 0;
  return static_cast<int>(std::lround(std::clamp(value, -1000.0, 1000.0) * 100.0));
}

double units(int hundredths) { return hundredths / 100.0; }

}  // namespace

void apply_input(Match& match, const ReplayInput& input) {
  switch (input.kind) {
    case ReplayInput::Kind::Aim:
      match.set_input(input.slot, units(input.a), input.b != 0);
      break;
    case ReplayInput::Kind::Giant:
      match.request_giant(input.slot);
      break;
    case ReplayInput::Kind::Bomb:
      match.request_bomb(input.slot, {units(input.a), units(input.b)});
      break;
    case ReplayInput::Kind::Phase:
      match.request_phase(input.slot);
      break;
    case ReplayInput::Kind::Pick:
      static_cast<void>(match.pick_card(input.slot, input.a));
      break;
    case ReplayInput::Kind::Connect:
      match.set_connected(input.slot, input.a != 0);
      break;
  }
}

void InputLog::record(const Match& match, ReplayInput input) {
  input.tick = static_cast<std::uint32_t>(match.ticks());
  inputs_.push_back(input);
}

void InputLog::aim(Match& match, int slot, double x, bool firing) {
  ReplayInput input{0, ReplayInput::Kind::Aim, slot, hundredths(x), firing ? 1 : 0};
  // What Match::set_input would leave on the cannon (see World::set_input).
  const MatchSeat* seat = match.seat(slot);
  if (seat == nullptr || seat->spec.bot) return;
  if (const Cannon* cannon = match.world().cannon(slot)) {
    const double target = std::clamp(units(input.a), 0.0, kFieldWidth);
    const bool fires = input.b != 0 && match.phase() == Phase::Playing;
    if (cannon->target_x == target && cannon->firing == fires) return;
  }
  apply_input(match, input);
  record(match, input);
}

void InputLog::giant(Match& match, int slot) {
  ReplayInput input{0, ReplayInput::Kind::Giant, slot, 0, 0};
  apply_input(match, input);
  record(match, input);
}

void InputLog::bomb(Match& match, int slot, Vec2 target) {
  ReplayInput input{0, ReplayInput::Kind::Bomb, slot, hundredths(target.x), hundredths(target.y)};
  apply_input(match, input);
  record(match, input);
}

void InputLog::phase(Match& match, int slot) {
  ReplayInput input{0, ReplayInput::Kind::Phase, slot, 0, 0};
  apply_input(match, input);
  record(match, input);
}

bool InputLog::pick(Match& match, int slot, int choice) {
  if (!match.pick_card(slot, choice)) return false;
  record(match, {0, ReplayInput::Kind::Pick, slot, choice, 0});
  return true;
}

void InputLog::connect(Match& match, int slot, bool connected) {
  ReplayInput input{0, ReplayInput::Kind::Connect, slot, connected ? 1 : 0, 0};
  apply_input(match, input);
  record(match, input);
}

void InputLog::clear() { inputs_.clear(); }

ReplayPlayer::ReplayPlayer(Replay replay)
    : replay_(std::move(replay)), match_(std::make_unique<Match>(replay_.mode, replay_.seed, replay_.seats)) {}

void ReplayPlayer::step() {
  while (next_ < replay_.inputs.size() && replay_.inputs[next_].tick <= match_->ticks()) {
    apply_input(*match_, replay_.inputs[next_]);
    ++next_;
  }
  match_->step(kTickSeconds);
}

std::unique_ptr<Match> play_replay(const Replay& replay, std::uint64_t max_ticks) {
  ReplayPlayer player(replay);
  while (!player.done() && player.match().ticks() < max_ticks) player.step();
  return player.release();
}

}  // namespace mob_survivor
