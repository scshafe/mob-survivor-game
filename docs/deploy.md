# Deploying and operating

The game runs at **<https://mob-survivor.colobus-stargazer.ts.net/>**,
reachable only from the tailnet (Funnel is off). It is deployed by the scshafe
**runner lane** (scshafe/infra `docs/platform/agent-deploy.md`): a merge to
`main` is the deploy. Nobody runs `docker compose` on the production host by
hand.

## How it fits together

```
browser (tailnet) --HTTPS/WSS--> ts-mob-survivor (tailscale sidecar, node "mob-survivor")
                                   | tailscale serve (deploy/stack/serve.json): 443 -> 127.0.0.1:8080
                                   v
                                 mob-survivor-game (same network namespace, uid 65532)
                                   mob-survivor-server: web client + /ws, leaderboard in /data
```

- The sidecar joins the tailnet as its own machine, `mob-survivor`, so the game
  gets its own MagicDNS name and certificate.
- The game shares the sidecar's network namespace and listens on
  `127.0.0.1:8080` only: nothing is published on the host.
- `tailscale serve` passes WebSocket upgrades through and adds
  `Tailscale-User-Name`, which the game uses only to suggest a player name.

The stack lives in this repository, in [`deploy/stack/`](../deploy/stack/):
`compose.yaml`, `serve.json` and `stack.toml` (the deploy's data: image, pin,
health path, backup paths, env names). The host's allowances (node name, no
door, no host binds) are in scshafe/infra `stacks/mob-survivor-game/host.conf`.

## A deploy

`.github/workflows/deploy.yml` (managed by scshafe-dev from `dev.toml
[deploy]`) runs on every push to `main`:

1. **verify**, on a GitHub-hosted runner: `./scripts/verify-linux`.
2. **deploy**, on this repository's own runner on the production host
   (`laptop-mob-survivor-game-prod`): the host entrypoint fetches the commit
   from `main`, checks it descends from what is live, backs up, builds the
   image from the `Dockerfile` (the build stage runs the whole test suite
   again), validates `deploy/stack/` against infra's allowlist, and brings the
   stack up. If the new stack does not answer `/healthz` through the tailnet,
   it rolls back to the previous image and files by itself, and the run is red.
3. **health**: a read-only check that the live commit, the containers
   (running, healthy), `/healthz` and "tailnet only" are all as expected.

Watch it with `gh run watch` (or `gh run list -w deploy`). The job log is a
summary (a public repository); the full log stays on the host.

Matches in progress end when the game container restarts; the hall of fame
is kept.

## Rolling back

- Merge a revert of the bad change: that deploys like any other merge.
- Or dispatch `deploy.yml` with `sha` = an older commit on `main` and
  `allow_rollback` = true (`gh workflow run deploy.yml -f sha=<sha> -f
  allow_rollback=true`). It skips verify, since that commit was verified when
  it was merged.

## Where state lives

On the production host, in the runtime directory `/srv/stacks/mob-survivor-game/`:

| Path | What |
| --- | --- |
| `state/tailscale/` | the node identity of `mob-survivor` (keep it, or the node must be re-joined with a new auth key) |
| `state/game/` | the hall of fame (`leaderboard.tsv`), the daily boards (`daily.tsv`, the last 7 days) and their best runs' replays (`replays/`, at most 10 a day, a few hundred KB at most each), owned by uid 65532 |
| `.env` | the image pin, and `TS_AUTHKEY`, which the deploy blanks once the node has joined |
| `compose.yaml`, `serve.json`, `stack.toml` | the materialised copy of `deploy/stack/` at the live commit |
| `.deployed.json`, `.deployed/prev/` | the live record and the previous set, for rollback |

`stack.toml [backup]` names `state/game` and the tailscale identity for the
host's nightly backup.

## Check

```sh
curl -fsS https://mob-survivor.colobus-stargazer.ts.net/healthz      # ok
curl -fsS https://mob-survivor.colobus-stargazer.ts.net/api/status   # rooms, clients
```

The image's own healthcheck is `mob-survivor-server --healthcheck`, which
probes `/healthz` inside the container (the image has no curl).

## Running without the lane

The server is one binary with no runtime dependencies beyond the C++ runtime:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release
build/release/mob-survivor-server --bind 127.0.0.1 --port 8080 --web web --data ~/.local/share/mob-survivor
```

`tailscale serve --bg --https=443 http://127.0.0.1:8080` on that machine then
serves it at that machine's own name rather than at `mob-survivor`.
