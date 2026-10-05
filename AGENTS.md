# mob-survivor-game agent notes

- C++20, CMake 3.28+, CTest. Every first-party target goes through
  `mob_survivor_configure_target` (C++20, no extensions, `-Wall -Wextra
  -Wpedantic -Werror`).
- The simulation core (`mob_survivor_core`) stays free of rendering, input and
  platform code, and `World::step` stays deterministic: tests and replays
  depend on it. All randomness comes from `Rng` (seeded), never the clock.
- `mob_survivor_net` (`src/net/`) holds everything server-side that does not
  touch a socket (HTTP, WebSocket framing, JSON, protocol, lobby); keep it
  that way so it stays testable headless. Sockets live only in `src/server/`.
- The web client (`web/`) is plain ES modules with no build step and no
  third-party code. A protocol change updates `docs/protocol.md`, both
  `kProtocolVersion` and the client's `PROTOCOL`, and the decoder test.
- Balance changes: measure with the bot (`bot_think`) over several seeds
  before and after, and keep `docs/design.md` in step.
- A merge to `main` deploys (the runner lane, `docs/deploy.md`): watch it
  with `gh run watch`; roll back by merging a revert, or by dispatching
  `deploy.yml` with `sha` and `allow_rollback`. `deploy/stack/` is checked by
  `dev check` (DEPLOY-03..05) and, at deploy, by infra's validator; the agent
  cannot reach the tailnet or the host.
- Public repository: commits use the repo-local noreply identity.

<!-- scshafe-dev:begin landing -->
## Verify and landing

Managed by scshafe-dev: `dev adopt` and `dev update` refresh this section from `dev.toml`; change `dev.toml`, not these lines.

Before finishing, both of these must pass:

```sh
./scripts/verify-linux
dev check .
```

How a change lands:

1. Work on a branch and open a PR.
2. Run the two commands above. If the repository is private, GitHub Actions does not run for it: verify locally and say in the PR what you ran. If it is public, wait for CI to be green.
3. Merge your own PR with a merge commit, one change at a time: `gh pr merge <N> --merge --subject "Merge #<N>: <title>"`. Never squash or rebase (both are off on the repository), and pass `--subject`: `gh pr merge` does not make the `Merge #N: <title>` subject by itself.

The project's agent may merge its own PR and push `main`; there is no approval gate.

Central concepts are defined in VOCABULARY.toml; refer to them as `mob-survivor-game:<slug>` (e.g. `mob-survivor-game:mob`), and add a term with `dev vocab add` when you introduce one.
<!-- scshafe-dev:end landing -->
