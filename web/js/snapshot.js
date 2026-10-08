// Decodes the server's binary snapshot (layout: docs/protocol.md).
// Coordinates arrive in hundredths of a world unit.

export const Phase = Object.freeze({ Lobby: 0, Countdown: 1, Playing: 2, Upgrade: 3, Over: 4 });
export const Outcome = Object.freeze({ None: 0, Blue: 1, Red: 2, Draw: 3 });
export const MobKind = Object.freeze({ Grunt: 0, Runner: 1, Giant: 2, Brute: 3 });
export const PowerUpKind = Object.freeze({ Shot: 0, Freeze: 1, Flip: 2, Shield: 3, Magnet: 4 });
export const EventType = Object.freeze({
  GatePass: 1,
  BaseHit: 2,
  BombBlast: 3,
  GiantLaunch: 4,
  SawCut: 5,
  Frenzy: 6,
  BossSpawn: 7,
  PowerUp: 8,
  Phase: 9,
  Teleport: 10,
});

const SNAPSHOT_KIND = 1;

export function decodeSnapshot(buffer) {
  const view = new DataView(buffer);
  let at = 0;
  const u8 = () => view.getUint8(at++);
  const u16 = () => {
    const v = view.getUint16(at, true);
    at += 2;
    return v;
  };
  const i16 = () => {
    const v = view.getInt16(at, true);
    at += 2;
    return v;
  };
  const u32 = () => {
    const v = view.getUint32(at, true);
    at += 4;
    return v;
  };
  const i32 = () => {
    const v = view.getInt32(at, true);
    at += 4;
    return v;
  };
  const f32 = () => {
    const v = view.getFloat32(at, true);
    at += 4;
    return v;
  };
  const coord = () => i16() / 100;

  if (u8() !== SNAPSHOT_KIND) return null;
  const snap = {};
  snap.phase = u8();
  snap.level = u16();
  snap.tick = u32();
  snap.timeLeft = f32();
  snap.elapsed = f32();
  snap.frenzy = (u8() & 1) !== 0;
  snap.outcome = u8();
  snap.bases = [];
  for (let t = 0; t < 2; t++) snap.bases.push({ hp: i32(), max: i32() });
  snap.effects = [];
  for (let t = 0; t < 2; t++) snap.effects.push({ shield: i32(), frozen: u8() / 10, flipped: u8() / 10 });

  snap.cannons = [];
  for (let n = u8(), i = 0; i < n; i++) {
    const slot = u8();
    const team = u8();
    const x = coord();
    const charge = u8() / 255;
    const bombCooldown = u8() / 10;
    const flags = u8();
    const volley = u8();
    const phase = u8() / 255;
    snap.cannons.push({
      slot,
      team,
      x,
      charge,
      bombCooldown,
      connected: (flags & 1) !== 0,
      firing: (flags & 2) !== 0,
      giantReady: (flags & 4) !== 0,
      phasing: (flags & 8) !== 0,
      magnet: (flags & 16) !== 0,
      volley,
      phase,
    });
  }

  snap.gateX = [];
  snap.fuseFill = [];
  for (let n = u8(), i = 0; i < n; i++) {
    snap.gateX.push(coord());
    snap.fuseFill.push([u8(), u8()]);
  }
  snap.sawX = [];
  for (let n = u8(), i = 0; i < n; i++) snap.sawX.push(coord());

  snap.bombs = [];
  for (let n = u8(), i = 0; i < n; i++) {
    snap.bombs.push({
      team: u8(),
      fromX: coord(),
      fromY: coord(),
      x: coord(),
      y: coord(),
      radius: coord(),
      fuse: u8() / 100,
    });
  }

  snap.powerups = [];
  for (let n = u8(), i = 0; i < n; i++) {
    snap.powerups.push({ id: u32(), kind: u8(), x: coord(), y: coord(), hp: u16(), max: u16() });
  }

  snap.events = [];
  for (let n = u16(), i = 0; i < n; i++) {
    snap.events.push({ type: u8(), team: u8(), index: i16(), x: coord(), y: coord(), value: i32() });
  }

  const count = u16();
  const ids = new Uint32Array(count);
  const xs = new Float32Array(count);
  const ys = new Float32Array(count);
  const teams = new Uint8Array(count);
  const kinds = new Uint8Array(count);
  const hps = new Uint16Array(count);
  const phased = new Uint8Array(count);
  const armored = new Uint8Array(count);
  for (let i = 0; i < count; i++) {
    ids[i] = u32();
    xs[i] = coord();
    ys[i] = coord();
    const packed = u8();
    teams[i] = packed & 1;
    kinds[i] = (packed >> 1) & 7;
    phased[i] = (packed >> 4) & 1;
    armored[i] = (packed >> 5) & 1;
    hps[i] = u16();
  }
  snap.mobs = { count, ids, xs, ys, teams, kinds, hps, phased, armored };
  snap.size = at;
  return snap;
}

// id -> index, built on first use.
export function mobIndex(snap) {
  if (!snap.index) {
    const map = new Map();
    const { ids, count } = snap.mobs;
    for (let i = 0; i < count; i++) map.set(ids[i], i);
    snap.index = map;
  }
  return snap.index;
}

export function mobRadius(kind, hp) {
  switch (kind) {
    case MobKind.Grunt:
      return Math.min(0.7, 0.3 * Math.sqrt(Math.max(hp, 1)));
    case MobKind.Runner:
      return Math.min(0.7, 0.26 * Math.sqrt(Math.max(hp, 1)));
    case MobKind.Giant:
      return 0.9;
    case MobKind.Brute:
      return 1.5;
    default:
      return 0.3;
  }
}
