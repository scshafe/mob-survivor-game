// The web client's snapshot decoder against snapshots the server encoded, one
// for each feature of the layout.
// usage: node web_snapshot_test.mjs <fixture-dir>   (see snapshot_fixture.cpp)

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { decodeSnapshot, mobIndex } from '../web/js/snapshot.js';

const dir = process.argv[2];
const { cases } = JSON.parse(readFileSync(join(dir, 'expected.json'), 'utf8'));

let failures = 0;
const check = (ok, what) => {
  if (!ok) {
    console.error(`FAIL ${what}`);
    failures++;
  }
};
const effectOn = (e) => e.shield > 0 || e.frozen > 0 || e.flipped > 0;
// What each case is there to show.
const shows = {
  powerup: (e) => e.powerups > 0,
  effect: (e) => effectOn(e.blueEffects) || effectOn(e.redEffects),
  phased: (e) => e.phasedMobs > 0 && e.phasingCannons > 0,
  armored: (e) => e.armoredMobs > 0,
  fuse: (e) => e.fuseFill.flat().some((v) => v > 0),
};

check(Object.keys(shows).every((feature) => cases.some((c) => c.feature === feature)), 'a case for every feature');
for (const expected of cases) {
  const at = (what) => `${expected.feature}: ${what}`;
  const bytes = readFileSync(join(dir, expected.file));
  const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  const snap = decodeSnapshot(buffer);
  check(snap !== null, at('decodes'));
  if (!snap) continue;
  check(shows[expected.feature]?.(expected), at('the case shows its feature'));
  check(snap.size === expected.size && snap.size === bytes.byteLength, at(`reads every byte (${snap.size})`));
  check(snap.phase === expected.phase, at('phase'));
  check(snap.level === expected.level, at('level'));
  check(snap.tick === expected.tick, at('tick'));
  check(snap.bases[0].hp === expected.blueHp, at('blue base hp'));
  check(snap.bases[1].max === expected.redMax, at('red base max'));
  check(snap.cannons.length === expected.cannons, at('cannons'));
  check(snap.gateX.length === expected.gates, at('gates'));
  check(snap.events.length === expected.events, at('events'));
  check(snap.cannons[0].volley === expected.volley, at('first cannon volley'));
  check(snap.powerups.length === expected.powerups, at('power-ups'));
  [expected.blueEffects, expected.redEffects].forEach((want, t) => {
    const got = snap.effects[t];
    check(got.shield === want.shield, at(`team ${t} shield`));
    check(Math.abs(got.frozen - want.frozen) <= 0.051, at(`team ${t} frozen`));
    check(Math.abs(got.flipped - want.flipped) <= 0.051, at(`team ${t} flipped`));
  });
  check(snap.cannons.filter((c) => c.phasing).length === expected.phasingCannons, at('phasing cannons'));
  check(snap.cannons.every((c) => c.phase >= 0 && c.phase <= 1), at('phase meters'));
  check(snap.mobs.phased.reduce((a, b) => a + b, 0) === expected.phasedMobs, at('phased mobs'));
  check(snap.mobs.armored.reduce((a, b) => a + b, 0) === expected.armoredMobs, at('armored mobs'));
  check(snap.mobs.kinds.every((k) => k <= 3), at('mob kinds without the phase and armor bits'));
  check(JSON.stringify(snap.fuseFill) === JSON.stringify(expected.fuseFill), at('fuse fill per gate'));
  if (expected.firstPowerUp) {
    const p = snap.powerups[0];
    check(p.id === expected.firstPowerUp.id, at('power-up id'));
    check(p.kind === expected.firstPowerUp.kind, at('power-up kind'));
    check(Math.abs(p.x - expected.firstPowerUp.x) <= 0.006, at('power-up x'));
    check(Math.abs(p.y - expected.firstPowerUp.y) <= 0.006, at('power-up y'));
    check(p.hp === expected.firstPowerUp.hp && p.max === expected.firstPowerUp.max, at('power-up hp'));
  }
  check(snap.mobs.count === expected.mobs, at('mobs'));
  check(expected.mobs > 0, at('the case has mobs'));
  if (expected.lastMob) {
    const i = mobIndex(snap).get(expected.lastMob.id);
    check(i === snap.mobs.count - 1, at('last mob found by id'));
    check(Math.abs(snap.mobs.xs[i] - expected.lastMob.x) <= 0.006, at('last mob x'));
    check(Math.abs(snap.mobs.ys[i] - expected.lastMob.y) <= 0.006, at('last mob y'));
    check(snap.mobs.teams[i] === expected.lastMob.team, at('last mob team'));
    check(snap.mobs.kinds[i] === expected.lastMob.kind, at('last mob kind'));
    check(snap.mobs.hps[i] === expected.lastMob.hp, at('last mob hp'));
  }
}

if (failures) {
  console.error(`web_snapshot_test: ${failures} failure(s)`);
  process.exit(1);
}
console.log(`web_snapshot_test: ok (${cases.length} snapshots)`);
