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
- **Transforming gates** change what mobs are, not how many (from campaign
  level 2, and in versus; at most one in a row of two or three gates, about
  18% of those gates): **fuse** (`10→👑`) takes in grunts and runners and
  turns every 10 hit points it has taken from a team into a giant of 12 (more
  with Heavy Giants), keeping what is left over toward the next; **runner**
  (`»RUN»`) turns grunts into runners (6.8 units a second instead of 4.6);
  **armor** (`⛨`) lets each mob shrug off its next hit, whether an enemy mob,
  a saw or a bomb (the enemy mob still takes its hit).
- **The cap**: at most 300 mobs a team on the field; what a gate would add
  beyond that is lost. Giants and bosses ignore the cap.
- **Giants**: firing charges a meter (45 volleys); a charged cannon launches
  a giant (15 hp, tramples squads). **Bombs**: thrown at any point, land after
  0.8 s, deal 6 to every enemy mob within 2.6 units; 13 s cooldown.
- **Saw blades** slide along a rail and cut any mob they touch, either team:
  1 hit point (giants and brutes too), then the mob is thrown aside and is safe
  from saws for 0.8 s, so a blade nicks a squad instead of grinding through it.
  Campaign levels have one saw from level 3, two from 8, three from 15.
- **Phase**: a rechargeable power. For 5 s a player's mobs (on the field and
  newly fired) ignore the map, meaning saw blades and "/2" gates, but still
  fight enemy mobs, take bomb damage and hit the base as usual. It recharges
  for 20 s after it ends, and starts each level ready.
- **Layouts**: each campaign level from level 3, and each versus arena, rolls
  one of five shapes (equally likely): **Open** (the plain lane);
  **Hourglass**, walls 3 deep from both side walls leaving an 8-unit gap, on
  the gap between gate rows nearest the centre line (a saw there keeps to the
  gap); **Twin Lanes**, a wall down the centre from the first gate row to the
  last, with a crossing 2.4 units wide in the middle, and every gate row two
  gates (no sliding ones), one a lane; **Conveyor**, belts 1.6 deep between
  the gate rows that carry mobs sideways at 2.2-3 units a second, alternating
  direction; **Teleporters**, pairs of pads 3.5 units in from the side walls
  between the gate rows: a mob stepping on one comes out of the other, once a
  pair. Walls push mobs out sideways and they walk on along them; power-ups,
  bombs and saw blades pass over. Phasing mobs are not carried by belts. In
  versus every piece is point-symmetric, like the gates.
- **Power-ups**: orbs drift in from one side wall and out by the other
  (1.4-2.4 units a second). A player's grunts and runners that run into one
  are spent on it, one hit point for one; the player whose mob breaks it gets
  its effect. The AI's mobs and giants pass them by. Kinds, and how often
  each turns up:
  - **+1 shot** (gold, 40%): the breaker fires one more mob every volley for
    the rest of the level, up to three.
  - **Freeze** (blue, 15%): the breaker's enemies march at half speed for 6 s.
  - **Flip** (pink, 15%): for 7 s every gate counts as "/2" for the breaker's
    enemies (Phase and Lucky Charm still protect). Only on levels where both
    sides have gates; elsewhere it is a +1 shot.
  - **Shield** (white, 15%): the breaker's base soaks up the next 15% of its
    max health in damage.
  - **Magnet** (green, 15%): for 8 s the breaker's grunts drift toward the
    nearest helpful gate ahead.

  Campaign: the first after 6 s, then about every 9 s, 10 + 2 x level hit
  points (at most 30), between the first and last gate rows. Versus: the first
  after 15 s, then about every 22 s, 40 hit points, 3-6.5 units either side of
  the centre line. At most three at once.
- **Phase**: a rechargeable power. For 5 s a player's mobs (on the field and
  newly fired) ignore the map, meaning saw blades and "/2" gates, but still
  fight enemy mobs, take bomb damage and hit the base as usual. It recharges
  for 20 s after it ends, and starts each level ready.
- **Power-ups**: a gold "+1" orb drifts in from one side wall and out by the
  other (1.4-2.4 units a second). A player's grunts and runners that run into
  it are spent on it, one hit point for one; the player whose mob breaks it
  fires one more mob every volley for the rest of the level, up to three. The
  AI's mobs and giants pass it by. Campaign: the first after 7 s, then about
  every 16 s, 10 + 2 x level hit points (at most 30), between the first and
  last gate rows. Versus: the first after 15 s, then about every 22 s, 40 hit
  points, 3-6.5 units either side of the centre line. At most two at once.

## Controls

The keyboard drives the game; the mouse does not move, fire or aim (a phone's
touch screen still does: drag to slide, hold to fire, tap the bomb button and
then the field).

- `A`/`D` or the arrows slide the cannon; hold `Space` to fire.
- `G` (or `E`) launches a charged giant.
- `F` starts Phase.
- `B` (or `Q`) shows the bomb aimer on the nearest enemy group; `B` again or
  `Enter` throws it (once the bomb is off cooldown), `Esc` puts it away. The
  aimer moves vim-style, in screen directions: `h` `j` `k` `l` one unit, a
  count first repeats the step (`5k`), `0`/`$` jump to the left/right wall,
  `H`/`M`/`L` to the top/middle/bottom of the field, `n`/`N` to the next
  enemy group farther from/nearer to your base. Any motion also shows the
  aimer.

## Modes

- **Campaign** (solo, or co-op for up to four): level after level against an
  AI base that sends waves of grunts, runners (from level 3) and giants (from
  level 2), with a boss level (brutes) every fifth level. From level 6 one
  gate row serves both sides. Enemy base health and wave size scale with the
  level and the number of players. Losing the base ends the run; the run's
  levels cleared go to the hall of fame.
- **Upgrade cards**: after each cleared level every player picks one of three
  cards (rapid fire, lucky charm, quick charge, heavy giants, demolitions,
  sprinters, fortify). Some stack. No card adds mobs to a volley or to a
  gate: extra mobs a volley come only from power-ups, within a level.
- **Daily challenge** (solo): everyone who plays on a UTC day gets the same
  campaign, seeded from the date. A name's first finished run that day goes
  on the day's board (best levels, then kills); later runs are practice. A
  practice run races a 👻 ghost: the day's best run, re-simulated alongside
  from its replay, shown as a faint cannon and its progress. The server keeps
  seven days of boards and the replays of each day's top ten. Names are not
  accounts, so "one scored run" holds only as far as names do.
- **Replays**: a match is deterministic, so a run is stored as its players'
  inputs and the tick each arrived at (aims rounded to hundredths, and aims
  that change nothing left out): a few hundred inputs a minute from a steady
  player, up to about a thousand from one who never stops moving (tens of KB). Watching one (▶ on the daily board) plays it back on the server and
  streams it like a live match; a test re-simulates recorded runs and checks
  they end the same.
- **Versus** (1v1 or 2v2, bots fill empty seats): a point-symmetric arena,
  so both sides meet the same gates in the same order. Bases have 4600 health
  (+2000 per extra player on the larger side). At 2:30 the **Frenzy** makes
  every gate stronger (`xN` +1, `+N` half again); at 4:00 the healthier base
  wins.

## What we added to the original formula

- Co-op campaign and live versus, in the browser, with drop-in rejoin.
- Roguelite upgrade cards between levels.
- Bombs you aim yourself (with a vim-style keyboard aimer), giants you
  launch when you choose, and Phase to slip a crowd past the hazards.
- Drifting power-ups (+1 shot, freeze, flip, shield, magnet) you have to hit
  to claim.
- Sliding gates, `/2` gates, shared gates, fuse, runner and armor gates, saw
  blades, and named layouts (hourglass, twin lanes, conveyor, teleporters).
- Boss levels, the versus Frenzy, emotes and a hall of fame.
- A daily challenge with replays and a ghost of the day's best run.

## Balance

`World` and `Match` are deterministic, so balance can be measured headless: the
built-in bot (`src/core/bot.cpp`) plays the campaign and versus at a chosen
skill, goes for power-ups when nothing threatens its base, and phases when a
dozen of its mobs are about to meet a saw or a "/2" gate. At the numbers in
the code, a skilled bot alone clears 4-14 campaign levels (about 8.7 on
average over seeds 1-24); an idle player loses level 1 in about 30 s;
bot-against-bot versus rounds last one to three minutes (about 120 s on
average over seeds 1-8). Every layout is about as winnable as the open lane:
over seeds 1-40, levels 3-8, the bot wins 72-91% of each. Saws deal about 360
damage a minute of campaign play, across both
teams (680 before they were softened and Phase was added). `tests/match_test.cpp` keeps the
broad shape (levels can be cleared, an idle player loses, versus ends).
