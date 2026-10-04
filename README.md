# mob-survivor-game

A survivor-style game: the player holds out against ever-growing hordes of
mobs. Written in C++20 with CMake, in the same style as blob-royale.

The core is a deterministic simulation (`include/mob_survivor/`, `src/`) with
no rendering or platform code, so it builds and tests headless. The renderer
and input layer are not chosen yet.

## Build and test

```sh
./scripts/verify-linux
```

That is the repository's one verify command (`dev.toml [verify]`): a clean
CMake configure, a build with warnings as errors, CTest, and a smoke run of
the placeholder executable. Needs CMake 3.28+ and a C++20 compiler (GCC or
Clang).
