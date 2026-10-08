#include "net/daily.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mob_survivor/rng.hpp"
#include "net/json.hpp"
#include "net/replay_file.hpp"

namespace mob_survivor::net {

namespace {

// Days since 1970-01-01 to a civil date, and back (Howard Hinnant's
// algorithms), so no time zone or locale is involved.
std::string civil_from_days(std::int64_t days) {
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const auto doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
  return buffer;
}

std::optional<std::int64_t> days_from_civil(std::string_view day) {
  int y = 0;
  unsigned m = 0;
  unsigned d = 0;
  if (day.size() != 10 || std::sscanf(std::string(day).c_str(), "%4d-%2u-%2u", &y, &m, &d) != 3) return std::nullopt;
  if (m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool better(const DailyBoard::Run& a, const DailyBoard::Run& b) {
  if (a.levels != b.levels) return a.levels > b.levels;
  if (a.kills != b.kills) return a.kills > b.kills;
  return a.when < b.when;
}

// Replay ids name files: only lowercase hex digits and dashes.
bool safe_id(std::string_view id) {
  return !id.empty() && id.size() <= 40 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
  });
}

std::string clean(std::string text) {
  std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
  return text;
}

}  // namespace

std::string utc_day(double unix_seconds) {
  return civil_from_days(static_cast<std::int64_t>(std::floor(unix_seconds / 86400.0)));
}

std::uint64_t daily_seed(std::string_view day) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;  // FNV-1a
  for (const char c : std::string("mob-survivor-daily:") + std::string(day)) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001b3ULL;
  }
  Rng rng(hash);
  return rng.next();
}

DailyBoard::DailyBoard(std::string dir) : dir_(std::move(dir)) { load(); }

std::vector<const DailyBoard::Run*> DailyBoard::day_runs(std::string_view day) const {
  std::vector<const Run*> runs;
  for (const Run& run : runs_) {
    if (run.day == day) runs.push_back(&run);
  }
  std::sort(runs.begin(), runs.end(), [](const Run* a, const Run* b) { return better(*a, *b); });
  return runs;
}

bool DailyBoard::has_played(std::string_view day, std::string_view name) const {
  return std::any_of(runs_.begin(), runs_.end(), [&](const Run& run) { return run.day == day && run.name == name; });
}

int DailyBoard::add(Run run, const Replay& replay) {
  run.name = clean(std::move(run.name));
  if (has_played(run.day, run.name) || !safe_id(run.replay)) return -1;
  const std::string id = run.replay;
  const std::string day = run.day;
  runs_.push_back(std::move(run));
  const auto ranked = day_runs(day);
  int place = -1;
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (ranked[i]->replay == id) place = static_cast<int>(i);
  }
  if (place >= 0 && static_cast<std::size_t>(place) < kShown) {
    replays_[id] = replay;
    save_replay(id, replay);
  }
  // Runs pushed off the shown board let go of their replays.
  for (std::size_t i = kShown; i < ranked.size(); ++i) {
    for (Run& other : runs_) {
      if (&other == ranked[i] && !other.replay.empty()) {
        drop_replay(other.replay);
        other.replay.clear();
      }
    }
  }
  save();
  return place;
}

const Replay* DailyBoard::replay(std::string_view id) const {
  const auto found = replays_.find(id);
  return found == replays_.end() ? nullptr : &found->second;
}

const DailyBoard::Run* DailyBoard::best(std::string_view day) const {
  const auto ranked = day_runs(day);
  return ranked.empty() ? nullptr : ranked.front();
}

const DailyBoard::Run* DailyBoard::run_with_replay(std::string_view id) const {
  for (const Run& run : runs_) {
    if (!run.replay.empty() && run.replay == id) return &run;
  }
  return nullptr;
}

void DailyBoard::prune(std::string_view today) {
  const auto now = days_from_civil(today);
  if (!now) return;
  const std::string oldest = civil_from_days(*now - static_cast<std::int64_t>(kDaysKept) + 1);
  bool changed = false;
  for (const Run& run : runs_) {
    if (run.day < oldest && !run.replay.empty()) drop_replay(run.replay);
  }
  const auto before = runs_.size();
  std::erase_if(runs_, [&](const Run& run) { return run.day < oldest; });
  changed = runs_.size() != before;
  if (changed) save();
}

std::string DailyBoard::to_json(std::string_view day, std::string_view name) const {
  JsonWriter w;
  w.begin_object();
  w.field("t", "dailyboard");
  w.field("day", day);
  w.field("played", has_played(day, name));
  w.key("entries").begin_array();
  const auto ranked = day_runs(day);
  for (std::size_t i = 0; i < ranked.size() && i < kShown; ++i) {
    const Run& run = *ranked[i];
    w.begin_object();
    w.field("name", run.name).field("levels", run.levels).field("kills", run.kills).field("replay", run.replay);
    w.end_object();
  }
  w.end_array();
  w.field("players", static_cast<int>(ranked.size()));
  w.end_object();
  return w.take();
}

void DailyBoard::load() {
  if (dir_.empty()) return;
  std::ifstream in(dir_ + "/daily.tsv");
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    Run run;
    std::string levels;
    std::string kills;
    std::string when;
    if (!std::getline(fields, run.day, '\t') || !std::getline(fields, levels, '\t') ||
        !std::getline(fields, kills, '\t') || !std::getline(fields, when, '\t') ||
        !std::getline(fields, run.replay, '\t') || !std::getline(fields, run.name)) {
      continue;
    }
    try {
      run.levels = std::stoi(levels);
      run.kills = std::stoi(kills);
      run.when = std::stoll(when);
    } catch (...) {
      continue;
    }
    if (!days_from_civil(run.day)) continue;
    if (!run.replay.empty()) {
      std::ifstream file(dir_ + "/replays/" + run.replay + ".txt");
      std::stringstream text;
      text << file.rdbuf();
      auto replay = safe_id(run.replay) ? decode_replay(text.str()) : std::nullopt;
      if (replay) {
        replays_[run.replay] = std::move(*replay);
      } else {
        run.replay.clear();
      }
    }
    runs_.push_back(std::move(run));
  }
}

void DailyBoard::save() const {
  if (dir_.empty()) return;
  const std::string path = dir_ + "/daily.tsv";
  {
    std::ofstream out(path + ".tmp");
    for (const Run& run : runs_) {
      out << run.day << '\t' << run.levels << '\t' << run.kills << '\t' << run.when << '\t' << run.replay << '\t'
          << run.name << '\n';
    }
  }
  std::error_code error;
  std::filesystem::rename(path + ".tmp", path, error);
}

void DailyBoard::save_replay(const std::string& id, const Replay& replay) const {
  if (dir_.empty()) return;
  std::error_code error;
  std::filesystem::create_directories(dir_ + "/replays", error);
  const std::string path = dir_ + "/replays/" + id + ".txt";
  {
    std::ofstream out(path + ".tmp");
    out << encode_replay(replay);
  }
  std::filesystem::rename(path + ".tmp", path, error);
}

void DailyBoard::drop_replay(const std::string& id) {
  replays_.erase(id);
  if (dir_.empty() || !safe_id(id)) return;
  std::error_code error;
  std::filesystem::remove(dir_ + "/replays/" + id + ".txt", error);
}

}  // namespace mob_survivor::net
