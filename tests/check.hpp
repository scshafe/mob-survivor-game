#pragma once

// A minimal test harness: CHECK records a failure and keeps going; each test
// binary's main() runs its cases and returns finish().

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace check {

inline int failures = 0;

inline void fail(const char* file, int line, const char* what) {
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what);
  ++failures;
}

inline int finish(const char* name) {
  if (failures != 0) {
    std::fprintf(stderr, "%s: %d failure(s)\n", name, failures);
    return EXIT_FAILURE;
  }
  std::printf("%s: ok\n", name);
  return EXIT_SUCCESS;
}

}  // namespace check

#define CHECK(condition)                                         \
  do {                                                           \
    if (!(condition)) check::fail(__FILE__, __LINE__, #condition); \
  } while (false)

#define CHECK_NEAR(actual, expected, tolerance) CHECK(std::fabs((actual) - (expected)) <= (tolerance))
