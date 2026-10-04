#pragma once

#include <cstdint>

namespace mob_survivor {

// SplitMix64: small, fast and fully specified, so a seed gives the same
// sequence on every platform and compiler. The simulation draws all of its
// randomness from these, never from std::random_device or the clock.
class Rng {
 public:
  explicit Rng(std::uint64_t seed = 0x6d6f622d73757276ULL) : state_(seed) {}

  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
  }

  // Uniform in [0, 1).
  double unit() { return static_cast<double>(next() >> 11U) * 0x1.0p-53; }

  double uniform(double lo, double hi) { return lo + (hi - lo) * unit(); }

  // Uniform integer in [lo, hi], inclusive.
  int range(int lo, int hi) {
    const auto span = static_cast<std::uint64_t>(hi - lo) + 1U;
    return lo + static_cast<int>(next() % span);
  }

  bool chance(double probability) { return unit() < probability; }

  [[nodiscard]] std::uint64_t state() const { return state_; }

 private:
  std::uint64_t state_;
};

}  // namespace mob_survivor
