# Game design

Mob Survivor is a mob-shooter in the style of *Mob Control*: you slide a
cannon along your wall and hold to fire a stream of little mobs up a lane.
Gates across the lane multiply whatever runs through them; the crowd that
comes out the other side fights the enemy's crowd one for one and pounds the
enemy base. This page records the rules as built and the numbers that tune
them; the code is the authority (`src/core/`).

## The arena

A portrait lane 24 x 40 world units. Blue's base is the band `y < 2.5`, Red's
`y > 37.5`; cannons sit 3.4 units in front of their base. Every player sees
their own base at the bottom (Red's view is turned 180 degrees).

## Rules

- **Mobs** march straight at the enemy base. A grunt is one hit point; a
  *squad* is a grunt with more, drawn bigger with its count on it. Enemy mobs
  that touch trade hit points one for one (`min(a, b)` off both). Crowded
  friends shoulder each other sideways, so a stream spreads into a crowd.
- **Base hits**: a mob that reaches the enemy's band deals its hit points as
  damage (a brute deals double) and is gone.
- **Gates** (`xN`, `+N`, `/2`) change each mob once: `xN` turns a mob into N
  (a squad of h into N*h), `+N` adds N, `/2` culls half (a lone grunt has a
  50% chance). Gates can slide. Purple-edged gates serve both teams.
- **The cap**: at most 300 mobs a team on the field; what a gate would add
  beyond that is lost. Giants and bosses ignore the cap.
- **Giants**: firing charges a meter (45 volleys); a charged cannon launches
  a giant (15 hp, tramples squads). **Bombs**: thrown at any point, land after
  0.8 s, deal 6 to every enemy mob within 2.6 units; 13 s cooldown.
- **Saw blades** slide along a rail and cut any mob they touch, either team.

## Modes

- **Campaign** (solo, or co-op for up to four): level after level against an
  AI base that sends waves of grunts, runners (from level 3) and giants (from
  level 2), with a boss level (brutes) every fifth level. From level 6 one
  gate row serves both sides. Enemy base health and wave size scale with the
  level and the number of players. Losing the base ends the run; the run's
  levels cleared go to the hall of fame.
- **Upgrade cards**: after each cleared level every player picks one of three
  cards (rapid fire, twin barrel, generous and golden gates, lucky charm,
  quick charge, heavy giants, demolitions, sprinters, fortify). Some stack.
- **Versus** (1v1 or 2v2, bots fill empty seats): a point-symmetric arena,
  so both sides meet the same gates in the same order. Bases have 4000 health
  (+2000 per extra player on the larger side). At 2:30 the **Frenzy** makes
  every gate stronger (`xN` +1, `+N` half again); at 4:00 the healthier base
  wins.

## What we added to the original formula

- Co-op campaign and live versus, in the browser, with drop-in rejoin.
- Roguelite upgrade cards between levels.
- Bombs you aim yourself, and giants you launch when you choose.
- Sliding gates, `/2` gates, shared gates and saw blades.
- Boss levels, the versus Frenzy, emotes and a hall of fame.

## Balance

`World` and `Match` are deterministic, so balance can be measured headless: the
built-in bot (`src/core/bot.cpp`) plays the campaign and versus at a chosen
skill. At the numbers in the code, a skilled bot alone clears about 7-13
campaign levels; an idle player loses level 1 in about 30 s; bot-against-bot
versus rounds last one to four minutes. `tests/match_test.cpp` keeps the
broad shape (levels can be cleared, an idle player loses, versus ends).
