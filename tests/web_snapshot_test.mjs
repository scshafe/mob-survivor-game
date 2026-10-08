// The web client's snapshot decoder against a snapshot the server encoded.
// usage: node web_snapshot_test.mjs <fixture-dir>   (see snapshot_fixture.cpp)

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { decodeSnapshot, mobIndex } from '../web/js/snapshot.js';

const dir = process.argv[2];
const bytes = readFileSync(join(dir, 'snapshot.bin'));
const expected = JSON.parse(readFileSync(join(dir, 'expected.json'), 'utf8'));
const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);

let failures = 0;
const check = (ok, what) => {
  if (!ok) {
    console.error(`FAIL ${what}`);
    failures++;
  }
};

const snap = decodeSnapshot(buffer);
check(snap !== null, 'decodes');
check(snap.size === expected.size && snap.size === bytes.byteLength, `reads every byte (${snap.size})`);
check(snap.phase === expected.phase, 'phase');
check(snap.level === expected.level, 'level');
check(snap.tick === expected.tick, 'tick');
check(snap.bases[0].hp === expected.blueHp, 'blue base hp');
check(snap.bases[1].max === expected.redMax, 'red base max');
check(snap.cannons.length === expected.cannons, 'cannons');
check(snap.gateX.length === expected.gates, 'gates');
check(snap.events.length === expected.events, 'events');
check(snap.cannons[0].volley === expected.volley, 'first cannon volley');
check(snap.powerups.length === expected.powerups, 'power-ups');
check(snap.cannons.filter((c) => c.phasing).length === expected.phasingCannons, 'phasing cannons');
check(snap.cannons.every((c) => c.phase >= 0 && c.phase <= 1), 'phase meters');
check(snap.mobs.phased.reduce((a, b) => a + b, 0) === expected.phasedMobs, 'phased mobs');
check(expected.phasedMobs > 0 && expected.phasingCannons > 0, 'the fixture has phasing mobs');
check(snap.mobs.kinds.every((k) => k <= 3), 'mob kinds without the phase bit');
check(expected.powerups > 0, 'the fixture has a power-up');
if (expected.firstPowerUp) {
  const p = snap.powerups[0];
  check(p.id === expected.firstPowerUp.id, 'power-up id');
  check(Math.abs(p.x - expected.firstPowerUp.x) <= 0.006, 'power-up x');
  check(Math.abs(p.y - expected.firstPowerUp.y) <= 0.006, 'power-up y');
  check(p.hp === expected.firstPowerUp.hp && p.max === expected.firstPowerUp.max, 'power-up hp');
}
check(snap.mobs.count === expected.mobs, 'mobs');
check(expected.mobs > 0, 'the fixture has mobs');
if (expected.lastMob) {
  const i = mobIndex(snap).get(expected.lastMob.id);
  check(i === snap.mobs.count - 1, 'last mob found by id');
  check(Math.abs(snap.mobs.xs[i] - expected.lastMob.x) <= 0.006, 'last mob x');
  check(Math.abs(snap.mobs.ys[i] - expected.lastMob.y) <= 0.006, 'last mob y');
  check(snap.mobs.teams[i] === expected.lastMob.team, 'last mob team');
  check(snap.mobs.kinds[i] === expected.lastMob.kind, 'last mob kind');
  check(snap.mobs.hps[i] === expected.lastMob.hp, 'last mob hp');
}

if (failures) {
  console.error(`web_snapshot_test: ${failures} failure(s)`);
  process.exit(1);
}
console.log('web_snapshot_test: ok');
