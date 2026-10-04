# mob-survivor-game agent notes

- C++20, CMake 3.28+, CTest. Every first-party target goes through
  `mob_survivor_configure_target` (C++20, no extensions, `-Wall -Wextra
  -Wpedantic -Werror`).
- The simulation core (`mob_survivor_core`) stays free of rendering, input and
  platform code, and `World::step` stays deterministic: tests and replays
  depend on it.
- Public repository: commits use the repo-local noreply identity.
