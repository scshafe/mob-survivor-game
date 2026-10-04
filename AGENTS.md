# mob-survivor-game agent notes

- C++20, CMake 3.28+, CTest. Every first-party target goes through
  `mob_survivor_configure_target` (C++20, no extensions, `-Wall -Wextra
  -Wpedantic -Werror`).
- The simulation core (`mob_survivor_core`) stays free of rendering, input and
  platform code, and `World::step` stays deterministic: tests and replays
  depend on it.
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
<!-- scshafe-dev:end landing -->
