#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "mob_survivor/rng.hpp"

namespace mob_survivor {

// Everything a player's upgrades change. A fresh player has the defaults.
struct PlayerStats {
  double fire_interval = 0.17;  // seconds between volleys while firing
  bool halve_immune = false;    // "/2" gates never cull this player's mobs
  double charge_rate = 1.0;     // giant charge per shot, multiplier
  double giant_hp_scale = 1.0;
  double bomb_cooldown_scale = 1.0;
  double bomb_radius_scale = 1.0;
  double speed_scale = 1.0;     // this player's mobs' march speed
};

enum class CardId : std::uint8_t {
  RapidFire = 0,
  LuckyCharm,
  QuickCharge,
  HeavyGiants,
  Demolitions,
  Sprinters,
  Fortify,
};
inline constexpr int kCardCount = 7;

struct CardInfo {
  CardId id;
  std::string_view key;    // stable name used on the wire
  std::string_view title;
  std::string_view text;
  int max_stacks;          // 0 means unlimited
  int min_level;           // first campaign level after which it can be offered
};

[[nodiscard]] const CardInfo& card_info(CardId id);
[[nodiscard]] const std::array<CardInfo, kCardCount>& all_cards();

// How many times each card has been taken by one player.
using CardCounts = std::array<int, kCardCount>;

// Applies one card to a player's stats. Fortify is a team effect: it returns
// true so the caller can raise the team's base instead.
bool apply_card(CardId id, PlayerStats& stats);

// Offers up to `count` distinct cards the player may still take after
// clearing `level`, drawn from `rng`.
[[nodiscard]] std::vector<CardId> draw_offer(const CardCounts& taken, int level, int count, Rng& rng);

}  // namespace mob_survivor
