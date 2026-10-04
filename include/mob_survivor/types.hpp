#pragma once

#include <cstdint>

namespace mob_survivor {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

// The arena is a portrait lane. Blue holds the bottom (y = 0) and sends its
// mobs up; Red holds the top (y = kFieldLength) and sends its mobs down.
inline constexpr double kFieldWidth = 24.0;
inline constexpr double kFieldLength = 40.0;
// A mob that crosses into this band at the far end hits that end's base.
inline constexpr double kBaseDepth = 2.5;
// Cannons sit just in front of their base.
inline constexpr double kCannonOffset = 3.4;

inline constexpr double kTickSeconds = 1.0 / 30.0;

enum class Team : std::uint8_t { Blue = 0, Red = 1 };
inline constexpr int kTeamCount = 2;

enum class Mode : std::uint8_t {
  // Solo or co-op: every player is Blue, against an AI base, level after level.
  Campaign = 0,
  // Blue against Red on a mirrored arena; empty seats can be bots.
  Versus = 1,
};

[[nodiscard]] constexpr int team_index(Team team) { return static_cast<int>(team); }
[[nodiscard]] constexpr Team other_team(Team team) {
  return team == Team::Blue ? Team::Red : Team::Blue;
}
// +1 for Blue (marches up the field), -1 for Red.
[[nodiscard]] constexpr double team_direction(Team team) {
  return team == Team::Blue ? 1.0 : -1.0;
}
[[nodiscard]] constexpr double cannon_y(Team team) {
  return team == Team::Blue ? kCannonOffset : kFieldLength - kCannonOffset;
}
[[nodiscard]] constexpr std::uint8_t team_bit(Team team) {
  return static_cast<std::uint8_t>(1U << team_index(team));
}

}  // namespace mob_survivor
