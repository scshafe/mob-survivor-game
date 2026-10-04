# Deploying to the tailnet

Goal: the game at `https://mob-survivor.<tailnet>.ts.net/`, reachable only
from the tailnet. The repository carries everything for it in
[`deploy/tailnet/`](../deploy/tailnet/); deploying is an owner step, because
it needs a Docker host on the tailnet and a Tailscale auth key, and the
project's agent has neither (its sandbox cannot reach the tailnet).

## How it fits together

```
browser (tailnet) --HTTPS/WSS--> tailscale container "mob-survivor"
                                   | tailscale serve (serve.json): 443 -> 127.0.0.1:8080
                                   v
                                 game container (same network namespace)
                                   mob-survivor-server: web client + /ws, data in volume game-data
```

- The `tailscale` container joins the tailnet as its own machine named
  `mob-survivor`, so the game gets its own MagicDNS name and certificate
  instead of a port on an existing host.
- The `game` container shares that container's network namespace and listens
  on `127.0.0.1:8080` only: nothing is published on the Docker host, and the
  only way in is through the tailnet.
- `tailscale serve` passes WebSocket upgrades through and adds
  `Tailscale-User-Name`, which the game uses only to suggest a player name.
- Funnel stays off (`AllowFunnel: false`).

## First deploy

On any Docker host that can reach the internet (it does not have to be on the
tailnet already; the sidecar joins by itself):

1. Make an auth key at <https://login.tailscale.com/admin/settings/keys>.
   One-off (not reusable) is enough, since the node's state is kept in the
   `tailscale-state` volume. A tag (for example `tag:game`, declared in the
   tailnet policy) keeps the node from expiring with a user's login. The
   tailnet needs MagicDNS and HTTPS certificates enabled (DNS page of the
   admin console); both already are if other `*.ts.net` HTTPS URLs work.
2. Get the code and configure the key:

   ```sh
   git clone https://github.com/scshafe/mob-survivor-game.git
   cd mob-survivor-game/deploy/tailnet
   cp .env.example .env
   $EDITOR .env          # TS_AUTHKEY=tskey-auth-...
   ```

3. Build and start:

   ```sh
   docker compose up -d --build
   docker compose logs -f tailscale   # wait for the node to come up
   ```

   The image build compiles the server and runs the whole test suite inside
   the build stage, so a broken commit fails here rather than at runtime.
4. Open `https://mob-survivor.<tailnet>.ts.net/`. The first HTTPS request can
   take a few seconds while the certificate is issued.

If the admin console already has a machine called `mob-survivor`, the new one
becomes `mob-survivor-1`; remove or rename the old machine first to keep the
name.

## Update

```sh
cd mob-survivor-game
git pull --ff-only
cd deploy/tailnet && docker compose up -d --build game
```

Matches in progress end when the game container restarts; the hall of fame is
kept in the `game-data` volume.

## Check

```sh
curl -fsS https://mob-survivor.<tailnet>.ts.net/healthz      # ok
curl -fsS https://mob-survivor.<tailnet>.ts.net/api/status   # rooms, clients
docker compose ps
docker compose logs --since 10m game
```

## Stop or remove

```sh
docker compose down            # keeps the volumes (tailnet identity, hall of fame)
docker compose down --volumes  # forgets both; remove the machine in the admin console too
```

## Running without Docker

The server is one binary with no runtime dependencies beyond the C++ runtime:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release
build/release/mob-survivor-server --bind 127.0.0.1 --port 8080 --web web --data ~/.local/share/mob-survivor
```

`tailscale serve --bg --https=443 http://127.0.0.1:8080` on that machine then
serves it at that machine's own name rather than at `mob-survivor`.
