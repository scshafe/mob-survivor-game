#include <algorithm>

#include "check.hpp"
#include "mob_survivor/bot.hpp"
#include "mob_survivor/match.hpp"

using namespace mob_survivor;

namespace {

// Plays the human seats with the bot brain until `done` or a time limit.
template <typename Done>
void play(Match& match, std::vector<BotState>& brains, Rng& rng, double seconds, Done done) {
  for (int t = 0; t < static_cast<int>(seconds * 30) && !done(match); ++t) {
    if (match.phase() == Phase::Playing && match.world().outcome() == Outcome::None) {
      for (std::size_t s = 0; s < brains.size(); ++s) {
        const int slot = static_cast<int>(s);
        const auto command = bot_think(match.world(), slot, 0.8, kTickSeconds, brains[s], rng);
        match.set_input(slot, command.target_x, command.firing);
        if (command.giant) match.request_giant(slot);
        if (command.bomb) match.request_bomb(slot, *command.bomb);
        if (command.phase) match.request_phase(slot);
      }
    }
    match.step(kTickSeconds);
  }
}

void a_campaign_counts_down_then_plays() {
  Match match(Mode::Campaign, 1, {{0, Team::Blue, false}});
  CHECK(match.phase() == Phase::Countdown);
  CHECK(match.level() == 1);
  CHECK(match.level_serial() == 1);
  // Firing during the countdown does nothing.
  match.set_input(0, 12.0, true);
  for (int i = 0; i < 30; ++i) match.step(kTickSeconds);
  CHECK(match.world().mobs().empty());
  for (int i = 0; i < 70; ++i) match.step(kTickSeconds);
  CHECK(match.phase() == Phase::Playing);
}

void clearing_a_level_offers_cards_then_the_next_level() {
  Match match(Mode::Campaign, 2, {{0, Team::Blue, false}});
  std::vector<BotState> brains(1);
  Rng rng(5);
  play(match, brains, rng, 240.0, [](const Match& m) { return m.phase() != Phase::Countdown && m.phase() != Phase::Playing; });
  CHECK(match.phase() == Phase::Upgrade);
  CHECK(match.levels_cleared() == 1);
  const MatchSeat* seat = match.seat(0);
  CHECK(seat != nullptr && seat->offer.size() == 3 && !seat->picked);
  // A bad choice is refused; a good one sticks.
  CHECK(!match.pick_card(0, 7));
  const CardId chosen = seat->offer[1];
  CHECK(match.pick_card(0, 1));
  CHECK(!match.pick_card(0, 0));
  CHECK(match.seat(0)->cards.at(static_cast<std::size_t>(chosen)) == 1);
  match.step(kTickSeconds);  // everyone picked: next level at once
  CHECK(match.phase() == Phase::Countdown);
  CHECK(match.level() == 2);
  CHECK(match.level_serial() == 2);
  CHECK(match.total_tally(0).shots > 0);
}

void an_idle_campaign_is_lost() {
  Match match(Mode::Campaign, 3, {{0, Team::Blue, false}});
  for (int i = 0; i < 30 * 120 && match.phase() != Phase::GameOver; ++i) match.step(kTickSeconds);
  CHECK(match.phase() == Phase::GameOver);
  CHECK(match.outcome() == Outcome::RedWins);
  CHECK(match.levels_cleared() == 0);
}

void the_upgrade_timer_picks_for_the_slow() {
  Match match(Mode::Campaign, 2, {{0, Team::Blue, false}, {1, Team::Blue, false}});
  std::vector<BotState> brains(2);
  Rng rng(9);
  play(match, brains, rng, 300.0, [](const Match& m) { return m.phase() == Phase::Upgrade || m.phase() == Phase::GameOver; });
  CHECK(match.phase() == Phase::Upgrade);
  CHECK(match.pick_card(0, 0));
  for (int i = 0; i < 30 * 5; ++i) match.step(kTickSeconds);
  CHECK(match.phase() == Phase::Upgrade);  // still waiting for slot 1
  for (int i = 0; i < static_cast<int>(kUpgradeSeconds * 30) && match.phase() == Phase::Upgrade; ++i) {
    match.step(kTickSeconds);
  }
  CHECK(match.phase() == Phase::Countdown);
  CHECK(match.level() == 2);
}

void a_disconnected_player_does_not_hold_up_the_cards() {
  Match match(Mode::Campaign, 2, {{0, Team::Blue, false}, {1, Team::Blue, false}});
  std::vector<BotState> brains(2);
  Rng rng(9);
  play(match, brains, rng, 300.0, [](const Match& m) { return m.phase() == Phase::Upgrade || m.phase() == Phase::GameOver; });
  CHECK(match.phase() == Phase::Upgrade);
  match.set_connected(1, false);
  CHECK(match.pick_card(0, 2));
  match.step(kTickSeconds);
  CHECK(match.phase() == Phase::Countdown);
}

void bots_finish_a_versus_round() {
  auto run = [] {
    Match match(Mode::Versus, 77, {{0, Team::Blue, true}, {1, Team::Red, true}});
    for (int i = 0; i < 30 * 300 && match.phase() != Phase::GameOver; ++i) match.step(kTickSeconds);
    return std::make_pair(match.outcome(), match.ticks());
  };
  const auto first = run();
  CHECK(first.first != Outcome::None);
  CHECK(first.second < 30U * 260U);
  CHECK(run() == first);  // deterministic
}

void bots_ignore_human_inputs_for_their_seats() {
  Match match(Mode::Versus, 4, {{0, Team::Blue, false}, {1, Team::Red, true}});
  for (int i = 0; i < 30 * 4; ++i) match.step(kTickSeconds);
  match.set_input(1, 1.0, false);
  match.step(kTickSeconds);
  CHECK(match.world().cannon(1)->firing);
}

}  // namespace

int main() {
  a_campaign_counts_down_then_plays();
  clearing_a_level_offers_cards_then_the_next_level();
  an_idle_campaign_is_lost();
  the_upgrade_timer_picks_for_the_slow();
  a_disconnected_player_does_not_hold_up_the_cards();
  bots_finish_a_versus_round();
  bots_ignore_human_inputs_for_their_seats();
  return check::finish("match_test");
}
