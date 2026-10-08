// Writes a real snapshot and what it should decode to, for the web client's
// decoder test (tests/web_snapshot_test.mjs).
//
// usage: snapshot_fixture <output-dir>   (writes snapshot.bin and expected.json)

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "mob_survivor/match.hpp"
#include "net/json.hpp"
#include "net/protocol.hpp"

using namespace mob_survivor;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: snapshot_fixture <output-dir>\n");
    return 2;
  }
  const std::string dir = argv[1];
  // A bot-against-bot versus round, played until its snapshot has a power-up,
  // a team effect and phasing mobs in it, so the decoder test sees every part
  // of the layout. The first seed that gets there, so balance changes do not
  // break the fixture.
  auto interesting = [](const World& world) {
    const auto active = [&](Team team) {
      const TeamEffects& effects = world.effects(team);
      return effects.shield > 0 || effects.frozen > 0.0 || effects.flipped > 0.0;
    };
    return !world.powerups().empty() && (active(Team::Blue) || active(Team::Red)) &&
           std::any_of(world.mobs().begin(), world.mobs().end(), [&](const Mob& mob) { return world.phased(mob); });
  };
  std::unique_ptr<Match> found;
  for (std::uint64_t seed = 1; seed <= 200 && !found; ++seed) {
    auto match = std::make_unique<Match>(Mode::Versus, seed, std::vector<SeatSpec>{{0, Team::Blue, true}, {1, Team::Red, true}});
    for (int i = 0; i < 30 * 20; ++i) match->step(kTickSeconds);
    for (int i = 0; i < 30 * 200 && match->phase() == Phase::Playing; ++i) {
      if (interesting(match->world())) {
        found = std::move(match);
        break;
      }
      match->step(kTickSeconds);
    }
  }
  if (!found) {
    std::fprintf(stderr, "snapshot_fixture: no seed shows every part of a snapshot\n");
    return 1;
  }
  Match& match = *found;
  const auto events = match.take_events();
  const std::string bytes = net::encode_snapshot(match, events);

  const World& world = match.world();
  net::JsonWriter w;
  w.begin_object();
  w.field("size", static_cast<int>(bytes.size()));
  w.field("phase", static_cast<int>(match.phase()));
  w.field("level", match.level());
  w.field("tick", static_cast<std::int64_t>(match.ticks()));
  w.field("blueHp", world.base(Team::Blue).hp).field("redMax", world.base(Team::Red).max_hp);
  w.field("cannons", static_cast<int>(world.cannons().size()));
  w.field("gates", static_cast<int>(world.level().gates.size()));
  w.field("events", static_cast<int>(events.size()));
  w.field("powerups", static_cast<int>(world.powerups().size()));
  if (!world.powerups().empty()) {
    const PowerUp& first = world.powerups().front();
    w.key("firstPowerUp").begin_object();
    w.field("id", first.id).field("kind", static_cast<int>(first.kind)).field("x", first.position.x).field("y", first.position.y);
    w.field("hp", first.hp).field("max", first.max_hp);
    w.end_object();
  }
  w.field("volley", world.cannons().front().shots_per_volley);
  for (int t = 0; t < kTeamCount; ++t) {
    const TeamEffects& effects = world.effects(static_cast<Team>(t));
    w.key(t == 0 ? "blueEffects" : "redEffects").begin_object();
    w.field("shield", effects.shield).field("frozen", effects.frozen).field("flipped", effects.flipped);
    w.end_object();
  }
  int phasing = 0;
  for (const Cannon& cannon : world.cannons()) phasing += cannon.phase_time > 0.0 ? 1 : 0;
  w.field("phasingCannons", phasing);
  w.field("phasedMobs", static_cast<int>(std::count_if(world.mobs().begin(), world.mobs().end(),
                                                       [&](const Mob& mob) { return world.phased(mob); })));
  w.field("mobs", static_cast<int>(world.mobs().size()));
  if (!world.mobs().empty()) {
    const Mob& last = world.mobs().back();
    w.key("lastMob").begin_object();
    w.field("id", last.id).field("x", last.position.x).field("y", last.position.y);
    w.field("team", team_index(last.team)).field("kind", static_cast<int>(last.kind)).field("hp", last.hp);
    w.end_object();
  }
  w.end_object();

  std::ofstream(dir + "/snapshot.bin", std::ios::binary) << bytes;
  std::ofstream(dir + "/expected.json") << w.str();
  return 0;
}
