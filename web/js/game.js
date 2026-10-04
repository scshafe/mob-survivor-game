// The match as the player sees it: buffers snapshots, interpolates between
// them, turns events into effects and sounds, reads input and draws.

import { decodeSnapshot, mobIndex, Phase, EventType, MobKind, Outcome } from './snapshot.js';
import { Renderer, Effects, TEAM, PLAYER_COLORS } from './render.js';
import { sfx, unlockAudio } from './audio.js';

const TICK = 1 / 30;
const INTERP_DELAY = 0.11;
const CANNON_SPEED = 20;
const KEY_SPEED = 16;
const RING = 119.4; // circumference of the ability rings (r = 19)

const $ = (id) => document.getElementById(id);

export class GameView {
  constructor({ canvas, send }) {
    this.canvas = canvas;
    this.send = send;
    this.renderer = new Renderer(canvas);
    this.effects = new Effects();
    this.level = null;
    this.room = null;
    this.mySlot = -1;
    this.myTeam = 0;
    this.active = false;
    this.reset();

    this.hud = {
      level: $('hud-level'),
      sub: $('hud-sub'),
      army: $('army-count'),
      enemy: $('enemy-count'),
      giant: $('btn-giant'),
      bomb: $('btn-bomb'),
      giantRing: $('btn-giant').querySelector('circle'),
      bombRing: $('btn-bomb').querySelector('circle'),
      banner: $('banner'),
      countdown: $('countdown'),
      toasts: $('toast-stack'),
    };
    this.lastHud = {};
    this.bindInput();
    window.addEventListener('resize', () => this.renderer.resize());
    requestAnimationFrame((t) => this.loop(t));
  }

  reset() {
    this.snaps = [];
    this.prev = null;
    this.next = null;
    this.offset = null;
    this.localX = null;
    this.targetX = 12;
    this.pointerFiring = false;
    this.keyFiring = false;
    this.keys = new Set();
    this.bombArmed = false;
    this.bombArmedAt = 0;
    this.lastSent = { x: -1, f: false, at: 0 };
    this.baseFlash = [0, 0];
    this.shake = 0;
    this.lastPhase = -1;
    this.lastCount = -1;
    this.lastOutcome = Outcome.None;
    this.gateTexts = new Map();
    this.bombMax = 13;
    this.effects = new Effects();
  }

  // ---------------------------------------------------------------- state in

  setRoom(room) {
    this.room = room;
    const me = room.members.find((m) => m.id === room.you && m.id !== 0);
    this.mySlot = me && me.seated && me.slot >= 0 ? me.slot : -1;
    this.myTeam = me ? me.team : 0;
    this.renderer.setFlipped(this.mySlot >= 0 && this.myTeam === 1);
    this.hud.giant.hidden = this.mySlot < 0;
    this.hud.bomb.hidden = this.mySlot < 0;
  }

  setLevel(level) {
    if (!this.level || level.serial !== this.level.serial || level.mode !== this.level.mode) {
      // A new level: the old world's mobs and effects are gone.
      this.prev = null;
      this.next = null;
      this.effects = new Effects();
      this.gateTexts.clear();
      this.localX = null;
    }
    this.level = level;
    this.renderer.setField(level.field);
    if (level.boss) this.showBanner('BOSS LEVEL', 'A brute is coming', '#fca5a5');
  }

  setActive(active) {
    this.active = active;
    if (!active) {
      this.pointerFiring = false;
      this.keyFiring = false;
      this.keys.clear();
      this.setBombArmed(false);
    }
  }

  onSnapshot(buffer) {
    const snap = decodeSnapshot(buffer);
    if (!snap) return;
    snap.time = snap.tick * TICK;
    const now = performance.now() / 1000;
    const latest = this.snaps[this.snaps.length - 1] ?? this.prev;
    if (latest && snap.tick < latest.tick) {
      // A new match restarted the clock.
      this.snaps = [];
      this.prev = null;
      this.offset = null;
    }
    const sample = now - snap.time;
    if (this.offset === null || sample < this.offset) this.offset = sample;
    else this.offset += (sample - this.offset) * 0.02;
    this.snaps.push(snap);
    if (this.snaps.length > 40) this.snaps.splice(0, this.snaps.length - 40);
  }

  // ---------------------------------------------------------------- frame

  loop(ms) {
    const now = ms / 1000;
    const dt = Math.min(0.1, this.lastFrame ? now - this.lastFrame : 0.016);
    this.lastFrame = now;
    if (this.active) {
      try {
        this.update(now, dt);
      } catch (error) {
        console.error(error);
      }
    }
    requestAnimationFrame((t) => this.loop(t));
  }

  update(now, dt) {
    const wallNow = performance.now() / 1000;
    const renderTime = this.offset === null ? 0 : wallNow - this.offset - INTERP_DELAY;
    // Consume every snapshot whose time has come.
    while (this.snaps.length && (this.snaps[0].time <= renderTime || !this.prev)) {
      this.consume(this.snaps.shift());
    }
    this.next = this.snaps[0] ?? null;
    const view = this.interpolate(renderTime);

    this.updateInput(dt, view);
    this.effects.update(dt);
    for (let t = 0; t < 2; t++) this.baseFlash[t] = Math.max(0, this.baseFlash[t] - dt * 2);
    this.shake = Math.max(0, this.shake - dt * 30);
    const shake = { x: (Math.random() - 0.5) * this.shake, y: (Math.random() - 0.5) * this.shake };

    this.renderer.draw({
      time: now,
      level: this.level,
      view,
      mySlot: this.mySlot,
      localX: this.mySlot >= 0 ? this.localX : null,
      aim: this.mySlot >= 0 && this.localX !== null ? { x: this.localX, team: this.myTeam } : null,
      effects: this.effects,
      shake,
      baseFlash: this.baseFlash,
      bombArmed: this.bombArmed,
      pointer: this.pointer,
      colorOf: (slot, team) => this.colorOf(slot, team),
      nameOf: (slot) => this.nameOf(slot),
    });
    if (view) this.updateHud(view);
  }

  interpolate(renderTime) {
    const a = this.prev;
    if (!a) return null;
    const b = this.next;
    const alpha = b && b.time > a.time ? Math.min(1, Math.max(0, (renderTime - a.time) / (b.time - a.time))) : 0;
    const view = {
      phase: a.phase,
      timeLeft: b ? a.timeLeft + (b.timeLeft - a.timeLeft) * alpha : a.timeLeft,
      frenzy: a.frenzy,
      outcome: a.outcome,
      bases: a.bases,
      bombs: a.bombs.map((bomb) => ({ ...bomb, fuse: Math.max(0, bomb.fuse - Math.max(0, renderTime - a.time)) })),
      gateX: a.gateX.map((x, i) => (b && b.gateX[i] !== undefined ? x + (b.gateX[i] - x) * alpha : x)),
      sawX: a.sawX.map((x, i) => (b && b.sawX[i] !== undefined ? x + (b.sawX[i] - x) * alpha : x)),
      cannons: a.cannons.map((cannon) => {
        const later = b?.cannons.find((c) => c.slot === cannon.slot);
        return later ? { ...cannon, x: cannon.x + (later.x - cannon.x) * alpha } : cannon;
      }),
    };
    // Mobs: positions blend from a to b; a mob only in b has just spawned.
    const source = b ?? a;
    const m = source.mobs;
    const xs = new Float32Array(m.count);
    const ys = new Float32Array(m.count);
    if (b && alpha < 1) {
      const index = mobIndex(a);
      for (let i = 0; i < m.count; i++) {
        const j = index.get(m.ids[i]);
        if (j === undefined) {
          xs[i] = m.xs[i];
          ys[i] = m.ys[i];
        } else {
          xs[i] = a.mobs.xs[j] + (m.xs[i] - a.mobs.xs[j]) * alpha;
          ys[i] = a.mobs.ys[j] + (m.ys[i] - a.mobs.ys[j]) * alpha;
        }
      }
    } else {
      xs.set(m.xs);
      ys.set(m.ys);
    }
    view.mobs = { count: m.count, ids: m.ids, xs, ys, teams: m.teams, kinds: m.kinds, hps: m.hps };
    return view;
  }

  // A snapshot becomes "now": play its events and mark the mobs that died.
  consume(snap) {
    const before = this.prev;
    this.prev = snap;
    if (before && this.level && snap.level === before.level) {
      const index = mobIndex(snap);
      const m = before.mobs;
      const H = this.level.field.h;
      const depth = this.level.field.baseDepth;
      let poofs = 0;
      for (let i = 0; i < m.count && poofs < 80; i++) {
        if (index.has(m.ids[i])) continue;
        const atBase = m.teams[i] === 0 ? m.ys[i] >= H - depth - 0.9 : m.ys[i] <= depth + 0.9;
        if (atBase) continue;
        const big = m.kinds[i] >= MobKind.Giant;
        this.effects.poof(m.xs[i], m.ys[i], m.teams[i], big ? 16 : 3);
        if (big) this.effects.ring(m.xs[i], m.ys[i], 2.2, TEAM[m.teams[i]].main, 0.5);
        poofs++;
      }
      if (poofs > 0) sfx.pop();
    }
    for (const event of snap.events) this.playEvent(event);
    this.onPhase(snap);
  }

  playEvent(event) {
    const mine = this.mySlot < 0 ? 0 : this.myTeam;
    switch (event.type) {
      case EventType.GatePass: {
        const key = `${event.index}:${event.team}`;
        const existing = this.gateTexts.get(key);
        const color = event.value < 0 ? '#fb7185' : TEAM[event.team].light;
        const dir = event.team === 0 ? 1 : -1;
        if (existing && existing.age < 0.35 && this.effects.texts.includes(existing)) {
          existing.total += event.value;
          existing.text = existing.total > 0 ? `+${existing.total}` : `${existing.total}`;
          existing.age = 0.05;
        } else {
          const text = event.value > 0 ? `+${event.value}` : `${event.value}`;
          this.effects.text(event.x, event.y + dir * 1.2, text, color, 1.1, 0.9);
          const created = this.effects.texts[this.effects.texts.length - 1];
          created.total = event.value;
          this.gateTexts.set(key, created);
        }
        if (event.team === mine) {
          if (event.value > 0) sfx.gate(event.value);
          else sfx.halve();
        }
        break;
      }
      case EventType.BaseHit: {
        this.baseFlash[event.team] = 0.35;
        const y = event.team === 0 ? 1.8 : this.level.field.h - 1.8;
        // One running total per base while hits keep coming.
        const key = `base:${event.team}`;
        const existing = this.gateTexts.get(key);
        if (existing && existing.age < 0.5 && this.effects.texts.includes(existing)) {
          existing.total += event.value;
          existing.text = `-${existing.total}`;
          existing.age = 0.1;
        } else {
          this.effects.text(this.level.field.w / 2, y, `-${event.value}`, '#fde68a', 1.1, 0.9);
          const created = this.effects.texts[this.effects.texts.length - 1];
          created.total = event.value;
          this.gateTexts.set(key, created);
        }
        this.effects.sparks(event.x, y, '#fde68a', 3);
        if (event.team === mine) {
          this.shake = Math.max(this.shake, 6);
          sfx.hitOwnBase();
        } else {
          sfx.hitEnemyBase();
        }
        break;
      }
      case EventType.BombBlast:
        this.effects.ring(event.x, event.y, 2.8, '#fb923c', 0.55);
        this.effects.ring(event.x, event.y, 1.6, '#fde68a', 0.35);
        this.effects.sparks(event.x, event.y, '#fb923c', 22);
        if (event.value > 0) this.effects.text(event.x, event.y, `💥 ${event.value}`, '#fed7aa', 1.2, 1.0);
        this.shake = Math.max(this.shake, 10);
        sfx.bomb();
        break;
      case EventType.GiantLaunch:
        this.effects.ring(event.x, event.y, 2.2, TEAM[event.team].light, 0.5);
        sfx.giant();
        break;
      case EventType.SawCut:
        this.effects.sparks(event.x, event.y, '#e2e8f0', 3);
        sfx.saw();
        break;
      case EventType.Frenzy:
        this.showBanner('FRENZY!', 'Every gate is stronger', '#e9d5ff');
        sfx.frenzy();
        break;
      case EventType.BossSpawn:
        this.showBanner('BOSS!', `${event.value} hp`, '#fca5a5');
        this.shake = Math.max(this.shake, 14);
        sfx.boss();
        break;
      default:
        break;
    }
  }

  onPhase(snap) {
    if (snap.phase === Phase.Countdown) {
      const count = Math.max(1, Math.ceil(snap.timeLeft));
      if (count !== this.lastCount) {
        this.lastCount = count;
        this.hud.countdown.hidden = false;
        this.hud.countdown.textContent = String(count);
        sfx.beep(false);
      }
    } else {
      this.hud.countdown.hidden = true;
      if (this.lastPhase === Phase.Countdown && snap.phase === Phase.Playing) {
        this.showBanner('GO!', '', '#bbf7d0', 900);
        sfx.beep(true);
      }
      this.lastCount = -1;
    }
    if (snap.phase === Phase.Playing && snap.outcome !== Outcome.None && this.lastOutcome === Outcome.None) {
      const mine = this.mySlot < 0 ? 0 : this.myTeam;
      const won = (snap.outcome === Outcome.Blue && mine === 0) || (snap.outcome === Outcome.Red && mine === 1);
      const campaign = this.level?.mode === 'campaign';
      if (snap.outcome === Outcome.Draw) {
        this.showBanner('DRAW', '', '#e2e8f0', 2000);
      } else if (won) {
        this.showBanner(campaign ? 'LEVEL CLEARED!' : 'VICTORY!', '', '#fde68a', 2000);
        sfx.win();
      } else {
        this.showBanner(campaign ? 'BASE DESTROYED' : 'DEFEAT', '', '#fca5a5', 2000);
        sfx.lose();
      }
    }
    this.lastOutcome = snap.outcome;
    this.lastPhase = snap.phase;
  }

  // ---------------------------------------------------------------- HUD

  updateHud(view) {
    const set = (key, value, apply) => {
      if (this.lastHud[key] !== value) {
        this.lastHud[key] = value;
        apply(value);
      }
    };
    const level = this.level;
    const campaign = level?.mode === 'campaign';
    const enemyTeam = this.mySlot >= 0 ? 1 - this.myTeam : 1;
    const ownTeam = 1 - enemyTeam;
    set('title', campaign ? `Level ${this.prev.level}${level?.boss ? ' · Boss' : ''}` : 'Versus', (v) => (this.hud.level.textContent = v));
    let sub;
    if (view.phase === Phase.Upgrade) {
      sub = `Next level in ${Math.ceil(view.timeLeft)}s`;
    } else if (!campaign && view.phase === Phase.Playing) {
      const t = Math.max(0, Math.ceil(view.timeLeft));
      sub = `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}${view.frenzy ? ' · FRENZY' : ''}`;
    } else if (this.mySlot < 0) {
      sub = 'Spectating';
    } else {
      sub = campaign ? 'Destroy the enemy base' : 'Destroy their base';
    }
    set('sub', sub, (v) => (this.hud.sub.textContent = v));

    let own = 0;
    let enemy = 0;
    const m = view.mobs;
    for (let i = 0; i < m.count; i++) {
      if (m.teams[i] === ownTeam) own += m.hps[i];
      else enemy += m.hps[i];
    }
    set('army', own, (v) => (this.hud.army.textContent = String(v)));
    set('enemy', enemy, (v) => (this.hud.enemy.textContent = String(v)));

    const cannon = view.cannons.find((c) => c.slot === this.mySlot);
    if (cannon) {
      set('charge', Math.round(cannon.charge * 100), (v) => {
        this.hud.giantRing.style.strokeDashoffset = String(RING * (1 - v / 100));
      });
      set('giantReady', cannon.giantReady, (v) => this.hud.giant.classList.toggle('ready', v));
      // A jump in the cooldown means a bomb just went: that is the full wait.
      if (cannon.bombCooldown > (this.lastBombCooldown ?? 0) + 0.5) this.bombMax = cannon.bombCooldown;
      this.lastBombCooldown = cannon.bombCooldown;
      const bombProgress = cannon.bombCooldown <= 0 ? 1 : 1 - cannon.bombCooldown / Math.max(this.bombMax, 0.1);
      set('bomb', Math.round(bombProgress * 100), (v) => {
        this.hud.bombRing.style.strokeDashoffset = String(RING * (1 - v / 100));
      });
      set('bombReady', cannon.bombCooldown <= 0, (v) => this.hud.bomb.classList.toggle('ready', v));
    }
  }

  showBanner(title, subtitle, color = '#fff', ms = 1600) {
    const banner = this.hud.banner;
    banner.innerHTML = '';
    const strong = document.createElement('div');
    strong.textContent = title;
    banner.appendChild(strong);
    if (subtitle) {
      const small = document.createElement('small');
      small.textContent = subtitle;
      banner.appendChild(small);
    }
    banner.style.color = color;
    banner.hidden = false;
    // Restart the pop animation.
    banner.style.animation = 'none';
    void banner.offsetWidth;
    banner.style.animation = '';
    clearTimeout(this.bannerTimer);
    this.bannerTimer = setTimeout(() => (banner.hidden = true), ms);
  }

  toast(node) {
    const toast = document.createElement('div');
    toast.className = 'toast';
    toast.appendChild(node);
    this.hud.toasts.appendChild(toast);
    setTimeout(() => toast.remove(), 3000);
    while (this.hud.toasts.children.length > 4) this.hud.toasts.firstChild.remove();
  }

  showEmote(message, glyph) {
    const node = document.createElement('span');
    const name = document.createElement('b');
    name.textContent = `${message.name} `;
    const big = document.createElement('span');
    big.className = 'big';
    big.textContent = glyph;
    node.append(name, big);
    this.toast(node);
    sfx.emote();
    const cannon = this.prev?.cannons.find((c) => c.slot === message.slot);
    if (cannon && this.level) {
      const y = cannon.team === 0 ? this.level.field.cannonOffset + 2 : this.level.field.h - this.level.field.cannonOffset - 2;
      this.effects.text(cannon.x, y, glyph, '#fff', 2.2, 1.6);
    }
  }

  // ---------------------------------------------------------------- players

  seatedOnTeam(team) {
    if (!this.room) return [];
    return this.room.members.filter((m) => m.seated && m.slot >= 0 && m.team === team).sort((a, b) => a.slot - b.slot);
  }

  colorOf(slot, team) {
    const rank = this.seatedOnTeam(team).findIndex((m) => m.slot === slot);
    return PLAYER_COLORS[team][Math.max(0, rank) % 4];
  }

  nameOf(slot) {
    return this.room?.members.find((m) => m.slot === slot && m.seated)?.name ?? '';
  }

  // ---------------------------------------------------------------- input

  canControl() {
    return this.active && this.mySlot >= 0 && this.prev && (this.prev.phase === Phase.Playing || this.prev.phase === Phase.Countdown);
  }

  bindInput() {
    const canvas = this.canvas;
    const toWorld = (event) => {
      const rect = canvas.getBoundingClientRect();
      return this.renderer.toWorld(event.clientX - rect.left, event.clientY - rect.top);
    };
    const localPoint = (event) => {
      const rect = canvas.getBoundingClientRect();
      return { x: event.clientX - rect.left, y: event.clientY - rect.top };
    };

    canvas.addEventListener('pointerdown', (event) => {
      unlockAudio();
      if (!this.canControl()) return;
      event.preventDefault();
      this.pointer = localPoint(event);
      const world = toWorld(event);
      if (this.bombArmed || event.button === 2) {
        this.throwBomb(world);
        return;
      }
      if (event.button !== 0) return;
      canvas.setPointerCapture?.(event.pointerId);
      this.pointerFiring = true;
      this.targetX = world.x;
    });
    canvas.addEventListener('pointermove', (event) => {
      this.pointer = localPoint(event);
      if (!this.canControl()) return;
      if (this.pointerFiring || event.pointerType === 'mouse') this.targetX = toWorld(event).x;
    });
    const release = () => {
      this.pointerFiring = false;
    };
    canvas.addEventListener('pointerup', release);
    canvas.addEventListener('pointercancel', release);
    canvas.addEventListener('pointerleave', (event) => {
      if (event.pointerType === 'mouse') this.pointer = null;
    });
    canvas.addEventListener('contextmenu', (event) => event.preventDefault());

    window.addEventListener('keydown', (event) => {
      if (!this.active || event.target instanceof HTMLInputElement) return;
      const key = event.key.toLowerCase();
      if (['arrowleft', 'arrowright', 'a', 'd', ' '].includes(key)) event.preventDefault();
      if (event.repeat) return;
      unlockAudio();
      if (key === ' ') this.keyFiring = true;
      else if (key === 'arrowleft' || key === 'a') this.keys.add('left');
      else if (key === 'arrowright' || key === 'd') this.keys.add('right');
      else if (key === 'g' || key === 'e') this.launchGiant();
      else if (key === 'b' || key === 'q') {
        if (!this.canControl()) return;
        const target = this.pointer
          ? this.renderer.toWorld(this.pointer.x, this.pointer.y)
          : { x: this.localX ?? 12, y: this.myTeam === 0 ? 18 : this.level.field.h - 18 };
        this.throwBomb(target);
      } else if (key === 'escape') this.setBombArmed(false);
    });
    window.addEventListener('keyup', (event) => {
      const key = event.key.toLowerCase();
      if (key === ' ') this.keyFiring = false;
      else if (key === 'arrowleft' || key === 'a') this.keys.delete('left');
      else if (key === 'arrowright' || key === 'd') this.keys.delete('right');
    });
    window.addEventListener('blur', () => {
      this.keys.clear();
      this.keyFiring = false;
      this.pointerFiring = false;
    });

    const tap = (element, handler) => {
      element.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        event.stopPropagation();
        unlockAudio();
        handler();
      });
    };
    tap(this.hud?.giant ?? $('btn-giant'), () => this.launchGiant());
    tap(this.hud?.bomb ?? $('btn-bomb'), () => {
      if (this.canControl()) this.setBombArmed(!this.bombArmed);
    });
  }

  launchGiant() {
    if (!this.canControl()) return;
    const cannon = this.prev.cannons.find((c) => c.slot === this.mySlot);
    if (cannon?.giantReady) this.send({ t: 'giant' });
  }

  setBombArmed(armed) {
    this.bombArmed = armed;
    this.bombArmedAt = performance.now();
    this.hud?.bomb.classList.toggle('armed', armed);
  }

  throwBomb(world) {
    const cannon = this.prev?.cannons.find((c) => c.slot === this.mySlot);
    this.setBombArmed(false);
    if (!cannon || cannon.bombCooldown > 0) return;
    this.send({ t: 'bomb', x: +world.x.toFixed(2), y: +world.y.toFixed(2) });
    sfx.throwBomb();
  }

  updateInput(dt) {
    if (!this.canControl()) return;
    const W = this.level?.field.w ?? 24;
    const flip = this.myTeam === 1 ? -1 : 1;
    if (this.keys.has('left')) this.targetX -= KEY_SPEED * dt * flip;
    if (this.keys.has('right')) this.targetX += KEY_SPEED * dt * flip;
    this.targetX = Math.min(W - 0.6, Math.max(0.6, this.targetX));
    if (this.bombArmed && performance.now() - this.bombArmedAt > 5000) this.setBombArmed(false);

    // Predict our own cannon so it answers the finger at once.
    const server = this.prev.cannons.find((c) => c.slot === this.mySlot);
    if (server && (this.localX === null || Math.abs(this.localX - server.x) > 3)) this.localX = server.x;
    if (this.localX !== null && this.prev.phase === Phase.Playing) {
      const step = CANNON_SPEED * dt;
      this.localX += Math.max(-step, Math.min(step, this.targetX - this.localX));
    }

    const firing = this.pointerFiring || this.keyFiring;
    const now = performance.now() / 1000;
    const moved = Math.abs(this.targetX - this.lastSent.x) > 0.05;
    const changed = firing !== this.lastSent.f;
    const since = now - this.lastSent.at;
    if ((changed || (moved && since > 0.05) || since > 0.5) && this.send({ t: 'in', x: +this.targetX.toFixed(2), f: firing })) {
      this.lastSent = { x: this.targetX, f: firing, at: now };
    }
  }
}
