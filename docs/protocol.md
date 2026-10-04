# Wire protocol (version 1)

One WebSocket per browser tab, at `GET /ws`. Text frames carry JSON objects,
each with a type in `t`. Binary frames carry snapshots (server to client only).
The server's side lives in `src/net/lobby.cpp` and `src/net/protocol.cpp`; the
client's in `web/js/main.js`, `web/js/game.js` and `web/js/snapshot.js`.
`tests/web_snapshot_test.mjs` checks the client decoder against a snapshot the
server encoded, so the two cannot drift apart unnoticed.

Bump `kProtocolVersion` (`src/net/protocol.hpp`) and `PROTOCOL`
(`web/js/main.js`) together whenever a message changes shape.

## Limits

- Client text messages: at most 4 KiB; a budget of 120 messages that refills
  at 60 a second (extra messages are dropped).
- Names: control characters removed, leading and trailing blanks trimmed, at
  most 16 code points.
- A client that falls 4 MiB behind on reads is disconnected (it reconnects).
- Silence for 60 s closes a socket; the server pings every 20 s.

## Client to server

| `t` | Fields | When |
| --- | --- | --- |
| `hello` | `name`, `token`? | after connecting; again to rename. A `token` from an earlier connection reclaims that player's seat in a running match (60 s grace). |
| `rooms` | | asks for the public room list |
| `board` | | asks for the hall of fame |
| `create` | `mode` (`campaign`\|`versus`), `public`, `start`?, `bots`? | makes a room and enters it as host; `start` starts at once; `bots` (versus) seats a Red bot |
| `join` | `code` | enters a room: seated if the lobby has room, else watching |
| `leave` | | leaves the room |
| `team` | `team` (0 Blue, 1 Red) | lobby: take a seat, or switch team (versus, at most 2 a side) |
| `spectate` | | lobby: give up the seat and watch |
| `bot` | `team` | host, lobby: add a bot |
| `unbot` | | host, lobby: remove the last bot |
| `start` | | host, lobby: start the match (versus fills an empty side with a bot) |
| `in` | `x`, `f` | seated, in a match: cannon target x (world units) and whether firing |
| `giant` | | launch a giant, if charged |
| `bomb` | `x`, `y` | throw a bomb at that world point, if off cooldown |
| `pick` | `i` | upgrade phase: take card `i` of the offer |
| `again` | | host, game over: back to the room lobby |
| `emote` | `e` (0-5) | show an emote to the room |
| `ping` | `n` | answered by `pong` with the same `n` |

## Server to client (JSON)

| `t` | Fields |
| --- | --- |
| `welcome` | `v` (protocol), `id`, `name` (suggested; from `Tailscale-User-Name` when present), `token` |
| `session` | `name`, `token`, `rejoined`? (room code when the token reclaimed a seat) |
| `rooms` | `rooms`: `[{code, mode, phase, seated, members, maxSeats, host, level?}]` |
| `board` | `entries`: `[{names, levels, kills, when}]`, best first |
| `room` | `code`, `mode`, `public`, `phase` (`lobby`\|`countdown`\|`playing`\|`upgrade`\|`over`), `host`, `you`, `maxSeats`, `members`: `[{id, name, team, bot, seated, slot, connected, host}]` |
| `level` | `serial`, `level`, `mode`, `boss`, `field` `{w, h, baseDepth, cannonOffset}`, `timeLimit`, `frenzyAt`, `gates`: `[{x, y, w, op, v, teams, moving}]`, `saws`: `[{y, r}]` |
| `cards` | `cleared`, `picked`, `cards`: `[{key, title, text, taken}]` |
| `picks` | `picked`: slots that have chosen |
| `over` | `mode`, `outcome` (`blue`\|`red`\|`draw`), `levels`, `rank` (hall of fame place or -1), `players`: `[{slot, name, team, bot, shots, gateMobs, kills, baseDamage, giants, bombs}]` |
| `emote` | `id`, `slot`, `e`, `name` |
| `pong` | `n` |
| `error` | `message` |

`teams` on a gate is a bit set: 1 Blue, 2 Red, 3 both (drawn with a purple edge).

## Snapshot (binary)

Sent 15 times a second while a match runs (every 2nd of 30 ticks; every 15th
in the game-over phase). Little-endian. A "coord" is an `i16` in hundredths
of a world unit.

| Field | Type |
| --- | --- |
| kind (= 1) | u8 |
| phase (0 lobby, 1 countdown, 2 playing, 3 upgrade, 4 over) | u8 |
| level | u16 |
| match tick | u32 |
| phase time left, seconds | f32 |
| level time elapsed, seconds | f32 |
| flags (bit 0: frenzy) | u8 |
| outcome (0 none, 1 Blue wins, 2 Red wins, 3 draw) | u8 |
| bases, Blue then Red: hp, max hp | 2 x (i32, i32) |
| cannon count, then each: slot u8, team u8, x coord, charge u8 (/255), bomb cooldown u8 (tenths of s), flags u8 (1 connected, 2 firing, 4 giant ready) | u8 + n x 7 |
| gate count, then each gate's current x | u8 + n x coord |
| saw count, then each saw's current x | u8 + n x coord |
| bomb count, then each: team u8, from x, from y, target x, target y, radius (coords), fuse u8 (hundredths of s) | u8 + n x 12 |
| event count, then each: type u8, team u8, index i16, x, y (coords), value i32 | u16 + n x 12 |
| mob count, then each: id u32, x, y (coords), team and kind u8 (bit 0 team, bits 1-3 kind), hp u16 | u16 + n x 11 |

Mob kinds: 0 grunt, 1 runner, 2 giant, 3 brute. Event types: 1 gate pass
(value: mobs gained, negative for a "/2" cull; index: gate), 2 base hit (team:
the base hit; value: damage), 3 bomb blast (value: mobs destroyed), 4 giant
launch, 5 saw cut, 6 frenzy, 7 boss spawn.

The client interpolates mob positions between consecutive snapshots by mob id
and renders about 110 ms behind the newest one. A mob present in one snapshot
and gone in the next has died; one that died at the far wall hit the base.
