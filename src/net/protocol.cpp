#include "net/protocol.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "net/json.hpp"

namespace mob_survivor::net {

namespace {

// Little-endian byte writer.
class Bytes {
 public:
  void u8(unsigned value) { out_.push_back(static_cast<char>(value & 0xFFU)); }
  void u16(unsigned value) {
    u8(value);
    u8(value >> 8U);
  }
  void u32(std::uint32_t value) {
    u16(value & 0xFFFFU);
    u16(value >> 16U);
  }
  void i16(int value) { u16(static_cast<unsigned>(static_cast<std::uint16_t>(std::clamp(value, -32768, 32767)))); }
  void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
  void f32(double value) { u32(std::bit_cast<std::uint32_t>(static_cast<float>(value))); }
  // A world coordinate in hundredths of a unit.
  void coord(double value) { i16(static_cast<int>(std::lround(value * 100.0))); }

  std::string take() { return std::move(out_); }

 private:
  std::string out_;
};

}  // namespace

std::string_view mode_name(Mode mode) { return mode == Mode::Versus ? "versus" : "campaign"; }

std::optional<Mode> parse_mode(std::string_view name) {
  if (name == "campaign") return Mode::Campaign;
  if (name == "versus") return Mode::Versus;
  return std::nullopt;
}

std::string_view phase_name(Phase phase) {
  switch (phase) {
    case Phase::Lobby:
      return "lobby";
    case Phase::Countdown:
      return "countdown";
    case Phase::Playing:
      return "playing";
    case Phase::Upgrade:
      return "upgrade";
    case Phase::GameOver:
      return "over";
  }
  return "lobby";
}

std::string_view outcome_name(Outcome outcome) {
  switch (outcome) {
    case Outcome::BlueWins:
      return "blue";
    case Outcome::RedWins:
      return "red";
    case Outcome::Draw:
      return "draw";
    case Outcome::None:
      break;
  }
  return "none";
}

std::string_view gate_op_name(GateOp op) {
  switch (op) {
    case GateOp::Add:
      return "add";
    case GateOp::Mul:
      return "mul";
    case GateOp::Half:
      return "half";
  }
  return "add";
}

std::string encode_snapshot(const Match& match, const std::vector<Event>& events) {
  const World& world = match.world();
  Bytes out;
  out.u8(kSnapshotKind);
  out.u8(static_cast<unsigned>(match.phase()));
  out.u16(static_cast<unsigned>(std::clamp(match.level(), 0, 65535)));
  out.u32(static_cast<std::uint32_t>(match.ticks()));
  out.f32(match.phase_time_left());
  out.f32(world.elapsed());
  out.u8((world.frenzy() ? 1U : 0U));
  out.u8(static_cast<unsigned>(world.outcome()));
  for (const Base& base : world.bases()) {
    out.i32(base.hp);
    out.i32(base.max_hp);
  }

  const auto& cannons = world.cannons();
  out.u8(static_cast<unsigned>(std::min<std::size_t>(cannons.size(), 255)));
  for (std::size_t i = 0; i < cannons.size() && i < 255; ++i) {
    const Cannon& cannon = cannons[i];
    out.u8(static_cast<unsigned>(cannon.slot));
    out.u8(static_cast<unsigned>(team_index(cannon.team)));
    out.coord(cannon.x);
    out.u8(static_cast<unsigned>(std::lround(std::clamp(cannon.charge, 0.0, 1.0) * 255.0)));
    out.u8(static_cast<unsigned>(std::lround(std::clamp(cannon.bomb_cooldown, 0.0, 25.5) * 10.0)));
    const bool phasing = cannon.phase_time > 0.0;
    const unsigned flags = (cannon.connected ? 1U : 0U) | (cannon.firing ? 2U : 0U) | (cannon.charge >= 1.0 ? 4U : 0U) |
                           (phasing ? 8U : 0U);
    out.u8(flags);
    out.u8(static_cast<unsigned>(std::clamp(cannon.shots_per_volley, 0, 255)));
    // While phasing: the time left; otherwise how far it has recharged.
    const double meter = phasing ? cannon.phase_time / kPhaseSeconds : 1.0 - cannon.phase_cooldown / kPhaseCooldown;
    out.u8(static_cast<unsigned>(std::lround(std::clamp(meter, 0.0, 1.0) * 255.0)));
  }

  const auto& gates = world.level().gates;
  out.u8(static_cast<unsigned>(gates.size()));
  for (std::size_t g = 0; g < gates.size(); ++g) out.coord(world.gate_x(g));

  const auto& saws = world.level().saws;
  out.u8(static_cast<unsigned>(std::min<std::size_t>(saws.size(), 255)));
  for (std::size_t s = 0; s < saws.size() && s < 255; ++s) out.coord(world.saw_x(s));

  const auto& bombs = world.bombs();
  out.u8(static_cast<unsigned>(std::min<std::size_t>(bombs.size(), 255)));
  for (std::size_t b = 0; b < bombs.size() && b < 255; ++b) {
    const Bomb& bomb = bombs[b];
    out.u8(static_cast<unsigned>(team_index(bomb.team)));
    out.coord(bomb.from.x);
    out.coord(bomb.from.y);
    out.coord(bomb.target.x);
    out.coord(bomb.target.y);
    out.coord(bomb.radius);
    out.u8(static_cast<unsigned>(std::lround(std::clamp(bomb.fuse, 0.0, 2.55) * 100.0)));
  }

  const auto& powerups = world.powerups();
  out.u8(static_cast<unsigned>(std::min<std::size_t>(powerups.size(), 255)));
  for (std::size_t p = 0; p < powerups.size() && p < 255; ++p) {
    const PowerUp& powerup = powerups[p];
    out.u32(powerup.id);
    out.coord(powerup.position.x);
    out.coord(powerup.position.y);
    out.u16(static_cast<unsigned>(std::clamp(powerup.hp, 0, 65535)));
    out.u16(static_cast<unsigned>(std::clamp(powerup.max_hp, 0, 65535)));
  }

  const std::size_t event_count = std::min<std::size_t>(events.size(), 65535);
  out.u16(static_cast<unsigned>(event_count));
  for (std::size_t e = 0; e < event_count; ++e) {
    const Event& event = events[e];
    out.u8(static_cast<unsigned>(event.type));
    out.u8(static_cast<unsigned>(team_index(event.team)));
    out.i16(event.index);
    out.coord(event.position.x);
    out.coord(event.position.y);
    out.i32(event.value);
  }

  const auto& mobs = world.mobs();
  const std::size_t mob_count = std::min<std::size_t>(mobs.size(), 65535);
  out.u16(static_cast<unsigned>(mob_count));
  for (std::size_t m = 0; m < mob_count; ++m) {
    const Mob& mob = mobs[m];
    out.u32(mob.id);
    out.coord(mob.position.x);
    out.coord(mob.position.y);
    out.u8(static_cast<unsigned>(team_index(mob.team)) | (static_cast<unsigned>(mob.kind) << 1U) |
           (world.phased(mob) ? 16U : 0U));
    out.u16(static_cast<unsigned>(std::clamp(mob.hp, 0, 65535)));
  }
  return out.take();
}

std::string encode_level(const Match& match) {
  const World& world = match.world();
  const LevelSpec& level = world.level();
  JsonWriter w;
  w.begin_object();
  w.field("t", "level");
  w.field("serial", match.level_serial());
  w.field("level", level.number);
  w.field("mode", mode_name(level.mode));
  w.field("boss", level.boss);
  w.key("field").begin_object();
  w.field("w", kFieldWidth).field("h", kFieldLength).field("baseDepth", kBaseDepth).field("cannonOffset", kCannonOffset);
  w.field("powerUpRadius", kPowerUpRadius).field("maxVolley", kMaxShotsPerVolley);
  w.end_object();
  w.field("timeLimit", level.time_limit);
  w.field("frenzyAt", level.frenzy_at);
  w.key("gates").begin_array();
  for (const Gate& gate : level.gates) {
    w.begin_object();
    w.field("x", gate.x).field("y", gate.y).field("w", gate.width);
    w.field("op", gate_op_name(gate.op)).field("v", gate.value);
    w.field("teams", static_cast<int>(gate.teams));
    w.field("moving", gate.amplitude != 0.0);
    w.end_object();
  }
  w.end_array();
  w.key("saws").begin_array();
  for (const Saw& saw : level.saws) {
    w.begin_object().field("y", saw.y).field("r", saw.radius).end_object();
  }
  w.end_array();
  w.end_object();
  return w.take();
}

std::optional<std::string> encode_cards(const Match& match, int slot) {
  const MatchSeat* seat = match.seat(slot);
  if (seat == nullptr || match.phase() != Phase::Upgrade) return std::nullopt;
  JsonWriter w;
  w.begin_object();
  w.field("t", "cards");
  w.field("cleared", match.levels_cleared());
  w.field("picked", seat->picked);
  w.key("cards").begin_array();
  for (const CardId id : seat->offer) {
    const CardInfo& card = card_info(id);
    const int taken = seat->cards.at(static_cast<std::size_t>(id));
    w.begin_object();
    w.field("key", card.key).field("title", card.title).field("text", card.text).field("taken", taken);
    w.end_object();
  }
  w.end_array();
  w.end_object();
  return w.take();
}

}  // namespace mob_survivor::net
