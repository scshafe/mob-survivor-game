# mob-survivor-game

**Mob Survivor**: a multiplayer mob-shooter for the browser, in the style of
*Mob Control*. Slide your cannon, hold to fire, send your mob through `x3` and
`+8` gates, and crush the enemy base, alone, with up to three friends against
the horde, or against each other.

- **Campaign**: solo or co-op (up to 4) against an AI base, level after level,
  with an upgrade card for every player between levels and a boss every fifth
  level. Best runs go to the hall of fame.
- **Versus**: 1v1 or 2v2 on a mirrored arena; bots fill empty seats.
- Extras: bombs aimed from the keyboard (vim-style), chargeable giants,
  drifting "+1 shot" power-ups you have to hit, sliding, halving and shared
  gates, saw blades, the versus Frenzy, emotes, and rejoining a match after a
  drop.
- Controls: `A`/`D` or arrows and `Space` to slide and fire, `G` for a giant,
  `F` to Phase (your mobs ignore saws and `/2` gates for 5 s),
  `B` then `h` `j` `k` `l` and `B`/`Enter` for a bomb (keys only; touch on a
  phone).

## Play it

Open **<https://mob-survivor.colobus-stargazer.ts.net/>** from any device on
the tailnet (it is not reachable from the internet). Pick a name, then **Play
solo**, open a **Co-op room** or **Versus room** and share its code with
friends (they join with that code), or start a **Quick duel** against a bot.
Works on phones too: slide to aim, hold to fire.

Rules and tuning: [docs/design.md](docs/design.md). Wire protocol:
[docs/protocol.md](docs/protocol.md). Deploys, rollback and where state
lives: [docs/deploy.md](docs/deploy.md).

## Layout

| Path | What |
| --- | --- |
| `include/mob_survivor/`, `src/core/` | the deterministic simulation: world, levels, upgrades, bots, matches (`mob_survivor_core`) |
| `src/net/` | HTTP, WebSocket, JSON, wire protocol, rooms and lobby, hall of fame: no sockets, tested headless (`mob_survivor_net`) |
| `src/server/` | `mob-survivor-server`: one poll() loop serving the client and every room |
| `web/` | the browser client: plain ES modules and Canvas 2D, no build step |
| `tests/` | C++ unit tests, an end-to-end test of the real server, and the client's decoder test |
| `Dockerfile`, `deploy/stack/` | the image, and the app-owned stack that puts it on the tailnet as `mob-survivor` (deployed by a merge to `main`) |

## Run it locally

```sh
cmake -S . -B build/dev && cmake --build build/dev --parallel
build/dev/mob-survivor-server --port 8080 --web web
# open http://localhost:8080
```

Or with Docker: `docker build -t mob-survivor . && docker run --rm -p 8080:8080 mob-survivor`.

## Build and test

```sh
./scripts/verify-linux
```

That is the repository's one verify command (`dev.toml [verify]`): a clean
CMake configure, a build with warnings as errors, CTest, and a smoke run of
the server. Needs CMake 3.28+ and a C++20 compiler (GCC or Clang); the
end-to-end test needs `python3`, and the web client tests need `node` (they
are skipped, with a notice, without it).
