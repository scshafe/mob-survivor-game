#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mob_survivor::net {

// The campaign's hall of fame: the best runs by levels cleared, then by
// mobs destroyed. Kept in memory and, when given a path, in a small text file
// (one tab-separated run per line) so it survives restarts.
class Leaderboard {
 public:
  struct Entry {
    std::string names;  // the crew, e.g. "Ann & Bo"
    int levels = 0;
    int kills = 0;
    std::int64_t when = 0;  // unix seconds
  };

  static constexpr std::size_t kSize = 10;

  explicit Leaderboard(std::string path = {});

  // Records a run; returns its rank (0 is best) or -1 if it did not place.
  int add(Entry entry);

  [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
  [[nodiscard]] std::string to_json() const;

 private:
  void load();
  void save() const;

  std::string path_;
  std::vector<Entry> entries_;
};

}  // namespace mob_survivor::net
