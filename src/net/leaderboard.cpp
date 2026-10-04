#include "net/leaderboard.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "net/json.hpp"

namespace mob_survivor::net {

namespace {

bool better(const Leaderboard::Entry& a, const Leaderboard::Entry& b) {
  if (a.levels != b.levels) return a.levels > b.levels;
  if (a.kills != b.kills) return a.kills > b.kills;
  return a.when < b.when;
}

std::string clean(std::string text) {
  std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
  return text;
}

}  // namespace

Leaderboard::Leaderboard(std::string path) : path_(std::move(path)) { load(); }

int Leaderboard::add(Entry entry) {
  entry.names = clean(std::move(entry.names));
  const auto at = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& other) { return better(entry, other); });
  const auto rank = static_cast<std::size_t>(at - entries_.begin());
  if (rank >= kSize) return -1;
  entries_.insert(at, std::move(entry));
  if (entries_.size() > kSize) entries_.resize(kSize);
  save();
  return static_cast<int>(rank);
}

std::string Leaderboard::to_json() const {
  JsonWriter w;
  w.begin_object();
  w.field("t", "board");
  w.key("entries").begin_array();
  for (const Entry& entry : entries_) {
    w.begin_object();
    w.field("names", entry.names).field("levels", entry.levels).field("kills", entry.kills).field("when", entry.when);
    w.end_object();
  }
  w.end_array();
  w.end_object();
  return w.take();
}

void Leaderboard::load() {
  if (path_.empty()) return;
  std::ifstream in(path_);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    Entry entry;
    std::string levels;
    std::string kills;
    std::string when;
    if (!std::getline(fields, levels, '\t') || !std::getline(fields, kills, '\t') || !std::getline(fields, when, '\t') ||
        !std::getline(fields, entry.names)) {
      continue;
    }
    try {
      entry.levels = std::stoi(levels);
      entry.kills = std::stoi(kills);
      entry.when = std::stoll(when);
    } catch (...) {
      continue;
    }
    entries_.push_back(std::move(entry));
  }
  std::sort(entries_.begin(), entries_.end(), better);
  if (entries_.size() > kSize) entries_.resize(kSize);
}

void Leaderboard::save() const {
  if (path_.empty()) return;
  const std::string temporary = path_ + ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return;
    for (const Entry& entry : entries_) {
      out << entry.levels << '\t' << entry.kills << '\t' << entry.when << '\t' << entry.names << '\n';
    }
    if (!out) return;
  }
  std::rename(temporary.c_str(), path_.c_str());
}

}  // namespace mob_survivor::net
