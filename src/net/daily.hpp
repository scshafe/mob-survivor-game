#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mob_survivor/replay.hpp"

namespace mob_survivor::net {

// The UTC calendar day of a unix time, as "YYYY-MM-DD".
[[nodiscard]] std::string utc_day(double unix_seconds);
// The day's campaign seed: everyone who plays the daily challenge that day
// gets the same levels.
[[nodiscard]] std::uint64_t daily_seed(std::string_view day);

// The daily challenge's boards: for each of the last few days, the runs that
// scored (the first finished daily run of each name that day), best first,
// and the replays of each day's best few. Kept in memory and, given a data
// directory, in `daily.tsv` and `replays/<id>.txt` there.
class DailyBoard {
 public:
  struct Run {
    std::string day;
    std::string name;
    int levels = 0;
    int kills = 0;
    std::int64_t when = 0;  // unix seconds
    std::string replay;     // replay id; empty once it fell off the shown board
  };

  static constexpr std::size_t kShown = 10;    // runs a day with replays kept
  static constexpr std::size_t kDaysKept = 7;

  explicit DailyBoard(std::string dir = {});

  [[nodiscard]] bool has_played(std::string_view day, std::string_view name) const;
  // Scores a run with its replay, under the id given. Returns its place on
  // the day's board (0 is best), or -1 if this name already scored that day.
  int add(Run run, const Replay& replay);
  [[nodiscard]] const Replay* replay(std::string_view id) const;
  [[nodiscard]] const Run* best(std::string_view day) const;
  [[nodiscard]] const Run* run_with_replay(std::string_view id) const;
  // Forgets days older than kDaysKept before `today`.
  void prune(std::string_view today);
  // The "dailyboard" message: the day's board, and whether `name` has scored.
  [[nodiscard]] std::string to_json(std::string_view day, std::string_view name) const;

 private:
  [[nodiscard]] std::vector<const Run*> day_runs(std::string_view day) const;
  void load();
  void save() const;
  void save_replay(const std::string& id, const Replay& replay) const;
  void drop_replay(const std::string& id);

  std::string dir_;
  std::vector<Run> runs_;
  std::map<std::string, Replay, std::less<>> replays_;
};

}  // namespace mob_survivor::net
