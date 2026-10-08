// The app: session, screens (home, room lobby, game, cards, results) and the
// messages that move between them.

import { Link } from './net.js';
import { GameView } from './game.js';
import { PLAYER_COLORS } from './render.js';
import { isMuted, setMuted, sfx, unlockAudio } from './audio.js';

const PROTOCOL = 2;
const DEFAULT_NAME = /^Player \d+$/;
const EMOTES = ['👍', '😂', '😱', '🔥', '😡', '🎉'];
const CARD_GLYPHS = {
  rapid: '⚡',
  lucky: '🍀',
  charge: '🔋',
  heavy: '🏋️',
  demo: '💥',
  swift: '👟',
  fortify: '🏰',
};

const $ = (id) => document.getElementById(id);
const el = (tag, attrs = {}, ...children) => {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (key === 'class') node.className = value;
    else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
    else if (value === true) node.setAttribute(key, '');
    else if (value !== false && value != null) node.setAttribute(key, value);
  }
  for (const child of children.flat()) {
    if (child == null || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
};

const storage = {
  get(area, key) {
    try {
      return window[area].getItem(key);
    } catch {
      return null;
    }
  },
  set(area, key, value) {
    try {
      window[area].setItem(key, value);
    } catch {
      /* private mode */
    }
  },
};

const state = {
  id: 0,
  name: storage.get('localStorage', 'mob-survivor-name') || '',
  token: storage.get('sessionStorage', 'mob-survivor-token') || '',
  room: null,
  cards: null,
  picked: [],
  over: null,
  pendingJoin: /^#[A-Za-z]{4}$/.test(location.hash) ? location.hash.slice(1).toUpperCase() : null,
  screen: 'home',
};

const link = new Link({
  onOpen: () => {
    state.awaitingSession = true;
    $('reconnecting').hidden = true;
    $('conn-state').textContent = 'Connected';
    link.send({ t: 'hello', name: state.name, token: state.token });
  },
  onClose: (wasOpen) => {
    $('conn-state').textContent = 'Offline: reconnecting…';
    if (wasOpen && state.room) $('reconnecting').hidden = false;
  },
  onMessage: (message) => handle(message),
  onSnapshot: (buffer) => game.onSnapshot(buffer),
});

const game = new GameView({ canvas: $('arena'), send: (message) => link.send(message) });

// ---------------------------------------------------------------- routing

function show(screen) {
  state.screen = screen;
  $('home').hidden = screen !== 'home';
  $('lobby').hidden = screen !== 'lobby';
  $('hud').hidden = screen !== 'game';
  game.setActive(screen === 'game');
  if (screen !== 'game') {
    $('upgrade').hidden = true;
    $('over').hidden = true;
  }
}

function route() {
  const room = state.room;
  if (!room) {
    show('home');
    if (location.hash) history.replaceState(null, '', location.pathname);
    return;
  }
  if (location.hash !== `#${room.code}`) history.replaceState(null, '', `#${room.code}`);
  if (room.phase === 'lobby') {
    show('lobby');
    renderLobby();
  } else {
    show('game');
    $('upgrade').hidden = room.phase !== 'upgrade' || !state.cards;
    $('over').hidden = room.phase !== 'over' || !state.over;
  }
}

function errorToast(text) {
  const toast = $('error-toast');
  toast.textContent = text;
  toast.hidden = false;
  clearTimeout(errorToast.timer);
  errorToast.timer = setTimeout(() => (toast.hidden = true), 3200);
}

// ---------------------------------------------------------------- messages

function handle(message) {
  switch (message.t) {
    case 'welcome':
      state.id = message.id;
      if (message.v !== PROTOCOL) errorToast('The game was updated: reload the page.');
      if (!state.token) state.token = message.token;
      // A Tailscale name is a good default; "Player 12" is not worth showing.
      if (!state.name && message.name && !DEFAULT_NAME.test(message.name)) setName(message.name, false);
      if (!$('name-input').value) $('name-input').value = state.name;
      break;
    case 'session':
      state.token = message.token;
      storage.set('sessionStorage', 'mob-survivor-token', message.token);
      state.name = message.name;
      if (document.activeElement !== $('name-input') && !DEFAULT_NAME.test(message.name)) {
        $('name-input').value = message.name;
      }
      if (state.awaitingSession && !message.rejoined) {
        // A fresh connection that did not get its seat back: whatever room
        // we were in is gone (or we are new).
        if (state.room) {
          state.room = null;
          route();
        }
        if (state.pendingJoin) {
          link.send({ t: 'join', code: state.pendingJoin });
          state.pendingJoin = null;
        }
      }
      state.awaitingSession = false;
      break;
    case 'rooms':
      renderRooms(message.rooms);
      break;
    case 'board':
      renderBoard(message.entries);
      break;
    case 'room': {
      const previous = state.room;
      state.room = message;
      game.setRoom(message);
      if (message.phase === 'lobby') {
        state.cards = null;
        state.over = null;
      }
      if (previous && previous.phase === 'over' && message.phase !== 'over') state.over = null;
      if (message.phase !== 'upgrade') state.cards = null;
      route();
      if (message.phase === 'over') renderOver();
      break;
    }
    case 'level':
      game.setLevel(message);
      break;
    case 'cards':
      state.cards = message;
      state.picked = [];
      renderCards();
      route();
      break;
    case 'picks':
      state.picked = message.picked;
      renderWaiting();
      break;
    case 'over':
      state.over = message;
      renderOver();
      route();
      break;
    case 'emote':
      if (state.screen === 'game') game.showEmote(message, EMOTES[message.e] ?? '❓');
      break;
    case 'pong':
      $('hud-ping').textContent = `${Math.round(performance.now() - message.n)} ms`;
      break;
    case 'error':
      errorToast(message.message);
      break;
    default:
      break;
  }
}

function setName(name, announce = true) {
  state.name = name.trim().slice(0, 16);
  storage.set('localStorage', 'mob-survivor-name', state.name);
  if (announce) link.send({ t: 'hello', name: state.name });
}

// ---------------------------------------------------------------- home

function renderRooms(rooms) {
  const list = $('room-list');
  list.replaceChildren();
  if (!rooms.length) {
    list.append(el('li', { class: 'muted' }, 'No open rooms yet. Make one!'));
    return;
  }
  for (const room of rooms) {
    const inGame = room.phase !== 'lobby';
    list.append(
      el(
        'li',
        {},
        el('span', { class: 'code-chip' }, room.code),
        el('span', { class: 'tag' }, room.mode === 'versus' ? '⚔ versus' : '🤝 co-op'),
        el('span', { class: 'muted' }, inGame ? `playing${room.level ? ` · L${room.level}` : ''}` : `${room.seated}/${room.maxSeats}`),
        el('span', { class: 'muted' }, room.host || ''),
        el('button', { class: 'btn small', onclick: () => joinRoom(room.code) }, inGame ? 'Watch' : 'Join'),
      ),
    );
  }
}

function renderBoard(entries) {
  const list = $('board-list');
  list.replaceChildren();
  if (!entries.length) {
    list.append(el('li', { class: 'muted' }, 'No runs yet. Be the first.'));
    return;
  }
  for (const entry of entries) {
    const levels = entry.levels === 1 ? '1 level' : `${entry.levels} levels`;
    list.append(
      el('li', {}, el('span', { class: 'names' }, entry.names), el('span', { class: 'score' }, levels), el('span', { class: 'muted' }, `${entry.kills} ☠`)),
    );
  }
}

function joinRoom(code) {
  unlockAudio();
  sfx.click();
  link.send({ t: 'join', code });
}

function create(options) {
  unlockAudio();
  sfx.click();
  const name = $('name-input').value.trim();
  if (name && name !== state.name) setName(name);
  link.send({ t: 'create', ...options });
}

$('btn-solo').addEventListener('click', () => create({ mode: 'campaign', public: false, start: true }));
$('btn-coop').addEventListener('click', () => create({ mode: 'campaign', public: true }));
$('btn-versus').addEventListener('click', () => create({ mode: 'versus', public: true }));
$('btn-duel').addEventListener('click', () => create({ mode: 'versus', public: false, bots: true, start: true }));
$('join-form').addEventListener('submit', (event) => {
  event.preventDefault();
  const code = $('join-code').value.trim().toUpperCase();
  if (code.length === 4) joinRoom(code);
  else errorToast('Room codes have four letters.');
});
$('name-input').addEventListener('change', (event) => setName(event.target.value));

function syncMuteButtons() {
  const muted = isMuted();
  $('btn-mute').textContent = muted ? '🔇' : '🔊';
  $('btn-mute-home').textContent = muted ? '🔇 Sound off' : '🔊 Sound on';
}
for (const id of ['btn-mute', 'btn-mute-home']) {
  $(id).addEventListener('click', () => {
    unlockAudio();
    setMuted(!isMuted());
    syncMuteButtons();
  });
}
syncMuteButtons();

setInterval(() => {
  if (state.screen === 'home') link.send({ t: 'rooms' });
}, 4000);
setInterval(() => {
  if (state.screen === 'game') link.send({ t: 'ping', n: Math.round(performance.now()) });
}, 2000);

// ---------------------------------------------------------------- lobby

function me() {
  return state.room?.members.find((m) => m.id === state.room.you);
}

function memberRow(member, color) {
  const room = state.room;
  const isHost = room.host === room.you;
  const badges = [];
  if (member.host) badges.push(el('span', { class: 'badge' }, 'host'));
  if (member.id === room.you && !member.bot) badges.push(el('span', { class: 'badge' }, 'you'));
  if (member.bot) badges.push(el('span', { class: 'badge' }, 'bot'));
  if (!member.connected) badges.push(el('span', { class: 'badge' }, 'away'));
  return el(
    'div',
    { class: 'member' },
    el('span', { class: 'swatch', style: `background:${color}` }),
    el('span', { class: 'who' }, member.name),
    badges,
    member.bot && isHost ? el('button', { class: 'btn small ghost', onclick: () => link.send({ t: 'unbot' }) }, '✕') : null,
  );
}

function renderLobby() {
  const room = state.room;
  const mine = me();
  const isHost = room.host === room.you;
  const versus = room.mode === 'versus';
  $('lobby-mode').textContent = versus ? 'Versus room' : 'Co-op campaign room';
  $('lobby-code').textContent = room.code;
  const teams = $('lobby-teams');
  teams.replaceChildren();
  const seatsPerTeam = versus ? 2 : room.maxSeats;
  const teamList = versus ? [0, 1] : [0];
  for (const team of teamList) {
    const seated = room.members.filter((m) => m.seated && (!versus || m.team === team));
    const box = el('div', { class: `team ${team === 0 ? 'blue' : 'red'}` });
    const title = versus ? (team === 0 ? '🔵 Blue team' : '🔴 Red team') : '🤝 Your crew';
    box.append(el('h3', {}, title, el('small', { class: 'muted' }, `${seated.length}/${seatsPerTeam}`)));
    seated.forEach((member, i) => box.append(memberRow(member, PLAYER_COLORS[versus ? team : 0][i % 4])));
    for (let i = seated.length; i < seatsPerTeam; i++) box.append(el('div', { class: 'member empty' }, 'Open seat'));
    const buttons = el('div', { class: 'lobby-actions' });
    const seatsLeft = room.members.filter((m) => m.seated).length < room.maxSeats;
    if (seated.length < seatsPerTeam && seatsLeft) {
      if (mine && (!mine.seated || (versus && mine.team !== team))) {
        buttons.append(el('button', { class: `btn small ${team === 0 ? 'blue' : 'red'}`, onclick: () => link.send({ t: 'team', team }) }, versus ? 'Join this team' : 'Take a seat'));
      }
      if (isHost) buttons.append(el('button', { class: 'btn small ghost', onclick: () => link.send({ t: 'bot', team }) }, '+ Add bot'));
    }
    if (buttons.children.length) box.append(buttons);
    teams.append(box);
  }
  const watchers = room.members.filter((m) => !m.seated);
  if (watchers.length) {
    const box = el('div', { class: 'team' }, el('h3', {}, '👀 Watching'));
    watchers.forEach((member) => box.append(memberRow(member, '#64748b')));
    teams.append(box);
  }

  const actions = $('lobby-actions');
  actions.replaceChildren();
  const hint = $('lobby-hint');
  if (isHost) {
    const canStart = room.members.some((m) => m.seated);
    actions.append(el('button', { class: 'btn primary', disabled: !canStart, onclick: () => (sfx.click(), link.send({ t: 'start' })) }, '▶ Start match'));
    hint.textContent = versus
      ? 'Share the code. Empty teams get a bot when you start.'
      : 'Share the code: friends join with it, or with the invite link.';
  } else {
    hint.textContent = 'Waiting for the host to start…';
  }
  if (mine?.seated && versus) {
    actions.append(el('button', { class: 'btn ghost', onclick: () => link.send({ t: 'spectate' }) }, 'Watch instead'));
  }
}

$('btn-leave-lobby').addEventListener('click', () => leave());
$('btn-copy-link').addEventListener('click', async () => {
  const url = `${location.origin}${location.pathname}#${state.room?.code ?? ''}`;
  try {
    await navigator.clipboard.writeText(url);
    errorToast('Invite link copied!');
  } catch {
    errorToast(url);
  }
});

function leave() {
  link.send({ t: 'leave' });
  state.room = null;
  state.cards = null;
  state.over = null;
  game.reset();
  route();
}

// ---------------------------------------------------------------- game HUD

let quitArmed = 0;
$('btn-quit').addEventListener('click', () => {
  if (performance.now() - quitArmed < 2500) {
    leave();
    return;
  }
  quitArmed = performance.now();
  errorToast('Tap ✕ again to leave the match.');
});

$('btn-emote').addEventListener('click', () => {
  const menu = $('emote-menu');
  menu.hidden = !menu.hidden;
});
$('emote-menu').append(
  ...EMOTES.map((glyph, e) =>
    el('button', {
      onclick: () => {
        link.send({ t: 'emote', e });
        $('emote-menu').hidden = true;
      },
    }, glyph),
  ),
);

// ---------------------------------------------------------------- cards

function renderCards() {
  const offer = state.cards;
  if (!offer) return;
  $('upgrade-title').textContent = `Level ${offer.cleared} cleared!`;
  $('upgrade-sub').textContent = offer.picked ? 'Upgrade taken.' : 'Pick one upgrade for the next level.';
  const cards = $('cards');
  cards.replaceChildren();
  offer.cards.forEach((card, i) => {
    cards.append(
      el(
        'button',
        {
          class: `card${offer.chosen === i ? ' chosen' : ''}`,
          disabled: offer.picked,
          onclick: () => {
            if (state.cards.picked) return;
            state.cards.picked = true;
            state.cards.chosen = i;
            sfx.pick();
            link.send({ t: 'pick', i });
            renderCards();
          },
        },
        el('span', { class: 'glyph' }, CARD_GLYPHS[card.key] ?? '⭐'),
        el('b', {}, card.title),
        el('small', {}, card.text),
        card.taken ? el('span', { class: 'stack' }, `owned ×${card.taken}`) : null,
      ),
    );
  });
  renderWaiting();
}

function renderWaiting() {
  const room = state.room;
  if (!room) return;
  const waiting = room.members.filter((m) => m.seated && m.slot >= 0 && !m.bot && m.connected && !state.picked.includes(m.slot));
  $('upgrade-wait').textContent = waiting.length ? `Waiting for ${waiting.map((m) => m.name).join(', ')}…` : 'Everyone is ready!';
}

// ---------------------------------------------------------------- results

// [heading, hidden on narrow screens]
const COLUMNS = [
  ['Player', false],
  ['Shots', true],
  ['Gate mobs', false],
  ['Kills', false],
  ['Base dmg', false],
  ['Giants', true],
  ['Bombs', true],
];

function renderOver() {
  const over = state.over;
  const room = state.room;
  if (!over || !room) return;
  const mine = me();
  const myTeam = mine?.seated ? mine.team : null;
  let title;
  let sub;
  if (over.mode === 'campaign') {
    title = 'Game over';
    sub = `Your crew cleared ${over.levels} level${over.levels === 1 ? '' : 's'}.`;
    if (over.rank === 0) sub += ' A new record on the Hall of Fame! 🏆';
    else if (over.rank > 0) sub += ` #${over.rank + 1} on the Hall of Fame!`;
  } else if (over.outcome === 'draw') {
    title = 'Draw!';
    sub = 'Both bases held to the end.';
  } else {
    const winner = over.outcome === 'blue' ? 0 : 1;
    if (myTeam === null) title = `${winner === 0 ? 'Blue' : 'Red'} wins!`;
    else title = winner === myTeam ? 'Victory!' : 'Defeat';
    sub = `${winner === 0 ? 'Blue' : 'Red'} team destroyed the enemy base.`;
  }
  $('over-title').textContent = title;
  $('over-sub').textContent = sub;
  const table = $('over-table');
  table.replaceChildren(
    el(
      'thead',
      {},
      el(
        'tr',
        {},
        COLUMNS.map(([h, optional]) => el('th', { class: optional ? 'opt' : null }, h)),
      ),
    ),
    el(
      'tbody',
      {},
      over.players.map((p) =>
        el(
          'tr',
          {},
          el('td', {}, `${p.team === 0 ? '🔵' : '🔴'} ${p.name}${p.bot ? ' 🤖' : ''}`),
          [p.shots, p.gateMobs, p.kills, p.baseDamage, p.giants, p.bombs].map((v, i) =>
            el('td', { class: COLUMNS[i + 1][1] ? 'opt' : null }, v),
          ),
        ),
      ),
    ),
  );
  const isHost = room.host === room.you;
  $('btn-again').hidden = !isHost;
  $('over-wait').textContent = isHost ? '' : 'Waiting for the host to play again…';
}

$('btn-again').addEventListener('click', () => {
  sfx.click();
  link.send({ t: 'again' });
});
$('btn-over-leave').addEventListener('click', () => leave());

// ---------------------------------------------------------------- start

document.addEventListener('pointerdown', () => unlockAudio(), { once: true });
if (state.name) $('name-input').value = state.name;
show('home');
link.connect();
