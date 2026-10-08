// Draws the arena on a <canvas>. World units: the field is W x H with Blue
// at y = 0 and Red at y = H; whoever is watching sees their own base at the
// bottom of the screen (Red's view is turned 180 degrees).

import { MobKind, mobRadius } from './snapshot.js';

export const TEAM = [
  { main: '#3b82f6', dark: '#1e3a8a', light: '#bfdbfe', glow: 'rgba(59,130,246,0.55)', name: 'Blue' },
  { main: '#ef4444', dark: '#7f1d1d', light: '#fecaca', glow: 'rgba(239,68,68,0.55)', name: 'Red' },
];
export const PLAYER_COLORS = [
  ['#3b82f6', '#22d3ee', '#a78bfa', '#2dd4bf'],
  ['#ef4444', '#fb923c', '#f472b6', '#facc15'],
];

const GATE_STYLE = {
  mul: { fill: 'rgba(56,189,248,0.30)', edge: '#38bdf8', text: '#e0f2fe' },
  add: { fill: 'rgba(74,222,128,0.30)', edge: '#4ade80', text: '#dcfce7' },
  half: { fill: 'rgba(244,63,94,0.32)', edge: '#fb7185', text: '#ffe4e6' },
  fuse: { fill: 'rgba(250,204,21,0.30)', edge: '#facc15', text: '#fef9c3' },
  runner: { fill: 'rgba(251,146,60,0.30)', edge: '#fb923c', text: '#ffedd5' },
  armor: { fill: 'rgba(148,163,184,0.38)', edge: '#cbd5e1', text: '#f8fafc' },
};
const SHARED_EDGE = '#c084fc';
// Power-up orbs by kind (PowerUpKind): gradient light and dark, and a glyph.
const ORB_STYLE = [
  { light: '#fef9c3', dark: '#ca8a04', glow: 'rgba(253,224,71,0.25)', ring: '#fde047', glyph: '+1', ink: '#422006' },
  { light: '#e0f2fe', dark: '#0284c7', glow: 'rgba(125,211,252,0.28)', ring: '#7dd3fc', glyph: '❄', ink: '#f0f9ff' },
  { light: '#f5d0fe', dark: '#a21caf', glow: 'rgba(232,121,249,0.25)', ring: '#e879f9', glyph: '÷2', ink: '#fdf4ff' },
  { light: '#f8fafc', dark: '#64748b', glow: 'rgba(226,232,240,0.28)', ring: '#e2e8f0', glyph: '🛡', ink: '#0f172a' },
  { light: '#dcfce7', dark: '#15803d', glow: 'rgba(134,239,172,0.25)', ring: '#86efac', glyph: '🧲', ink: '#052e16' },
];

export function gateLabel(gate, frenzy) {
  if (gate.op === 'mul') return `×${gate.v + (frenzy ? 1 : 0)}`;
  if (gate.op === 'add') return `+${gate.v + (frenzy ? Math.floor(gate.v / 2) : 0)}`;
  if (gate.op === 'fuse') return `${gate.v}→👑`;
  if (gate.op === 'runner') return '»RUN»';
  if (gate.op === 'armor') return '⛨ ARMOR';
  return '÷2';
}

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.W = 24;
    this.H = 40;
    this.baseDepth = 2.5;
    this.cannonOffset = 3.4;
    this.flipped = false;
    this.insets = { top: 64, bottom: 118 };
    this.background = null;
    this.resize();
  }

  setField(field) {
    this.W = field.w;
    this.H = field.h;
    this.baseDepth = field.baseDepth;
    this.cannonOffset = field.cannonOffset;
    this.resize();
  }

  setFlipped(flipped) {
    if (this.flipped !== flipped) {
      this.flipped = flipped;
      this.background = null;
    }
  }

  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    const width = this.canvas.clientWidth || window.innerWidth;
    const height = this.canvas.clientHeight || window.innerHeight;
    this.dpr = dpr;
    this.width = width;
    this.height = height;
    this.canvas.width = Math.round(width * dpr);
    this.canvas.height = Math.round(height * dpr);
    const usableH = Math.max(100, height - this.insets.top - this.insets.bottom);
    this.scale = Math.max(4, Math.min((width - 16) / this.W, usableH / this.H));
    this.ox = (width - this.W * this.scale) / 2;
    this.oy = this.insets.top + (usableH - this.H * this.scale) / 2;
    this.background = null;
  }

  // World -> CSS pixels.
  sx(x) {
    return this.ox + (this.flipped ? this.W - x : x) * this.scale;
  }
  sy(y) {
    return this.oy + (this.flipped ? y : this.H - y) * this.scale;
  }
  // CSS pixels -> world.
  toWorld(px, py) {
    let x = (px - this.ox) / this.scale;
    let y = this.H - (py - this.oy) / this.scale;
    if (this.flipped) {
      x = this.W - x;
      y = this.H - y;
    }
    return { x, y };
  }
  // +1 when a team's march goes up the screen.
  screenUp(team) {
    const up = team === 0 ? 1 : -1;
    return this.flipped ? -up : up;
  }

  buildBackground() {
    const canvas = document.createElement('canvas');
    canvas.width = this.canvas.width;
    canvas.height = this.canvas.height;
    const ctx = canvas.getContext('2d');
    ctx.scale(this.dpr, this.dpr);
    const s = this.scale;

    const outside = ctx.createLinearGradient(0, 0, 0, this.height);
    outside.addColorStop(0, '#0f172a');
    outside.addColorStop(1, '#1e1b4b');
    ctx.fillStyle = outside;
    ctx.fillRect(0, 0, this.width, this.height);
    ctx.fillStyle = 'rgba(255,255,255,0.035)';
    for (let y = 0; y < this.height; y += 26) {
      for (let x = (y / 26) % 2 ? 13 : 0; x < this.width; x += 26) {
        ctx.beginPath();
        ctx.arc(x, y, 1.6, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    const left = this.ox;
    const top = this.oy;
    const w = this.W * s;
    const h = this.H * s;
    ctx.save();
    ctx.shadowColor = 'rgba(0,0,0,0.55)';
    ctx.shadowBlur = 30;
    ctx.fillStyle = '#3f8f4f';
    roundRect(ctx, left, top, w, h, 10);
    ctx.fill();
    ctx.restore();

    ctx.save();
    roundRect(ctx, left, top, w, h, 10);
    ctx.clip();
    const grass = ctx.createLinearGradient(0, top, 0, top + h);
    grass.addColorStop(0, '#4d9a5b');
    grass.addColorStop(0.5, '#5fae67');
    grass.addColorStop(1, '#4d9a5b');
    ctx.fillStyle = grass;
    ctx.fillRect(left, top, w, h);
    // Mown stripes every 4 units.
    for (let y = 0; y < this.H; y += 4) {
      if ((y / 4) % 2 === 0) continue;
      ctx.fillStyle = 'rgba(255,255,255,0.045)';
      ctx.fillRect(left, this.sy(y + 4) < this.sy(y) ? this.sy(y + 4) : this.sy(y), w, 4 * s);
    }
    // Centre line.
    ctx.strokeStyle = 'rgba(255,255,255,0.18)';
    ctx.setLineDash([s * 0.6, s * 0.6]);
    ctx.lineWidth = Math.max(1, s * 0.08);
    ctx.beginPath();
    ctx.moveTo(left, this.sy(this.H / 2));
    ctx.lineTo(left + w, this.sy(this.H / 2));
    ctx.stroke();
    ctx.setLineDash([]);
    // Tufts of grass.
    let seed = 7;
    const rand = () => {
      seed = (seed * 16807) % 2147483647;
      return seed / 2147483647;
    };
    ctx.strokeStyle = 'rgba(30,80,40,0.35)';
    ctx.lineWidth = 1;
    for (let i = 0; i < 140; i++) {
      const x = left + rand() * w;
      const y = top + rand() * h;
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x - 2, y - 4);
      ctx.moveTo(x, y);
      ctx.lineTo(x + 2, y - 5);
      ctx.stroke();
    }
    ctx.restore();

    ctx.strokeStyle = 'rgba(0,0,0,0.35)';
    ctx.lineWidth = 3;
    roundRect(ctx, left, top, w, h, 10);
    ctx.stroke();
    this.background = canvas;
  }

  draw(frame) {
    const ctx = this.ctx;
    if (!this.background) this.buildBackground();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    ctx.drawImage(this.background, 0, 0);
    ctx.setTransform(this.dpr, 0, 0, this.dpr, frame.shake.x * this.dpr, frame.shake.y * this.dpr);
    if (!frame.level || !frame.view) return;
    const view = frame.view;

    this.drawBases(view, frame);
    this.drawGates(frame.level, view, frame.time);
    this.drawSaws(frame.level, view, frame.time);
    if (frame.aim) this.drawAim(frame.aim, frame.level);
    this.drawMobs(view);
    this.drawPowerUps(view, frame.time, frame.level);
    this.drawCannons(view, frame);
    this.drawBombs(view, frame.time);
    frame.effects.draw(ctx, this);
    if (frame.bombAim) this.drawCrosshair(frame.bombAim, frame.time, frame.aimCount);
    if (view.frenzy) this.drawFrenzy(frame.time);
  }

  drawBases(view, frame) {
    const ctx = this.ctx;
    const s = this.scale;
    for (let team = 0; team < 2; team++) {
      const colors = TEAM[team];
      const near = team === 0 ? 0 : this.H;
      const far = team === 0 ? this.baseDepth : this.H - this.baseDepth;
      const y1 = Math.min(this.sy(near), this.sy(far));
      const y2 = Math.max(this.sy(near), this.sy(far));
      const left = this.ox;
      const width = this.W * s;
      const base = view.bases[team];
      const ratio = base && base.max > 0 ? Math.max(0, base.hp / base.max) : 1;

      ctx.fillStyle = colors.dark;
      ctx.fillRect(left, y1, width, y2 - y1);
      // Brick rows.
      ctx.strokeStyle = 'rgba(0,0,0,0.25)';
      ctx.lineWidth = 1;
      const brick = s * 0.8;
      for (let row = 0, y = y1; y < y2; y += brick * 0.6, row++) {
        ctx.beginPath();
        ctx.moveTo(left, y);
        ctx.lineTo(left + width, y);
        ctx.stroke();
        for (let x = left + (row % 2 ? brick / 2 : 0); x < left + width; x += brick) {
          ctx.beginPath();
          ctx.moveTo(x, y);
          ctx.lineTo(x, Math.min(y + brick * 0.6, y2));
          ctx.stroke();
        }
      }
      // Battlements facing the field.
      const front = this.sy(far);
      const inward = this.sy(far) === y1 ? -1 : 1;
      ctx.fillStyle = colors.main;
      const merlon = s * 1.0;
      for (let x = left; x < left + width - 1; x += merlon * 2) {
        ctx.fillRect(x, inward < 0 ? front - merlon * 0.55 : front, merlon, merlon * 0.55);
      }
      ctx.fillRect(left, inward < 0 ? front - 2 : front, width, 3);
      // The keep: a flag that droops as the base takes damage.
      const keepX = this.sx(this.W / 2);
      const keepY = (y1 + y2) / 2;
      ctx.fillStyle = colors.main;
      roundRect(ctx, keepX - s * 1.6, keepY - s * 0.9, s * 3.2, s * 1.8, s * 0.3);
      ctx.fill();
      ctx.fillStyle = 'rgba(0,0,0,0.4)';
      roundRect(ctx, keepX - s * 0.5, keepY - s * 0.2, s * 1.0, s * 1.1, s * 0.5);
      ctx.fill();
      // Health bar on the wall.
      const barW = width * 0.6;
      const barX = left + (width - barW) / 2;
      const barY = inward < 0 ? y2 - s * 0.55 : y1 + s * 0.15;
      ctx.fillStyle = 'rgba(0,0,0,0.5)';
      roundRect(ctx, barX, barY, barW, s * 0.4, s * 0.2);
      ctx.fill();
      ctx.fillStyle = ratio > 0.5 ? '#4ade80' : ratio > 0.25 ? '#facc15' : '#f87171';
      if (ratio > 0) {
        roundRect(ctx, barX, barY, barW * ratio, s * 0.4, s * 0.2);
        ctx.fill();
      }
      // A power-up shield: a bright rim along the wall, with what it still soaks up.
      const shield = view.effects?.[team]?.shield ?? 0;
      if (shield > 0) {
        const rim = this.sy(far) + (inward < 0 ? -s * 0.9 : s * 0.9);
        ctx.strokeStyle = `rgba(226,232,240,${0.55 + 0.25 * Math.sin(frame.time * 5)})`;
        ctx.lineWidth = Math.max(3, s * 0.25);
        ctx.beginPath();
        ctx.moveTo(left + s * 0.3, rim);
        ctx.lineTo(left + width - s * 0.3, rim);
        ctx.stroke();
        ctx.font = `800 ${Math.max(10, s * 0.55)}px system-ui, sans-serif`;
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#f8fafc';
        ctx.fillText(`🛡 ${shield}`, left + width - s * 0.4, rim + (inward < 0 ? -s * 0.6 : s * 0.6));
      }
      // Hit flash.
      const flash = frame.baseFlash[team];
      if (flash > 0) {
        ctx.fillStyle = `rgba(255,255,255,${Math.min(0.5, flash)})`;
        ctx.fillRect(left, y1, width, y2 - y1);
      }
    }
  }

  drawGates(level, view, time) {
    const ctx = this.ctx;
    const s = this.scale;
    const fontSize = Math.max(12, s * 1.1);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    level.gates.forEach((gate, i) => {
      const x = view.gateX[i] ?? gate.x;
      const style = GATE_STYLE[gate.op] || GATE_STYLE.add;
      const shared = gate.teams === 3;
      const half = gate.w / 2;
      const xa = this.sx(x - half);
      const xb = this.sx(x + half);
      const left = Math.min(xa, xb);
      const width = Math.abs(xb - xa);
      const top = this.sy(gate.y) - s * 0.55;
      const height = s * 1.1;
      const pulse = view.frenzy ? 0.5 + 0.5 * Math.sin(time * 10) : 0;

      ctx.fillStyle = style.fill;
      roundRect(ctx, left, top, width, height, s * 0.2);
      ctx.fill();
      ctx.lineWidth = Math.max(2, s * 0.12) + pulse * 2;
      ctx.strokeStyle = shared ? SHARED_EDGE : style.edge;
      ctx.stroke();
      // Posts.
      ctx.fillStyle = shared ? SHARED_EDGE : style.edge;
      for (const px of [left, left + width]) {
        ctx.beginPath();
        ctx.arc(px, top + height / 2, s * 0.32, 0, Math.PI * 2);
        ctx.fill();
      }
      const label = gateLabel(gate, view.frenzy) + (shared ? ' ⚔' : '');
      ctx.font = `900 ${fontSize}px system-ui, sans-serif`;
      // Long labels shrink to fit their gate.
      const room = width - s * 0.9;
      const measured = ctx.measureText(label).width;
      const size = measured > room ? Math.max(8, fontSize * (room / measured)) : fontSize;
      ctx.font = `900 ${size}px system-ui, sans-serif`;
      ctx.lineWidth = Math.max(2, size * 0.18);
      ctx.strokeStyle = 'rgba(0,0,0,0.55)';
      ctx.strokeText(label, left + width / 2, top + height / 2 + 1);
      ctx.fillStyle = style.text;
      ctx.fillText(label, left + width / 2, top + height / 2 + 1);
      // A fuse gate fills toward its next giant, one bar per team.
      if (gate.op === 'fuse') {
        for (let team = 0; team < 2; team++) {
          const fill = view.fuseFill?.[i]?.[team] ?? 0;
          if (fill <= 0) continue;
          const barY = top + height - s * 0.22 - team * s * 0.2;
          ctx.fillStyle = TEAM[team].main;
          ctx.fillRect(left + s * 0.3, barY, (width - s * 0.6) * Math.min(1, fill / Math.max(gate.v, 1)), s * 0.16);
        }
      }
      if (gate.moving) {
        ctx.fillStyle = 'rgba(255,255,255,0.5)';
        ctx.font = `700 ${fontSize * 0.55}px system-ui, sans-serif`;
        ctx.fillText('⇄', left + width / 2, top - fontSize * 0.35);
      }
      // Flipped by a power-up: "/2" for that team, on the side it comes from.
      for (let team = 0; team < 2; team++) {
        if (!(gate.teams & (1 << team)) || !(view.effects?.[team]?.flipped > 0)) continue;
        const side = this.screenUp(team) > 0 ? 1 : -1; // the team arrives from below on screen when up
        const bx = left + width / 2;
        const by = top + height / 2 + side * height * 0.95;
        ctx.fillStyle = `rgba(190,24,93,${0.75 + 0.2 * Math.sin(time * 12)})`;
        roundRect(ctx, bx - fontSize * 1.1, by - fontSize * 0.42, fontSize * 2.2, fontSize * 0.84, fontSize * 0.3);
        ctx.fill();
        ctx.font = `900 ${fontSize * 0.62}px system-ui, sans-serif`;
        ctx.fillStyle = '#fff';
        ctx.fillText(`÷2 ${TEAM[team].name}`, bx, by + 1);
      }
    });
  }

  drawSaws(level, view, time) {
    const ctx = this.ctx;
    const s = this.scale;
    level.saws.forEach((saw, i) => {
      const x = this.sx(view.sawX[i] ?? this.W / 2);
      const y = this.sy(saw.y);
      const r = saw.r * s;
      // Rail.
      ctx.strokeStyle = 'rgba(0,0,0,0.25)';
      ctx.lineWidth = Math.max(2, s * 0.15);
      ctx.beginPath();
      ctx.moveTo(this.ox + s * 0.5, y);
      ctx.lineTo(this.ox + this.W * s - s * 0.5, y);
      ctx.stroke();
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(time * 12);
      ctx.fillStyle = '#cbd5e1';
      ctx.beginPath();
      const teeth = 12;
      for (let t = 0; t <= teeth * 2; t++) {
        const angle = (t / (teeth * 2)) * Math.PI * 2;
        const radius = t % 2 === 0 ? r * 1.12 : r * 0.82;
        ctx.lineTo(Math.cos(angle) * radius, Math.sin(angle) * radius);
      }
      ctx.fill();
      ctx.strokeStyle = '#64748b';
      ctx.lineWidth = 1.5;
      ctx.stroke();
      ctx.fillStyle = '#475569';
      ctx.beginPath();
      ctx.arc(0, 0, r * 0.3, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    });
  }

  drawAim(aim, level) {
    // A faint guide from the player's cannon up to the first gate row.
    const ctx = this.ctx;
    const s = this.scale;
    const fromY = aim.team === 0 ? this.cannonOffset : this.H - this.cannonOffset;
    let toY = aim.team === 0 ? this.H / 2 : this.H / 2;
    for (const gate of level.gates) {
      if (aim.team === 0 && gate.y > fromY) toY = Math.min(toY, gate.y);
      if (aim.team === 1 && gate.y < fromY) toY = Math.max(toY, gate.y);
    }
    ctx.strokeStyle = 'rgba(255,255,255,0.28)';
    ctx.lineWidth = Math.max(1.5, s * 0.08);
    ctx.setLineDash([s * 0.4, s * 0.4]);
    ctx.beginPath();
    ctx.moveTo(this.sx(aim.x), this.sy(fromY));
    ctx.lineTo(this.sx(aim.x), this.sy(toY));
    ctx.stroke();
    ctx.setLineDash([]);
  }

  drawMobs(view) {
    const ctx = this.ctx;
    const s = this.scale;
    const m = view.mobs;
    const n = m.count;
    // Shadows.
    ctx.fillStyle = 'rgba(0,0,0,0.22)';
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      if (m.kinds[i] >= MobKind.Giant) continue;
      const r = mobRadius(m.kinds[i], m.hps[i]) * s;
      const x = this.sx(m.xs[i]);
      const y = this.sy(m.ys[i]) + r * 0.35;
      ctx.moveTo(x + r, y);
      ctx.ellipse(x, y, r, r * 0.55, 0, 0, Math.PI * 2);
    }
    ctx.fill();
    // Frozen teams: a frosty halo.
    for (let team = 0; team < 2; team++) {
      if (!(view.effects?.[team]?.frozen > 0)) continue;
      ctx.fillStyle = 'rgba(186,230,253,0.5)';
      ctx.beginPath();
      for (let i = 0; i < n; i++) {
        if (m.teams[i] !== team) continue;
        const r = mobRadius(m.kinds[i], m.hps[i]) * s * 1.35;
        const x = this.sx(m.xs[i]);
        const y = this.sy(m.ys[i]);
        ctx.moveTo(x + r, y);
        ctx.arc(x, y, r, 0, Math.PI * 2);
      }
      ctx.fill();
    }
    // Phasing mobs shimmer: a pale aura under the body.
    ctx.fillStyle = 'rgba(165,243,252,0.45)';
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      if (!m.phased[i]) continue;
      const r = mobRadius(m.kinds[i], m.hps[i]) * s * 1.55;
      const x = this.sx(m.xs[i]);
      const y = this.sy(m.ys[i]);
      ctx.moveTo(x + r, y);
      ctx.arc(x, y, r, 0, Math.PI * 2);
    }
    ctx.fill();
    // Bodies, one path per team and kind.
    for (let team = 0; team < 2; team++) {
      for (const kind of [MobKind.Grunt, MobKind.Runner]) {
        ctx.fillStyle = kind === MobKind.Runner ? TEAM[team].light : TEAM[team].main;
        ctx.beginPath();
        for (let i = 0; i < n; i++) {
          if (m.teams[i] !== team || m.kinds[i] !== kind) continue;
          const r = mobRadius(kind, m.hps[i]) * s;
          const x = this.sx(m.xs[i]);
          const y = this.sy(m.ys[i]);
          ctx.moveTo(x + r, y);
          ctx.arc(x, y, r, 0, Math.PI * 2);
        }
        ctx.fill();
        ctx.strokeStyle = TEAM[team].dark;
        ctx.lineWidth = Math.max(1, s * 0.05);
        ctx.stroke();
      }
    }
    // Armor: a steel ring.
    ctx.strokeStyle = '#e2e8f0';
    ctx.lineWidth = Math.max(1.5, s * 0.1);
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      if (!m.armored?.[i] || m.kinds[i] >= MobKind.Giant) continue;
      const r = mobRadius(m.kinds[i], m.hps[i]) * s + Math.max(1, s * 0.06);
      const x = this.sx(m.xs[i]);
      const y = this.sy(m.ys[i]);
      ctx.moveTo(x + r, y);
      ctx.arc(x, y, r, 0, Math.PI * 2);
    }
    ctx.stroke();
    // Eyes, looking the way each mob marches.
    ctx.fillStyle = '#ffffff';
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      if (m.kinds[i] >= MobKind.Giant) continue;
      const r = mobRadius(m.kinds[i], m.hps[i]) * s;
      if (r < 3) continue;
      const up = this.screenUp(m.teams[i]);
      const x = this.sx(m.xs[i]);
      const y = this.sy(m.ys[i]) - up * r * 0.3;
      const e = Math.max(0.8, r * 0.22);
      ctx.moveTo(x - r * 0.32 + e, y);
      ctx.arc(x - r * 0.32, y, e, 0, Math.PI * 2);
      ctx.moveTo(x + r * 0.32 + e, y);
      ctx.arc(x + r * 0.32, y, e, 0, Math.PI * 2);
    }
    ctx.fill();
    // Squad sizes.
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = `800 ${Math.max(8, s * 0.42)}px system-ui, sans-serif`;
    for (let i = 0; i < n; i++) {
      if (m.kinds[i] >= MobKind.Giant || m.hps[i] < 3) continue;
      const x = this.sx(m.xs[i]);
      const y = this.sy(m.ys[i]) + mobRadius(m.kinds[i], m.hps[i]) * s * 0.3;
      ctx.fillStyle = 'rgba(0,0,0,0.6)';
      ctx.fillText(String(m.hps[i]), x + 0.8, y + 0.8);
      ctx.fillStyle = '#fff';
      ctx.fillText(String(m.hps[i]), x, y);
    }
    // Giants and brutes, one by one.
    for (let i = 0; i < n; i++) {
      if (m.kinds[i] < MobKind.Giant) continue;
      this.drawBig(m.kinds[i], m.teams[i], m.xs[i], m.ys[i], m.hps[i]);
    }
  }

  drawBig(kind, team, wx, wy, hp) {
    const ctx = this.ctx;
    const s = this.scale;
    const r = mobRadius(kind, hp) * s;
    const x = this.sx(wx);
    const y = this.sy(wy);
    const up = this.screenUp(team);
    const colors = TEAM[team];
    ctx.fillStyle = 'rgba(0,0,0,0.25)';
    ctx.beginPath();
    ctx.ellipse(x, y + r * 0.4, r * 1.05, r * 0.6, 0, 0, Math.PI * 2);
    ctx.fill();
    if (kind === MobKind.Brute) {
      ctx.fillStyle = colors.dark;
      ctx.beginPath();
      const spikes = 10;
      for (let t = 0; t <= spikes * 2; t++) {
        const angle = (t / (spikes * 2)) * Math.PI * 2;
        const radius = t % 2 === 0 ? r * 1.15 : r * 0.92;
        ctx.lineTo(x + Math.cos(angle) * radius, y + Math.sin(angle) * radius);
      }
      ctx.fill();
    }
    const body = ctx.createRadialGradient(x - r * 0.3, y - r * 0.3, r * 0.1, x, y, r);
    body.addColorStop(0, colors.light);
    body.addColorStop(1, kind === MobKind.Brute ? colors.dark : colors.main);
    ctx.fillStyle = body;
    ctx.beginPath();
    ctx.arc(x, y, r * (kind === MobKind.Brute ? 0.92 : 1), 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = colors.dark;
    ctx.lineWidth = Math.max(1.5, s * 0.1);
    ctx.stroke();
    // Face.
    const eyeY = y - up * r * 0.25;
    ctx.fillStyle = '#fff';
    for (const side of [-1, 1]) {
      ctx.beginPath();
      ctx.arc(x + side * r * 0.32, eyeY, r * 0.17, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.fillStyle = '#111827';
    for (const side of [-1, 1]) {
      ctx.beginPath();
      ctx.arc(x + side * r * 0.32, eyeY - up * r * 0.05, r * 0.08, 0, Math.PI * 2);
      ctx.fill();
    }
    if (kind === MobKind.Giant) {
      // A little crown.
      const crownY = y - up * r * 0.95;
      ctx.fillStyle = '#facc15';
      ctx.beginPath();
      ctx.moveTo(x - r * 0.45, crownY);
      ctx.lineTo(x - r * 0.45, crownY - up * r * 0.35);
      ctx.lineTo(x - r * 0.2, crownY - up * r * 0.15);
      ctx.lineTo(x, crownY - up * r * 0.42);
      ctx.lineTo(x + r * 0.2, crownY - up * r * 0.15);
      ctx.lineTo(x + r * 0.45, crownY - up * r * 0.35);
      ctx.lineTo(x + r * 0.45, crownY);
      ctx.closePath();
      ctx.fill();
    } else {
      // Horns.
      ctx.fillStyle = '#f5f5f4';
      for (const side of [-1, 1]) {
        ctx.beginPath();
        ctx.moveTo(x + side * r * 0.5, y - up * r * 0.6);
        ctx.lineTo(x + side * r * 0.95, y - up * r * 1.25);
        ctx.lineTo(x + side * r * 0.25, y - up * r * 0.8);
        ctx.fill();
      }
    }
    ctx.font = `900 ${Math.max(10, r * 0.6)}px system-ui, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.lineWidth = 3;
    ctx.strokeStyle = 'rgba(0,0,0,0.6)';
    ctx.strokeText(String(hp), x, y + up * r * 0.35);
    ctx.fillStyle = '#fff';
    ctx.fillText(String(hp), x, y + up * r * 0.35);
  }

  drawCannons(view, frame) {
    const ctx = this.ctx;
    const s = this.scale;
    for (const cannon of view.cannons) {
      const mine = cannon.slot === frame.mySlot;
      const x = this.sx(mine && frame.localX != null ? frame.localX : cannon.x);
      const wy = cannon.team === 0 ? this.cannonOffset : this.H - this.cannonOffset;
      const y = this.sy(wy);
      const up = this.screenUp(cannon.team);
      const color = frame.colorOf(cannon.slot, cannon.team);
      ctx.globalAlpha = cannon.connected ? 1 : 0.45;
      if (cannon.phasing) {
        ctx.fillStyle = `rgba(165,243,252,${0.3 + 0.15 * Math.sin(frame.time * 10)})`;
        ctx.beginPath();
        ctx.arc(x, y, s * 2.1, 0, Math.PI * 2);
        ctx.fill();
      }
      if (mine) {
        ctx.fillStyle = 'rgba(255,255,255,0.18)';
        ctx.beginPath();
        ctx.arc(x, y, s * 1.6, 0, Math.PI * 2);
        ctx.fill();
      }
      // Barrel, with a kick while firing.
      const kick = cannon.firing ? Math.sin(frame.time * 40) * s * 0.08 : 0;
      ctx.fillStyle = '#334155';
      roundRect(ctx, x - s * 0.32, y - up * (s * 1.5 - kick), s * 0.64, up * s * 1.2, s * 0.15);
      ctx.fill();
      ctx.fillStyle = '#1e293b';
      roundRect(ctx, x - s * 0.4, y - up * (s * 1.6 - kick), s * 0.8, up * s * 0.3, s * 0.1);
      ctx.fill();
      // Body.
      ctx.fillStyle = color;
      roundRect(ctx, x - s * 0.95, y - s * 0.55, s * 1.9, s * 1.1, s * 0.35);
      ctx.fill();
      ctx.strokeStyle = 'rgba(0,0,0,0.45)';
      ctx.lineWidth = Math.max(1.5, s * 0.08);
      ctx.stroke();
      // Charge meter on the body.
      ctx.fillStyle = 'rgba(0,0,0,0.35)';
      roundRect(ctx, x - s * 0.7, y + s * 0.12, s * 1.4, s * 0.22, s * 0.1);
      ctx.fill();
      ctx.fillStyle = cannon.giantReady ? '#facc15' : '#fde68a';
      if (cannon.charge > 0) {
        roundRect(ctx, x - s * 0.7, y + s * 0.12, s * 1.4 * cannon.charge, s * 0.22, s * 0.1);
        ctx.fill();
      }
      ctx.globalAlpha = 1;
      if (cannon.magnet) {
        ctx.font = `${Math.max(10, s * 0.7)}px system-ui, sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('🧲', x - s * 1.3, y);
      }
      // Shots per volley, once a power-up raised it.
      if (cannon.volley > 1) {
        const badgeX = x + s * 1.25;
        ctx.fillStyle = '#facc15';
        ctx.beginPath();
        ctx.arc(badgeX, y, s * 0.5, 0, Math.PI * 2);
        ctx.fill();
        ctx.font = `900 ${Math.max(9, s * 0.5)}px system-ui, sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#422006';
        ctx.fillText(`×${cannon.volley}`, badgeX, y + s * 0.02);
      }
      // Name tag.
      const name = frame.nameOf(cannon.slot);
      if (name) {
        ctx.font = `700 ${Math.max(10, s * 0.48)}px system-ui, sans-serif`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        const tagY = y + up * s * 1.05;
        ctx.lineWidth = 3;
        ctx.strokeStyle = 'rgba(0,0,0,0.6)';
        const label = mine ? `${name} (you)` : cannon.connected ? name : `${name} 💤`;
        ctx.strokeText(label, x, tagY);
        ctx.fillStyle = '#fff';
        ctx.fillText(label, x, tagY);
      }
    }
  }

  drawBombs(view, time) {
    const ctx = this.ctx;
    const s = this.scale;
    for (const bomb of view.bombs) {
      const total = 0.8;
      const p = Math.min(1, Math.max(0, 1 - bomb.fuse / total));
      // Target ring.
      ctx.strokeStyle = `rgba(255,255,255,${0.4 + 0.4 * Math.sin(time * 20)})`;
      ctx.lineWidth = 2;
      ctx.setLineDash([6, 5]);
      ctx.beginPath();
      ctx.arc(this.sx(bomb.x), this.sy(bomb.y), bomb.radius * s, 0, Math.PI * 2);
      ctx.stroke();
      ctx.setLineDash([]);
      // The bomb in flight, on an arc.
      const x = this.sx(bomb.fromX + (bomb.x - bomb.fromX) * p);
      const y = this.sy(bomb.fromY + (bomb.y - bomb.fromY) * p) - Math.sin(p * Math.PI) * s * 4;
      ctx.fillStyle = '#111827';
      ctx.beginPath();
      ctx.arc(x, y, s * 0.45, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#f97316';
      ctx.beginPath();
      ctx.arc(x + s * 0.25, y - s * 0.35, s * 0.15 + Math.random() * s * 0.08, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  // The bomb aimer at a world point, with any vim count being typed.
  drawCrosshair(aim, time, count) {
    const ctx = this.ctx;
    const s = this.scale;
    const r = 2.6 * s;
    const x = this.sx(aim.x);
    const y = this.sy(aim.y);
    ctx.strokeStyle = `rgba(251,146,60,${0.6 + 0.3 * Math.sin(time * 8)})`;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.moveTo(x - r * 1.2, y);
    ctx.lineTo(x + r * 1.2, y);
    ctx.moveTo(x, y - r * 1.2);
    ctx.lineTo(x, y + r * 1.2);
    ctx.stroke();
    if (count) {
      ctx.font = `800 ${Math.max(12, s * 0.8)}px system-ui, sans-serif`;
      ctx.textAlign = 'left';
      ctx.textBaseline = 'bottom';
      ctx.lineWidth = 3;
      ctx.strokeStyle = 'rgba(0,0,0,0.7)';
      ctx.strokeText(count, x + r * 0.75, y - r * 0.75);
      ctx.fillStyle = '#fed7aa';
      ctx.fillText(count, x + r * 0.75, y - r * 0.75);
    }
  }

  // Power-up orbs, coloured by kind, with a ring that empties as they are hit.
  drawPowerUps(view, time, level) {
    const ctx = this.ctx;
    const s = this.scale;
    const radius = (level.field.powerUpRadius ?? 0.9) * s;
    for (const powerup of view.powerups) {
      const x = this.sx(powerup.x);
      const y = this.sy(powerup.y);
      const pulse = 1 + 0.06 * Math.sin(time * 6 + powerup.id);
      const style = ORB_STYLE[powerup.kind] ?? ORB_STYLE[0];
      ctx.fillStyle = style.glow;
      ctx.beginPath();
      ctx.arc(x, y, radius * 1.5 * pulse, 0, Math.PI * 2);
      ctx.fill();
      const orb = ctx.createRadialGradient(x - radius * 0.3, y - radius * 0.3, radius * 0.1, x, y, radius);
      orb.addColorStop(0, style.light);
      orb.addColorStop(1, style.dark);
      ctx.fillStyle = orb;
      ctx.beginPath();
      ctx.arc(x, y, radius * pulse, 0, Math.PI * 2);
      ctx.fill();
      // What is left to break, as an arc around the orb.
      const left = powerup.max > 0 ? powerup.hp / powerup.max : 1;
      ctx.strokeStyle = style.ring;
      ctx.lineWidth = Math.max(2, s * 0.14);
      ctx.beginPath();
      ctx.arc(x, y, radius * 1.3, -Math.PI / 2, -Math.PI / 2 + left * Math.PI * 2);
      ctx.stroke();
      ctx.font = `900 ${Math.max(10, s * 0.75)}px system-ui, sans-serif`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillStyle = style.ink;
      ctx.fillText(style.glyph, x, y + s * 0.03);
    }
  }

  drawFrenzy(time) {
    const ctx = this.ctx;
    const alpha = 0.12 + 0.08 * Math.sin(time * 6);
    const gradient = ctx.createRadialGradient(
      this.width / 2,
      this.height / 2,
      Math.min(this.width, this.height) * 0.3,
      this.width / 2,
      this.height / 2,
      Math.max(this.width, this.height) * 0.75,
    );
    gradient.addColorStop(0, 'rgba(192,132,252,0)');
    gradient.addColorStop(1, `rgba(192,132,252,${alpha})`);
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, this.width, this.height);
  }
}

export function roundRect(ctx, x, y, w, h, r) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  r = Math.max(0, Math.min(r, w / 2, h / 2));
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

// Short-lived particles, rings and floating numbers, in world coordinates.
export class Effects {
  constructor() {
    this.particles = [];
    this.rings = [];
    this.texts = [];
  }

  poof(x, y, team, count = 4) {
    if (this.particles.length > 700) return;
    for (let i = 0; i < count; i++) {
      const angle = Math.random() * Math.PI * 2;
      const speed = 1.5 + Math.random() * 3;
      this.particles.push({
        x,
        y,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        life: 0.35 + Math.random() * 0.25,
        age: 0,
        size: 0.12 + Math.random() * 0.12,
        color: TEAM[team].main,
      });
    }
  }

  sparks(x, y, color = '#fde68a', count = 6) {
    if (this.particles.length > 700) return;
    for (let i = 0; i < count; i++) {
      const angle = Math.random() * Math.PI * 2;
      const speed = 4 + Math.random() * 6;
      this.particles.push({
        x,
        y,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed,
        life: 0.25 + Math.random() * 0.2,
        age: 0,
        size: 0.08,
        color,
      });
    }
  }

  ring(x, y, radius, color, life = 0.5) {
    this.rings.push({ x, y, radius, color, life, age: 0 });
  }

  text(x, y, text, color, size = 1, life = 0.9) {
    if (this.texts.length > 60) this.texts.shift();
    this.texts.push({ x, y, text, color, size, life, age: 0, rise: 0 });
  }

  update(dt) {
    for (const p of this.particles) {
      p.age += dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.vx *= 0.9;
      p.vy *= 0.9;
    }
    this.particles = this.particles.filter((p) => p.age < p.life);
    for (const r of this.rings) r.age += dt;
    this.rings = this.rings.filter((r) => r.age < r.life);
    for (const t of this.texts) {
      t.age += dt;
      t.rise += dt * 1.2;
    }
    this.texts = this.texts.filter((t) => t.age < t.life);
  }

  draw(ctx, view) {
    const s = view.scale;
    for (const p of this.particles) {
      ctx.globalAlpha = 1 - p.age / p.life;
      ctx.fillStyle = p.color;
      ctx.beginPath();
      ctx.arc(view.sx(p.x), view.sy(p.y), Math.max(1, p.size * s), 0, Math.PI * 2);
      ctx.fill();
    }
    for (const r of this.rings) {
      const k = r.age / r.life;
      ctx.globalAlpha = 1 - k;
      ctx.strokeStyle = r.color;
      ctx.lineWidth = Math.max(2, s * 0.25 * (1 - k));
      ctx.beginPath();
      ctx.arc(view.sx(r.x), view.sy(r.y), r.radius * s * (0.3 + 0.7 * k), 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const t of this.texts) {
      const k = t.age / t.life;
      ctx.globalAlpha = k < 0.7 ? 1 : 1 - (k - 0.7) / 0.3;
      const size = Math.max(11, s * 0.9 * t.size * (k < 0.15 ? 0.6 + k * 2.7 : 1));
      ctx.font = `900 ${size}px system-ui, sans-serif`;
      ctx.lineWidth = Math.max(2, size * 0.2);
      ctx.strokeStyle = 'rgba(0,0,0,0.6)';
      const ty = view.sy(t.y) - t.rise * s;
      ctx.strokeText(t.text, view.sx(t.x), ty);
      ctx.fillStyle = t.color;
      ctx.fillText(t.text, view.sx(t.x), ty);
    }
    ctx.globalAlpha = 1;
  }
}
