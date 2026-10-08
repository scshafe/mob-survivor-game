// Writes real snapshots and what they should decode to, for the web client's
// decoder test (tests/web_snapshot_test.mjs): one for each feature of the
// layout, each taken the first time a bot-against-bot versus round shows it,
// so the test reads every part with something in it and balance changes do
// not leave a part empty.
//
// usage: snapshot_fixture <output-dir>   (writes snapshot-<feature>.bin and expected.json)

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>

#include "mob_survivor/match.hpp"
#include "net/json.hpp"
#include "net/protocol.hpp"

using namespace mob_survivor;

namespace {

bool any_mob(const World& world, bool (*test)(const World&, const Mob&)) {
  return std::any_of(world.mobs().begin(), world.mobs().end(), [&](const Mob& mob) { return test(world, mob); });
}

struct Feature {
  const char* name;
  bool (*shows)(const World&);
};

constexpr std::array<Feature, 5> kFeatures{{
    {"powerup", [](const World& world) { return !world.powerups().empty(); }},
    {"effect",
     [](const World& world) {
       for (int t = 0; t < kTeamCount; ++t) {
         const TeamEffects& effects = world.effects(static_cast<Team>(t));
         if (effects.shield > 0 || effects.frozen > 0.0 || effects.flipped > 0.0) return true;
       }
       return false;
     }},
    {"phased", [](const World& world) { return any_mob(world, [](const World& w, const Mob& m) { return w.phased(m); }); }},
    {"armored", [](const World& world) { return any_mob(world, [](const World&, const Mob& m) { return m.armored; }); }},
    {"fuse",
     [](const World& world) {
       for (std::size_t g = 0; g < world.level().gates.size(); ++g) {
         if (world.fuse_fill(g, Team::Blue) > 0 || world.fuse_fill(g, Team::Red) > 0) return true;
       }
       return false;
     }},
}};

int count_mobs(const World& world, bool (*test)(const World&, const Mob&)) {
  return static_cast<int>(
      std::count_if(world.mobs().begin(), world.mobs().end(), [&](const Mob& mob) { return test(world, mob); }));
}

void describe(net::JsonWriter& w, const Match& match, const std::vector<Event>& events, const std::string& bytes) {
  const World& world = match.world();
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
  w.field("armoredMobs", count_mobs(world, [](const World&, const Mob& mob) { return mob.armored; }));
  w.key("fuseFill").begin_array();
  for (std::size_t g = 0; g < world.level().gates.size(); ++g) {
    w.begin_array().value(world.fuse_fill(g, Team::Blue)).value(world.fuse_fill(g, Team::Red)).end_array();
  }
  w.end_array();
  int phasing = 0;
  for (const Cannon& cannon : world.cannons()) phasing += cannon.phase_time > 0.0 ? 1 : 0;
  w.field("phasingCannons", phasing);
  w.field("phasedMobs", count_mobs(world, [](const World& w2, const Mob& mob) { return w2.phased(mob); }));
  w.field("mobs", static_cast<int>(world.mobs().size()));
  if (!world.mobs().empty()) {
    const Mob& last = world.mobs().back();
    w.key("lastMob").begin_object();
    w.field("id", last.id).field("x", last.position.x).field("y", last.position.y);
    w.field("team", team_index(last.team)).field("kind", static_cast<int>(last.kind)).field("hp", last.hp);
    w.end_object();
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: snapshot_fixture <output-dir>\n");
    return 2;
  }
  const std::string dir = argv[1];
  std::array<bool, kFeatures.size()> captured{};
  net::JsonWriter w;
  w.begin_object();
  w.key("cases").begin_array();
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    if (std::all_of(captured.begin(), captured.end(), [](bool done) { return done; })) break;
    Match match(Mode::Versus, seed, {{0, Team::Blue, true}, {1, Team::Red, true}});
    for (int i = 0; i < 30 * 300 && match.phase() != Phase::GameOver; ++i) {
      match.step(kTickSeconds);
      const auto events = match.take_events();
      if (match.phase() != Phase::Playing) continue;
      for (std::size_t f = 0; f < kFeatures.size(); ++f) {
        if (captured.at(f) || !kFeatures.at(f).shows(match.world())) continue;
        captured.at(f) = true;
        const std::string bytes = net::encode_snapshot(match, events);
        const std::string file = std::string("snapshot-") + kFeatures.at(f).name + ".bin";
        std::ofstream(dir + "/" + file, std::ios::binary) << bytes;
        w.begin_object();
        w.field("feature", kFeatures.at(f).name).field("file", file);
        describe(w, match, events, bytes);
        w.end_object();
      }
    }
  }
  w.end_array();
  w.end_object();
  for (std::size_t f = 0; f < kFeatures.size(); ++f) {
    if (!captured.at(f)) {
      std::fprintf(stderr, "snapshot_fixture: no seed shows %s\n", kFeatures.at(f).name);
      return 1;
    }
  }
  std::ofstream(dir + "/expected.json") << w.str();
  return 0;
}
