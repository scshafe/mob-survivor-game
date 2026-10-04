#include "mob_survivor/upgrades.hpp"

#include <algorithm>

namespace mob_survivor {

namespace {

constexpr std::array<CardInfo, kCardCount> kCards{{
    {CardId::RapidFire, "rapid", "Rapid Fire", "Cannon fires 18% faster.", 4, 1},
    {CardId::TwinBarrel, "twin", "Twin Barrel", "+1 mob in every volley.", 2, 1},
    {CardId::GenerousGates, "generous", "Generous Gates", "Every + gate gives you 3 more.", 3, 1},
    {CardId::GoldenGates, "golden", "Golden Gates", "Every x gate multiplies one more for you.", 1, 3},
    {CardId::LuckyCharm, "lucky", "Lucky Charm", "Your mobs walk through /2 gates unharmed.", 1, 2},
    {CardId::QuickCharge, "charge", "Quick Charge", "Giants charge 35% faster.", 3, 1},
    {CardId::HeavyGiants, "heavy", "Heavy Giants", "Your giants have 60% more health.", 3, 1},
    {CardId::Demolitions, "demo", "Demolitions", "Bombs recharge 30% faster and blast 20% wider.", 3, 1},
    {CardId::Sprinters, "swift", "Sprinters", "Your mobs march 15% faster.", 2, 1},
    {CardId::Fortify, "fortify", "Fortify", "Your base gains 30 health and is fully repaired.", 0, 1},
}};

}  // namespace

const CardInfo& card_info(CardId id) { return kCards.at(static_cast<std::size_t>(id)); }

const std::array<CardInfo, kCardCount>& all_cards() { return kCards; }

bool apply_card(CardId id, PlayerStats& stats) {
  switch (id) {
    case CardId::RapidFire:
      stats.fire_interval *= 0.82;
      return false;
    case CardId::TwinBarrel:
      stats.shots_per_volley += 1;
      return false;
    case CardId::GenerousGates:
      stats.add_gate_bonus += 3;
      return false;
    case CardId::GoldenGates:
      stats.mul_gate_bonus += 1;
      return false;
    case CardId::LuckyCharm:
      stats.halve_immune = true;
      return false;
    case CardId::QuickCharge:
      stats.charge_rate *= 1.35;
      return false;
    case CardId::HeavyGiants:
      stats.giant_hp_scale += 0.6;
      return false;
    case CardId::Demolitions:
      stats.bomb_cooldown_scale *= 0.7;
      stats.bomb_radius_scale *= 1.2;
      return false;
    case CardId::Sprinters:
      stats.speed_scale *= 1.15;
      return false;
    case CardId::Fortify:
      return true;
  }
  return false;
}

std::vector<CardId> draw_offer(const CardCounts& taken, int level, int count, Rng& rng) {
  std::vector<CardId> pool;
  for (const CardInfo& card : kCards) {
    const auto index = static_cast<std::size_t>(card.id);
    const bool maxed = card.max_stacks != 0 && taken.at(index) >= card.max_stacks;
    if (!maxed && level >= card.min_level) {
      pool.push_back(card.id);
    }
  }
  std::vector<CardId> offer;
  while (!pool.empty() && static_cast<int>(offer.size()) < count) {
    const int pick = rng.range(0, static_cast<int>(pool.size()) - 1);
    offer.push_back(pool[static_cast<std::size_t>(pick)]);
    pool.erase(pool.begin() + pick);
  }
  return offer;
}

}  // namespace mob_survivor
