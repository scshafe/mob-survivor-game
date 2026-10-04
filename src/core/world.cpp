#include "mob_survivor/world.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mob_survivor {

namespace {

constexpr double kGruntRadius = 0.3;
constexpr double kRunnerRadius = 0.26;
constexpr double kSquadRadiusCap = 0.7;
constexpr double kGiantRadius = 0.9;
constexpr double kBruteRadius = 1.5;

// The contact grid: cells at least as wide as two of the largest small mobs,
// so a small mob only ever touches small mobs in its own or adjacent cells.
constexpr double kCell = 1.5;
constexpr int kGridCols = static_cast<int>(kFieldWidth / kCell) + 1;
constexpr int kGridRows = static_cast<int>(kFieldLength / kCell) + 2;

constexpr std::size_t kMaxEvents = 512;
constexpr double kDriftDamping = 5.0;
constexpr double kPushStrength = 180.0;  // sideways shove per unit of overlap, 1/s
constexpr int kMaxClonesPerPass = 12;

bool is_big(MobKind kind) { return kind == MobKind::Giant || kind == MobKind::Brute; }

int damage_scale(MobKind kind) {
  switch (kind) {
    case MobKind::Brute:
      return 2;
    case MobKind::Giant:
    case MobKind::Grunt:
    case MobKind::Runner:
      break;
  }
  return 1;
}

int grid_cell(const Vec2& p) {
  const int cx = std::clamp(static_cast<int>(p.x / kCell), 0, kGridCols - 1);
  const int cy = std::clamp(static_cast<int>(p.y / kCell), 0, kGridRows - 1);
  return cy * kGridCols + cx;
}

}  // namespace

double mob_radius(const Mob& mob) {
  switch (mob.kind) {
    case MobKind::Grunt:
      return std::min(kSquadRadiusCap, kGruntRadius * std::sqrt(static_cast<double>(std::max(mob.hp, 1))));
    case MobKind::Runner:
      return std::min(kSquadRadiusCap, kRunnerRadius * std::sqrt(static_cast<double>(std::max(mob.hp, 1))));
    case MobKind::Giant:
      return kGiantRadius;
    case MobKind::Brute:
      return kBruteRadius;
  }
  return kGruntRadius;
}

double mob_speed(MobKind kind) {
  switch (kind) {
    case MobKind::Grunt:
      return 4.6;
    case MobKind::Runner:
      return 6.8;
    case MobKind::Giant:
      return 2.9;
    case MobKind::Brute:
      return 1.9;
  }
  return 4.6;
}

World::World(WorldConfig config) : level_(std::move(config.level)), rng_(config.seed) {
  if (level_.gates.size() > kMaxGates) level_.gates.resize(kMaxGates);
  for (int t = 0; t < kTeamCount; ++t) {
    const auto team = static_cast<Team>(t);
    const int hp = std::max(level_.base_hp.at(static_cast<std::size_t>(t)), 1);
    bases_.at(static_cast<std::size_t>(t)) = Base{team, hp, hp};
  }
  for (int t = 0; t < kTeamCount; ++t) {
    const auto team = static_cast<Team>(t);
    const auto on_team = std::count_if(config.cannons.begin(), config.cannons.end(),
                                       [team](const CannonSeat& seat) { return seat.team == team; });
    int placed = 0;
    for (const CannonSeat& seat : config.cannons) {
      if (seat.team != team) continue;
      Cannon cannon;
      cannon.slot = seat.slot;
      cannon.team = seat.team;
      cannon.stats = seat.stats;
      cannon.x = kFieldWidth * (placed + 1) / static_cast<double>(on_team + 1);
      cannon.target_x = cannon.x;
      cannons_.push_back(cannon);
      tallies_.emplace_back(seat.slot, Tally{});
      ++placed;
    }
  }
  gate_gain_.assign(level_.gates.size() * kTeamCount, 0);
  wave_timer_ = level_.ai.first_wave;
  boss_timer_ = level_.ai.first_boss;
}

Cannon* World::find_cannon(int slot) {
  for (Cannon& cannon : cannons_) {
    if (cannon.slot == slot) return &cannon;
  }
  return nullptr;
}

const Cannon* World::cannon(int slot) const {
  for (const Cannon& cannon : cannons_) {
    if (cannon.slot == slot) return &cannon;
  }
  return nullptr;
}

void World::set_input(int slot, double target_x, bool firing) {
  if (Cannon* cannon = find_cannon(slot)) {
    if (std::isfinite(target_x)) cannon->target_x = std::clamp(target_x, 0.0, kFieldWidth);
    cannon->firing = firing;
  }
}

void World::request_giant(int slot) {
  if (Cannon* cannon = find_cannon(slot)) cannon->giant_requested = true;
}

void World::request_bomb(int slot, Vec2 target) {
  if (!std::isfinite(target.x) || !std::isfinite(target.y)) return;
  if (Cannon* cannon = find_cannon(slot)) cannon->bomb_request = target;
}

void World::set_connected(int slot, bool connected) {
  if (Cannon* cannon = find_cannon(slot)) {
    cannon->connected = connected;
    if (!connected) cannon->firing = false;
  }
}

double World::gate_x(std::size_t index) const { return gate_x_at(level_.gates.at(index), elapsed_); }

double World::saw_x(std::size_t index) const { return saw_x_at(level_.saws.at(index), elapsed_); }

std::size_t World::team_mob_count(Team team) const {
  return static_cast<std::size_t>(
      std::count_if(mobs_.begin(), mobs_.end(), [team](const Mob& mob) { return mob.team == team; }));
}

Tally World::tally(int slot) const {
  for (const auto& [owner, tally] : tallies_) {
    if (owner == slot) return tally;
  }
  return {};
}

Tally& World::tally_for(int slot) {
  for (auto& [owner, tally] : tallies_) {
    if (owner == slot) return tally;
  }
  // The AI's mobs (slot -1) count into a tally nobody reads.
  unowned_tally_ = {};
  return unowned_tally_;
}

std::vector<Event> World::take_events() { return std::exchange(events_, {}); }

void World::emit(Event event) {
  if (events_.size() < kMaxEvents) events_.push_back(event);
}

std::uint32_t World::spawn(Mob mob) {
  mob.id = next_id_++;
  mobs_.push_back(mob);
  return mob.id;
}

bool World::add_mob(Mob mob) {
  if (team_mob_count(mob.team) >= kMaxMobsPerTeam) return false;
  spawn(mob);
  return true;
}

void World::step(double dt) {
  ++ticks_;
  if (outcome_ != Outcome::None || dt <= 0.0) return;
  elapsed_ += dt;
  if (level_.frenzy_at > 0.0 && !frenzy_ && elapsed_ >= level_.frenzy_at) {
    frenzy_ = true;
    emit({EventType::Frenzy, Team::Blue, {kFieldWidth / 2.0, kFieldLength / 2.0}, 0, -1});
  }
  step_cannons(dt);
  step_ai(dt);
  step_bombs(dt);
  step_mobs(dt);
  resolve_contacts(dt);
  std::erase_if(mobs_, [](const Mob& mob) { return mob.hp <= 0; });

  for (std::size_t g = 0; g < level_.gates.size(); ++g) {
    for (int t = 0; t < kTeamCount; ++t) {
      int& gain = gate_gain_[g * kTeamCount + static_cast<std::size_t>(t)];
      if (gain != 0) {
        const Gate& gate = level_.gates[g];
        emit({EventType::GatePass, static_cast<Team>(t), {gate_x(g), gate.y}, gain, static_cast<int>(g)});
        gain = 0;
      }
    }
  }
  settle_outcome();
}

void World::step_cannons(double dt) {
  for (Cannon& cannon : cannons_) {
    const double dir = team_direction(cannon.team);
    const double y = cannon_y(cannon.team);
    const double target = std::clamp(cannon.target_x, 0.6, kFieldWidth - 0.6);
    const double reach = kCannonSpeed * dt;
    cannon.x += std::clamp(target - cannon.x, -reach, reach);
    cannon.bomb_cooldown = std::max(0.0, cannon.bomb_cooldown - dt);
    Tally& tally = tally_for(cannon.slot);

    if (cannon.giant_requested) {
      cannon.giant_requested = false;
      if (cannon.charge >= 1.0) {
        Mob giant;
        giant.team = cannon.team;
        giant.kind = MobKind::Giant;
        giant.owner = cannon.slot;
        giant.position = {cannon.x, y + dir * 1.0};
        giant.hp = static_cast<int>(std::lround(15.0 * cannon.stats.giant_hp_scale));
        // A giant always gets onto the field, even over the mob cap.
        spawn(giant);
        cannon.charge = 0.0;
        ++tally.giants;
        emit({EventType::GiantLaunch, cannon.team, giant.position, giant.hp, cannon.slot});
      }
    }

    if (cannon.bomb_request) {
      if (cannon.bomb_cooldown <= 0.0 && cannon.connected) {
        Bomb bomb;
        bomb.team = cannon.team;
        bomb.owner = cannon.slot;
        bomb.from = {cannon.x, y};
        bomb.target = {std::clamp(cannon.bomb_request->x, 0.0, kFieldWidth),
                       std::clamp(cannon.bomb_request->y, 0.0, kFieldLength)};
        bomb.radius = kBombRadius * cannon.stats.bomb_radius_scale;
        bombs_.push_back(bomb);
        cannon.bomb_cooldown = kBombCooldown * cannon.stats.bomb_cooldown_scale;
        ++tally.bombs;
      }
      cannon.bomb_request.reset();
    }

    if (!cannon.connected) cannon.firing = false;
    if (!cannon.firing) {
      cannon.fire_timer = std::max(0.0, cannon.fire_timer - dt);
      continue;
    }
    cannon.fire_timer -= dt;
    while (cannon.fire_timer <= 0.0) {
      const int shots = cannon.stats.shots_per_volley;
      for (int s = 0; s < shots; ++s) {
        Mob mob;
        mob.team = cannon.team;
        mob.owner = cannon.slot;
        const double offset = (s - (shots - 1) / 2.0) * 0.6 + rng_.uniform(-0.25, 0.25);
        mob.position = {std::clamp(cannon.x + offset, 0.3, kFieldWidth - 0.3), y + dir * 0.7};
        if (add_mob(mob)) ++tally.shots;
      }
      cannon.charge = std::min(1.0, cannon.charge + cannon.stats.charge_rate / kShotsPerGiant);
      cannon.fire_timer += cannon.stats.fire_interval;
    }
  }
}

void World::step_ai(double dt) {
  const AiSpec& ai = level_.ai;
  if (!ai.enabled) return;
  const double front = kFieldLength - kBaseDepth;

  wave_timer_ -= dt;
  if (wave_timer_ <= 0.0) {
    wave_timer_ += ai.wave_interval * rng_.uniform(0.85, 1.15);
    ++wave_index_;
    const int size = ai.wave_size + static_cast<int>(ai.wave_growth * (wave_index_ - 1));
    const double centre = rng_.uniform(3.5, kFieldWidth - 3.5);
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(size * 1.6))));
    for (int i = 0; i < size; ++i) {
      Mob mob;
      mob.team = Team::Red;
      mob.kind = rng_.chance(ai.runner_percent / 100.0) ? MobKind::Runner : MobKind::Grunt;
      const int row = i / cols;
      const int col = i % cols;
      mob.position.x = std::clamp(centre + (col - (cols - 1) / 2.0) * 0.7 + rng_.uniform(-0.15, 0.15), 0.4,
                                  kFieldWidth - 0.4);
      mob.position.y = front + 0.5 + row * 0.7 + rng_.uniform(0.0, 0.2);
      add_mob(mob);
    }
    if (ai.giant_every > 0 && wave_index_ % ai.giant_every == 0) {
      Mob giant;
      giant.team = Team::Red;
      giant.kind = MobKind::Giant;
      giant.hp = ai.giant_hp;
      giant.position = {centre, front + 1.2};
      spawn(giant);
    }
  }

  if (ai.boss_interval > 0.0) {
    boss_timer_ -= dt;
    if (boss_timer_ <= 0.0) {
      boss_timer_ += ai.boss_interval;
      Mob brute;
      brute.team = Team::Red;
      brute.kind = MobKind::Brute;
      brute.hp = ai.boss_hp;
      brute.position = {rng_.uniform(6.0, kFieldWidth - 6.0), front + 1.0};
      spawn(brute);
      emit({EventType::BossSpawn, Team::Red, brute.position, brute.hp, -1});
    }
  }
}

void World::step_bombs(double dt) {
  for (Bomb& bomb : bombs_) {
    bomb.fuse -= dt;
    if (bomb.fuse > 0.0) continue;
    int destroyed = 0;
    for (Mob& mob : mobs_) {
      if (mob.team == bomb.team || mob.hp <= 0) continue;
      const double reach = bomb.radius + mob_radius(mob);
      const double dx = mob.position.x - bomb.target.x;
      const double dy = mob.position.y - bomb.target.y;
      if (dx * dx + dy * dy > reach * reach) continue;
      const int damage = std::min(mob.hp, bomb.damage);
      mob.hp -= damage;
      destroyed += damage;
    }
    tally_for(bomb.owner).kills += destroyed;
    emit({EventType::BombBlast, bomb.team, bomb.target, destroyed, bomb.owner});
  }
  std::erase_if(bombs_, [](const Bomb& bomb) { return bomb.fuse <= 0.0; });
}

void World::apply_gate(std::size_t gate_index, Mob& mob, std::vector<Mob>& born) {
  const Gate& gate = level_.gates[gate_index];
  mob.gates_passed |= 1U << gate_index;
  const Cannon* owner = mob.owner >= 0 ? cannon(mob.owner) : nullptr;
  const PlayerStats defaults;
  const PlayerStats& stats = owner != nullptr ? owner->stats : defaults;
  int& gain = gate_gain_[gate_index * kTeamCount + static_cast<std::size_t>(team_index(mob.team))];
  const bool big = is_big(mob.kind);

  int extra = 0;
  switch (gate.op) {
    case GateOp::Add: {
      extra = gate.value + stats.add_gate_bonus;
      if (frenzy_) extra += extra / 2;
      break;
    }
    case GateOp::Mul: {
      const int factor = gate.value + stats.mul_gate_bonus + (frenzy_ ? 1 : 0);
      extra = (factor - 1) * (big ? 3 : mob.hp);
      break;
    }
    case GateOp::Half: {
      if (stats.halve_immune) return;
      int lost = mob.hp / 2;
      if (mob.hp % 2 == 1 && rng_.chance(0.5)) ++lost;
      mob.hp -= lost;
      gain -= lost;
      return;
    }
  }
  if (extra <= 0) return;

  // Respect the cap: whatever does not fit on the field is lost.
  std::size_t on_field = team_mob_count(mob.team);
  for (const Mob& baby : born) {
    if (baby.team == mob.team) ++on_field;
  }
  const int room = static_cast<int>(kMaxMobsPerTeam - std::min(on_field, kMaxMobsPerTeam));
  const int bodies = std::min({extra, room, kMaxClonesPerPass});
  if (bodies <= 0) return;
  // Above kMaxClonesPerPass the clones come as squads, so the total holds.
  const int total = std::min(extra, room <= kMaxClonesPerPass ? room : extra);
  const double dir = team_direction(mob.team);
  for (int i = 0; i < bodies; ++i) {
    Mob clone;
    clone.team = mob.team;
    clone.kind = mob.kind == MobKind::Runner ? MobKind::Runner : MobKind::Grunt;
    clone.owner = mob.owner;
    clone.gates_passed = mob.gates_passed;
    clone.hp = total / bodies + (i < total % bodies ? 1 : 0);
    clone.position.x = std::clamp(mob.position.x + rng_.uniform(-0.9, 0.9), 0.3, kFieldWidth - 0.3);
    clone.position.y = gate.y + dir * rng_.uniform(0.05, 0.8);
    clone.drift = rng_.uniform(-1.6, 1.6);
    born.push_back(clone);
  }
  gain += total;
  tally_for(mob.owner).gate_mobs += total;
}

void World::step_mobs(double dt) {
  std::vector<Mob> born;
  const double damping = std::exp(-kDriftDamping * dt);
  for (Mob& mob : mobs_) {
    if (mob.hp <= 0) continue;
    const double dir = team_direction(mob.team);
    const Cannon* owner = mob.owner >= 0 ? cannon(mob.owner) : nullptr;
    const double speed = mob_speed(mob.kind) * (owner != nullptr ? owner->stats.speed_scale : 1.0);
    const double previous_y = mob.position.y;
    const double radius = mob_radius(mob);
    mob.position.y += dir * speed * dt;
    mob.drift *= damping;
    mob.position.x = std::clamp(mob.position.x + mob.drift * dt, radius, kFieldWidth - radius);
    mob.saw_cooldown = std::max(0.0, mob.saw_cooldown - dt);

    for (std::size_t g = 0; g < level_.gates.size(); ++g) {
      const Gate& gate = level_.gates[g];
      if ((gate.teams & team_bit(mob.team)) == 0 || ((mob.gates_passed >> g) & 1U) != 0) continue;
      const bool crossed = dir > 0.0 ? (previous_y < gate.y && mob.position.y >= gate.y)
                                     : (previous_y > gate.y && mob.position.y <= gate.y);
      if (!crossed) continue;
      if (std::fabs(mob.position.x - gate_x(g)) > gate.width / 2.0 + 0.15) continue;
      apply_gate(g, mob, born);
      if (mob.hp <= 0) break;
    }
    if (mob.hp <= 0) continue;

    for (std::size_t s = 0; s < level_.saws.size(); ++s) {
      const Saw& saw = level_.saws[s];
      const double sx = saw_x(s);
      const double dx = mob.position.x - sx;
      const double dy = mob.position.y - saw.y;
      const double reach = saw.radius + radius;
      if (mob.saw_cooldown > 0.0 || dx * dx + dy * dy > reach * reach) continue;
      mob.hp -= is_big(mob.kind) ? 2 : 1;
      mob.saw_cooldown = 0.25;
      mob.drift += dx >= 0.0 ? 3.0 : -3.0;
      emit({EventType::SawCut, mob.team, mob.position, 1, static_cast<int>(s)});
    }
    if (mob.hp <= 0) continue;

    const bool reached = mob.team == Team::Blue ? mob.position.y >= kFieldLength - kBaseDepth
                                                : mob.position.y <= kBaseDepth;
    if (reached) {
      const Team target = other_team(mob.team);
      Base& base = bases_.at(static_cast<std::size_t>(team_index(target)));
      const int damage = mob.hp * damage_scale(mob.kind);
      base.hp = std::max(0, base.hp - damage);
      tally_for(mob.owner).base_damage += damage;
      emit({EventType::BaseHit, target, mob.position, damage, mob.owner});
      mob.hp = 0;
    }
  }
  for (Mob& baby : born) spawn(baby);
}

void World::resolve_contacts(double dt) {
  const auto count = mobs_.size();
  constexpr int kCells = kGridCols * kGridRows;
  cell_of_.assign(count, -1);
  cell_start_.assign(kCells + 1, 0);
  std::vector<std::size_t> bigs;
  for (std::size_t i = 0; i < count; ++i) {
    const Mob& mob = mobs_[i];
    if (mob.hp <= 0) continue;
    if (is_big(mob.kind)) {
      bigs.push_back(i);
      continue;
    }
    cell_of_[i] = grid_cell(mob.position);
    ++cell_start_[static_cast<std::size_t>(cell_of_[i]) + 1];
  }
  for (int c = 0; c < kCells; ++c) cell_start_[c + 1] += cell_start_[c];
  cell_items_.assign(static_cast<std::size_t>(cell_start_[kCells]), 0);
  {
    std::vector<int> fill(cell_start_.begin(), cell_start_.end() - 1);
    for (std::size_t i = 0; i < count; ++i) {
      if (cell_of_[i] < 0) continue;
      cell_items_[static_cast<std::size_t>(fill[static_cast<std::size_t>(cell_of_[i])]++)] = static_cast<int>(i);
    }
  }

  const double push = kPushStrength * dt;
  auto touch = [&](std::size_t a, std::size_t b) {
    Mob& first = mobs_[a];
    Mob& second = mobs_[b];
    if (first.hp <= 0 || second.hp <= 0) return;
    const double dx = second.position.x - first.position.x;
    const double dy = second.position.y - first.position.y;
    const double reach = mob_radius(first) + mob_radius(second);
    const double distance_sq = dx * dx + dy * dy;
    if (distance_sq >= reach * reach) return;
    if (first.team != second.team) {
      const int trade = std::min(first.hp, second.hp);
      first.hp -= trade;
      second.hp -= trade;
      tally_for(first.owner).kills += trade;
      tally_for(second.owner).kills += trade;
      return;
    }
    // Same team: shoulder each other sideways so the crowd spreads out.
    const double distance = std::sqrt(distance_sq);
    double nx = distance > 1e-6 ? dx / distance : 0.0;
    if (std::fabs(nx) < 0.05) nx = ((first.id ^ second.id) & 1U) != 0 ? 0.5 : -0.5;
    const double overlap = reach - distance;
    const bool first_big = is_big(first.kind);
    const bool second_big = is_big(second.kind);
    const double share_first = first_big && !second_big ? 0.0 : (second_big && !first_big ? 1.0 : 0.5);
    first.drift -= nx * overlap * push * share_first;
    second.drift += nx * overlap * push * (1.0 - share_first);
  };

  constexpr std::array<std::pair<int, int>, 4> kForward{{{1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
  for (int cy = 0; cy < kGridRows; ++cy) {
    for (int cx = 0; cx < kGridCols; ++cx) {
      const int cell = cy * kGridCols + cx;
      const int begin = cell_start_[static_cast<std::size_t>(cell)];
      const int end = cell_start_[static_cast<std::size_t>(cell) + 1];
      for (int a = begin; a < end; ++a) {
        const auto i = static_cast<std::size_t>(cell_items_[static_cast<std::size_t>(a)]);
        for (int b = a + 1; b < end; ++b) touch(i, static_cast<std::size_t>(cell_items_[static_cast<std::size_t>(b)]));
        for (const auto& [ox, oy] : kForward) {
          const int nx = cx + ox;
          const int ny = cy + oy;
          if (nx < 0 || nx >= kGridCols || ny >= kGridRows) continue;
          const int other = ny * kGridCols + nx;
          const int obegin = cell_start_[static_cast<std::size_t>(other)];
          const int oend = cell_start_[static_cast<std::size_t>(other) + 1];
          for (int b = obegin; b < oend; ++b) touch(i, static_cast<std::size_t>(cell_items_[static_cast<std::size_t>(b)]));
        }
      }
    }
  }

  // Giants and brutes are few: check them against everything.
  for (std::size_t big : bigs) {
    for (std::size_t j = 0; j < count; ++j) {
      if (j == big) continue;
      // Two bigs meet once, from the lower index.
      if (is_big(mobs_[j].kind) && j < big) continue;
      touch(big, j);
    }
  }
}

void World::settle_outcome() {
  const bool blue_down = bases_[0].hp <= 0;
  const bool red_down = bases_[1].hp <= 0;
  if (blue_down && red_down) {
    outcome_ = Outcome::Draw;
  } else if (red_down) {
    outcome_ = Outcome::BlueWins;
  } else if (blue_down) {
    outcome_ = Outcome::RedWins;
  } else if (level_.time_limit > 0.0 && elapsed_ >= level_.time_limit) {
    const double blue = static_cast<double>(bases_[0].hp) / bases_[0].max_hp;
    const double red = static_cast<double>(bases_[1].hp) / bases_[1].max_hp;
    if (std::fabs(blue - red) < 1e-9) {
      outcome_ = Outcome::Draw;
    } else {
      outcome_ = blue > red ? Outcome::BlueWins : Outcome::RedWins;
    }
  }
}

}  // namespace mob_survivor
