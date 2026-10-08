// Little synthesized sound effects: no audio files, just WebAudio.

let context = null;
let master = null;
let muted = false;
const lastPlayed = new Map();

try {
  muted = localStorage.getItem('mob-survivor-muted') === '1';
} catch {
  muted = false;
}

// Browsers only allow audio after a user gesture; call this from one.
export function unlockAudio() {
  if (context) {
    if (context.state === 'suspended') context.resume();
    return;
  }
  const Context = window.AudioContext || window.webkitAudioContext;
  if (!Context) return;
  context = new Context();
  master = context.createGain();
  master.gain.value = muted ? 0 : 0.5;
  master.connect(context.destination);
}

export function isMuted() {
  return muted;
}

export function setMuted(value) {
  muted = value;
  try {
    localStorage.setItem('mob-survivor-muted', value ? '1' : '0');
  } catch {
    /* storage may be unavailable */
  }
  if (master) master.gain.value = muted ? 0 : 0.5;
}

// At most one of `name` every `gap` seconds.
function throttle(name, gap) {
  if (!context || muted) return false;
  const now = context.currentTime;
  if (now - (lastPlayed.get(name) ?? -1) < gap) return false;
  lastPlayed.set(name, now);
  return true;
}

function tone({ type = 'sine', from, to = from, duration, volume = 0.3, delay = 0 }) {
  const start = context.currentTime + delay;
  const osc = context.createOscillator();
  const gain = context.createGain();
  osc.type = type;
  osc.frequency.setValueAtTime(from, start);
  osc.frequency.exponentialRampToValueAtTime(Math.max(to, 1), start + duration);
  gain.gain.setValueAtTime(0.0001, start);
  gain.gain.exponentialRampToValueAtTime(volume, start + 0.01);
  gain.gain.exponentialRampToValueAtTime(0.0001, start + duration);
  osc.connect(gain).connect(master);
  osc.start(start);
  osc.stop(start + duration + 0.05);
}

let noiseBuffer = null;
function noise({ duration, volume = 0.3, from = 2000, to = 200, delay = 0 }) {
  if (!noiseBuffer) {
    noiseBuffer = context.createBuffer(1, context.sampleRate, context.sampleRate);
    const data = noiseBuffer.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
  }
  const start = context.currentTime + delay;
  const source = context.createBufferSource();
  source.buffer = noiseBuffer;
  const filter = context.createBiquadFilter();
  filter.type = 'lowpass';
  filter.frequency.setValueAtTime(from, start);
  filter.frequency.exponentialRampToValueAtTime(to, start + duration);
  const gain = context.createGain();
  gain.gain.setValueAtTime(volume, start);
  gain.gain.exponentialRampToValueAtTime(0.0001, start + duration);
  source.connect(filter).connect(gain).connect(master);
  source.start(start);
  source.stop(start + duration + 0.05);
}

export const sfx = {
  gate(gain) {
    if (!throttle('gate', 0.09)) return;
    const pitch = 520 + Math.min(gain, 40) * 18;
    tone({ type: 'triangle', from: pitch, to: pitch * 1.5, duration: 0.12, volume: 0.12 });
  },
  halve() {
    if (!throttle('halve', 0.15)) return;
    tone({ type: 'sawtooth', from: 300, to: 120, duration: 0.18, volume: 0.08 });
  },
  pop() {
    if (!throttle('pop', 0.05)) return;
    noise({ duration: 0.05, volume: 0.05, from: 3000, to: 800 });
  },
  hitEnemyBase() {
    if (!throttle('hitEnemy', 0.08)) return;
    tone({ type: 'square', from: 180, to: 90, duration: 0.08, volume: 0.07 });
  },
  hitOwnBase() {
    if (!throttle('hitOwn', 0.12)) return;
    tone({ type: 'sine', from: 110, to: 50, duration: 0.25, volume: 0.35 });
    noise({ duration: 0.15, volume: 0.1, from: 600, to: 100 });
  },
  bomb() {
    if (!context || muted) return;
    noise({ duration: 0.7, volume: 0.5, from: 1800, to: 60 });
    tone({ type: 'sine', from: 90, to: 30, duration: 0.6, volume: 0.4 });
  },
  throwBomb() {
    if (!throttle('throw', 0.2)) return;
    tone({ type: 'sine', from: 300, to: 900, duration: 0.25, volume: 0.08 });
  },
  giant() {
    if (!throttle('giant', 0.3)) return;
    tone({ type: 'sawtooth', from: 70, to: 140, duration: 0.5, volume: 0.18 });
    tone({ type: 'square', from: 140, to: 280, duration: 0.4, volume: 0.06, delay: 0.05 });
  },
  powerUp() {
    if (!throttle('powerUp', 0.3)) return;
    tone({ type: 'triangle', from: 660, to: 660, duration: 0.1, volume: 0.12 });
    tone({ type: 'triangle', from: 880, to: 880, duration: 0.1, volume: 0.12, delay: 0.09 });
    tone({ type: 'triangle', from: 1320, to: 1760, duration: 0.2, volume: 0.12, delay: 0.18 });
  },
  saw() {
    if (!throttle('saw', 0.1)) return;
    noise({ duration: 0.06, volume: 0.06, from: 6000, to: 3000 });
  },
  beep(high = false) {
    if (!context || muted) return;
    tone({ type: 'square', from: high ? 880 : 440, duration: high ? 0.35 : 0.15, volume: 0.1 });
  },
  boss() {
    if (!context || muted) return;
    tone({ type: 'sawtooth', from: 120, to: 40, duration: 1.2, volume: 0.25 });
    noise({ duration: 1.0, volume: 0.15, from: 500, to: 80 });
  },
  frenzy() {
    if (!context || muted) return;
    for (let i = 0; i < 3; i++) tone({ type: 'square', from: 600, to: 1200, duration: 0.18, volume: 0.07, delay: i * 0.2 });
  },
  win() {
    if (!context || muted) return;
    [523, 659, 784, 1046].forEach((f, i) => tone({ type: 'triangle', from: f, duration: 0.3, volume: 0.15, delay: i * 0.12 }));
  },
  lose() {
    if (!context || muted) return;
    [392, 370, 349, 262].forEach((f, i) =>
      tone({ type: 'sawtooth', from: f, to: f * 0.97, duration: 0.4, volume: 0.08, delay: i * 0.3 }),
    );
  },
  pick() {
    if (!context || muted) return;
    tone({ type: 'triangle', from: 660, to: 990, duration: 0.15, volume: 0.12 });
    tone({ type: 'triangle', from: 990, to: 1320, duration: 0.2, volume: 0.1, delay: 0.1 });
  },
  click() {
    if (!throttle('click', 0.03)) return;
    tone({ type: 'sine', from: 900, to: 700, duration: 0.05, volume: 0.06 });
  },
  emote() {
    if (!throttle('emote', 0.2)) return;
    tone({ type: 'sine', from: 700, to: 1100, duration: 0.12, volume: 0.08 });
  },
};
