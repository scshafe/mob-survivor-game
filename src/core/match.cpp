#include "mob_survivor/match.hpp"

#include <algorithm>
#include <utility>

#include "mob_survivor/level.hpp"

namespace mob_survivor {

namespace {

constexpr double kBotSkill = 0.75;

std::uint64_t level_seed(std::uint64_t seed, int level) {
  Rng rng(seed + 0x51ed270b27ULL * static_cast<std::uint64_t>(level));
  return rng.next();
}

}  // namespace

Match::Match(Mode mode, std::uint64_t seed, std::vector<SeatSpec> seats)
    : mode_(mode), seed_(seed), rng_(seed ^ 0xa5a5a5a5ULL) {
  std::sort(seats.begin(), seats.end(), [](const SeatSpec& a, const SeatSpec& b) { return a.slot < b.slot; });
  for (const SeatSpec& spec : seats) {
    MatchSeat seat;
    seat.spec = spec;
    if (mode_ == Mode::Campaign) seat.spec.team = Team::Blue;
    seats_.push_back(seat);
  }
  start_level();
}

MatchSeat* Match::find_seat(int slot) {
  for (MatchSeat& seat : seats_) {
    if (seat.spec.slot == slot) return &seat;
  }
  return nullptr;
}

const MatchSeat* Match::seat(int slot) const {
  for (const MatchSeat& seat : seats_) {
    if (seat.spec.slot == slot) return &seat;
  }
  return nullptr;
}

double Match::phase_time_left() const {
  if (phase_ == Phase::Playing) {
    const double limit = world_->level().time_limit;
    return limit > 0.0 ? std::max(0.0, limit - world_->elapsed()) : 0.0;
  }
  return std::max(0.0, phase_timer_);
}

void Match::start_level() {
  LevelSpec spec;
  if (mode_ == Mode::Campaign) {
    spec = make_campaign_level(level_, std::max(1, static_cast<int>(seats_.size())), seed_);
    spec.base_hp[team_index(Team::Blue)] += base_bonus_;
  } else {
    spec = make_versus_level(seed_);
    std::array<int, kTeamCount> sizes{};
    for (const MatchSeat& seat : seats_) ++sizes.at(static_cast<std::size_t>(team_index(seat.spec.team)));
    const int larger = std::max(sizes[0], sizes[1]);
    for (int& hp : spec.base_hp) hp += 2000 * std::max(0, larger - 1);
  }
  WorldConfig config;
  config.level = std::move(spec);
  config.seed = level_seed(seed_, level_);
  for (const MatchSeat& seat : seats_) config.cannons.push_back({seat.spec.slot, seat.spec.team, seat.stats});
  world_ = std::make_unique<World>(std::move(config));
  for (const MatchSeat& seat : seats_) world_->set_connected(seat.spec.slot, seat.connected);
  ++level_serial_;
  outro_ = -1.0;
  phase_ = Phase::Countdown;
  phase_timer_ = kCountdownSeconds;
}

void Match::finish_level() {
  for (MatchSeat& seat : seats_) {
    const Tally level = world_->tally(seat.spec.slot);
    seat.total.shots += level.shots;
    seat.total.gate_mobs += level.gate_mobs;
    seat.total.kills += level.kills;
    seat.total.base_damage += level.base_damage;
    seat.total.giants += level.giants;
    seat.total.bombs += level.bombs;
  }
  const Outcome result = world_->outcome();
  if (mode_ == Mode::Campaign && result == Outcome::BlueWins) {
    ++levels_cleared_;
    begin_upgrade();
    return;
  }
  outcome_ = mode_ == Mode::Campaign ? Outcome::RedWins : result;
  phase_ = Phase::GameOver;
  phase_timer_ = 0.0;
}

void Match::begin_upgrade() {
  phase_ = Phase::Upgrade;
  phase_timer_ = kUpgradeSeconds;
  for (MatchSeat& seat : seats_) {
    seat.offer = draw_offer(seat.cards, levels_cleared_, 3, rng_);
    seat.picked = seat.offer.empty();
    if (seat.spec.bot && !seat.picked) {
      pick_card(seat.spec.slot, rng_.range(0, static_cast<int>(seat.offer.size()) - 1));
    }
  }
}

bool Match::pick_card(int slot, int choice) {
  MatchSeat* seat = find_seat(slot);
  if (phase_ != Phase::Upgrade || seat == nullptr || seat->picked) return false;
  if (choice < 0 || choice >= static_cast<int>(seat->offer.size())) return false;
  const CardId card = seat->offer[static_cast<std::size_t>(choice)];
  if (apply_card(card, seat->stats)) base_bonus_ += 30;
  ++seat->cards.at(static_cast<std::size_t>(card));
  seat->picked = true;
  return true;
}

void Match::set_input(int slot, double target_x, bool firing) {
  const MatchSeat* seat = find_seat(slot);
  if (seat == nullptr || seat->spec.bot) return;
  world_->set_input(slot, target_x, firing && phase_ == Phase::Playing);
}

void Match::request_giant(int slot) {
  const MatchSeat* seat = find_seat(slot);
  if (phase_ == Phase::Playing && seat != nullptr && !seat->spec.bot) world_->request_giant(slot);
}

void Match::request_bomb(int slot, Vec2 target) {
  const MatchSeat* seat = find_seat(slot);
  if (phase_ == Phase::Playing && seat != nullptr && !seat->spec.bot) world_->request_bomb(slot, target);
}

void Match::set_connected(int slot, bool connected) {
  MatchSeat* seat = find_seat(slot);
  if (seat == nullptr || seat->spec.bot) return;
  seat->connected = connected;
  world_->set_connected(slot, connected);
}

Tally Match::total_tally(int slot) const {
  const MatchSeat* found = seat(slot);
  if (found == nullptr) return {};
  Tally total = found->total;
  // The level in play has not been added to the total yet.
  if (phase_ == Phase::Countdown || phase_ == Phase::Playing) {
    const Tally level = world_->tally(slot);
    total.shots += level.shots;
    total.gate_mobs += level.gate_mobs;
    total.kills += level.kills;
    total.base_damage += level.base_damage;
    total.giants += level.giants;
    total.bombs += level.bombs;
  }
  return total;
}

void Match::drive_bots(double dt) {
  for (MatchSeat& seat : seats_) {
    if (!seat.spec.bot) continue;
    const BotCommand command = bot_think(*world_, seat.spec.slot, kBotSkill, dt, seat.bot, rng_);
    world_->set_input(seat.spec.slot, command.target_x, command.firing);
    if (command.giant) world_->request_giant(seat.spec.slot);
    if (command.bomb) world_->request_bomb(seat.spec.slot, *command.bomb);
  }
}

void Match::step(double dt) {
  ++ticks_;
  switch (phase_) {
    case Phase::Lobby:
    case Phase::GameOver:
      return;
    case Phase::Countdown:
      phase_timer_ -= dt;
      if (phase_timer_ <= 0.0) {
        phase_ = Phase::Playing;
        phase_timer_ = 0.0;
      }
      return;
    case Phase::Playing:
      if (world_->outcome() == Outcome::None) {
        drive_bots(dt);
        world_->step(dt);
        return;
      }
      if (outro_ < 0.0) outro_ = kOutroSeconds;
      outro_ -= dt;
      if (outro_ <= 0.0) finish_level();
      return;
    case Phase::Upgrade: {
      phase_timer_ -= dt;
      const bool everyone = std::all_of(seats_.begin(), seats_.end(), [](const MatchSeat& seat) {
        return seat.picked || !seat.connected;
      });
      if (!everyone && phase_timer_ > 0.0) return;
      for (MatchSeat& seat : seats_) {
        if (!seat.picked) pick_card(seat.spec.slot, 0);
      }
      ++level_;
      start_level();
      return;
    }
  }
}

}  // namespace mob_survivor
