/* Keystone phone controls.
   Talks to the Keystone app through window.KeystoneNative (the Android shell finds Keystone
   on the hotspot and carries the JSON lines). Every Tone page is built from the parameter
   list Keystone sends, so new controls appear here without a phone update. */
'use strict';

var KS = (function () {
  // what Keystone runs on, from its health report: the Pi says device pi (0.5.6)
  function hostName(cap) { var n = S.health && S.health.device === 'pi' ? 'Pi' : 'laptop'; return cap ? n.charAt(0).toUpperCase() + n.slice(1) : n; }
  var S = {
    link: 'searching', linkDetail: '',
    schema: null, params: {}, modules: [], values: {},
    songs: [], parts: [], current: 0, edited: false, maxSongs: 15, maxEntries: 40, bank: 0, rig: 0, footMode: 0,
    tuner: false, captures: [], capture: '', captureFile: '', namAvailable: true, spaces: [], space: '', spaceFile: '',
    footswitch: '', audio: '', keeperLive: false,
    meter: {}, screen: 'play', module: null, prevScreen: 'play', tunerAuto: false,
    editVoice: null, sel: null, openRow: -1, renameRow: -1, confirmDel: -1,
    noteFor: 0, noteUntil: 0, pedalDrag: null,
    health: null, rtt: null, healthAt: 0,
    takes: null, takesFree: 0, roomTake: '', roomFailed: '',
    take: '', takeInfo: null, pickFrom: null, pack: null, making: false,
    upd: { latest: '', assets: [], checkedAt: 0, checking: false, busy: null, err: null, msg: '' },
    // Kaleidoscope: one plugin pedal at a time
    kal: { on: false, loaded: '', pick: 0, installed: [], gigLock: false }, kalParams: [], kalId: '', pedalTable: [], kalSel: null, kalOpen: {}
  };

  // ---------------------------------------------------------------- transport
  var NAV_ONLY = { hello: 1, ping: 1, tuner: 1 };
  function connected() { return S.link === 'connected'; }
  function send(o) {
    if (!connected() && !NAV_ONLY[o.t]) { toast('NOT CONNECTED'); return false; }
    var line = JSON.stringify(o);
    if (S.demo && window.KSDemo) { KSDemo.send(line); return true; }   // the pretend Keystone in the phone
    if (window.KeystoneNative) window.KeystoneNative.send(line);
    else if (window.KSMock) window.KSMock.send(line);
    return true;
  }
  var pendingSet = {}, setTimer = null;
  function setParam(id, v) {          // throttled while dragging
    if (!connected()) { toast('NOT CONNECTED'); return; }
    S.values[id] = v;
    pendingSet[id] = v;
    if (!setTimer) setTimer = setTimeout(function () {
      setTimer = null;
      for (var k in pendingSet) send({ t: 'set', id: k, v: pendingSet[k] });
      pendingSet = {};
    }, 25);
  }

  function onMessage(line) {
    var m;
    try { m = typeof line === 'string' ? JSON.parse(line) : line; } catch (e) { return; }
    switch (m.t) {
      case 'schema':
        useSchema(m);
        try { localStorage.setItem('ks.schema', JSON.stringify(m)); } catch (e) {}
        queueRender(); break;
      case 'state':
        S.values = m.values; S.songs = m.songs; S.parts = m.parts || []; S.current = m.current; S.edited = m.edited;
        S.bank = m.bank || 0; S.maxEntries = m.maxEntries || 40; S.rig = m.rig || 0; S.footMode = m.footMode || 0;
        S.maxSongs = m.maxSongs; S.captures = m.captures || []; S.capture = m.capture; S.captureFile = m.captureFile; S.spaces = m.spaces || []; S.space = m.space || ''; S.spaceFile = m.spaceFile || '';
        S.namAvailable = m.namAvailable; S.footswitch = m.footswitch; S.audio = m.audio; S.keeperLive = m.keeperLive; S.wifi = m.wifi || null;
        S.kal = m.kal || { on: false, loaded: '', pick: 0, installed: [], gigLock: false };
        if (!S.kal.loaded) { S.kalParams = []; S.kalId = ''; }
        else if (S.kal.loaded !== S.kalId) send({ t: 'kalParams' });
        tunerState(m.tuner);
        try { localStorage.setItem('ks.state', JSON.stringify(m)); } catch (e) {}
        queueRender(); break;
      case 'val':
        if (pendingSet[m.id] === undefined && S.values[m.id] === m.v && S.edited === m.edited) break;   // nothing new
        if (pendingSet[m.id] === undefined) S.values[m.id] = m.v;
        S.edited = m.edited;
        queueRender(true); break;
      case 'health':
        S.health = m; S.healthAt = Date.now();
        if (S.screen === 'health') queueRender(true); else strip();
        break;
      case 'pong':
        if (m.ts) S.rtt = Date.now() - m.ts;
        break;
      case 'meter':
        S.meter = m;
        if (m.bank != null && m.bank !== S.bank) { S.bank = m.bank; queueRender(true); }
        if (m.rig != null && m.rig !== S.rig) { S.rig = m.rig; queueRender(); }
        if (m.tuner) tunerState(true); else if (S.tuner) tunerState(false);
        roomSync(m.rec);
        if (S.screen === 'health') { var bfk = JSON.stringify(m.bf || null) + '|' + m.buf; if (bfk !== S.bfKey) { S.bfKey = bfk; queueRender(true); } }
        if (S.screen === 'recorder' || (S.screen === 'take' && (m.listen || S.making))) queueRender(true);
        else if (S.screen === 'listen') { /* the player updates its own row */ }
        else { var rp = document.getElementById('recPill'), want = recPill(); if (rp ? rp.outerHTML !== want : want) queueRender(true); }
        queueMeters(); break;
      case 'takeInfo':
        if (m.take !== S.take) break;
        S.takeInfo = m;
        if (S.pickFrom == null) S.pickFrom = m.marks && m.marks.length ? m.marks[0][0] : 0;
        if (S.screen === 'take') queueRender(true);
        break;
      case 'listenAck':
        if (!m.ok) { S.making = false; toast(m.why || 'COULD NOT START'); if (S.screen === 'take') render(true); }
        break;
      case 'listenDone':
        S.making = false;
        if (m.ok && S.take) go('listen');
        else if (!m.ok) { toast('LISTENING PACK STOPPED'); if (S.screen === 'take') { send({ t: 'takeInfo', take: S.take }); render(true); } }
        break;
      case 'listenList':
        if (m.take !== S.take) break;
        S.pack = m.ok ? m : null;
        if (S.screen === 'listen') queueRender(true);
        break;
      case 'listenChunk': onChunk(m); break;
      case 'defaultAck':
        S.defAck = m;
        if (S.screen === 'module') queueRender(true);
        break;
      case 'listenUseAck':
        if (m.ok) toast('ON ' + m.onto.toUpperCase() + ': SAVE OR REVERT');
        else toast(m.why || 'COULD NOT USE IT');
        break;
      case 'takes':
        S.takes = m.list || []; S.takesFree = m.free || 0;
        if (S.screen === 'recorder') queueRender(true);
        break;
      case 'kalParams':
        S.kalParams = m.params || []; S.kalId = m.id || '';
        queueRender(); break;
      case 'kalValues':
        if (m.id !== S.kalId) break;
        (m.v || []).forEach(function (c) {
          var p = S.kalParams[c[0]]; if (!p) return;
          if (!(S.dragging && S.kalDrag === c[0])) p.v = c[1];
          p.text = c[2];
          var lab = document.querySelector('[data-kaltext="' + c[0] + '"]'); if (lab) lab.textContent = c[2];
        });
        queueRender(true); break;
      case 'toast':
        toast(m.text);
        if (m.text === 'KEPT') { S.noteUntil = Date.now() + 20000; nod(); render(true); }
        break;
    }
  }

  // Demo: try every screen with no rig. The real Keystone takes over the moment it's found.
  function demo(on) {
    if (!window.KSDemo) return;
    S.demo = on;
    if (on) { KSDemo.start(); S.link = 'connected'; S.linkDetail = 'Demo: a pretend Keystone in this phone'; send({ t: 'hello', client: 'android', v: 1 }); toast('DEMO'); }
    else { KSDemo.stop(); S.link = S.realLink || 'searching'; S.linkDetail = S.realDetail || ''; S.health = null; S.meter = { rec: { on: false } }; loadOffline(); }
    render(true);
  }
  function onLink(status, detail) {
    S.realLink = status; S.realDetail = detail || '';
    if (S.demo && status !== 'connected') return;   // stay in the demo while the real one is looking
    if (S.demo && status === 'connected') { S.demo = false; KSDemo.stop(); toast('KEYSTONE FOUND'); }
    if (status !== 'connected') { S.health = null; S.rtt = null; }
    S.link = status; S.linkDetail = detail || '';
    if (status === 'connected') send({ t: 'hello', client: 'android', v: 1 });
    render(true);
  }

  function useSchema(m) {
    S.schema = m; S.params = {}; S.modules = m.modules; S.pedalTable = m.pedals || [];
    m.params.forEach(function (p) { S.params[p.id] = p; });
  }
  // Menus work before Keystone is found: the last state seen, or the built-in defaults
  function loadOffline() {
    var sc = null, st = null;
    try { sc = JSON.parse(localStorage.getItem('ks.schema')); st = JSON.parse(localStorage.getItem('ks.state')); } catch (e) {}
    if (!sc && window.KS_DEFAULT) { sc = window.KS_DEFAULT.schema; st = window.KS_DEFAULT.state; }
    if (sc) useSchema(sc);
    if (st) { S.values = st.values || {}; S.songs = st.songs || []; S.parts = st.parts || []; S.bank = st.bank || 0; S.current = st.current || 0; S.captures = st.captures || []; S.capture = st.capture; S.captureFile = st.captureFile; }
  }

  // Renders and meter updates are coalesced into one per screen frame, so a burst of
  // messages never queues up work behind your finger
  var renderQueued = 0, metersQueued = false;
  function queueRender(soft) {
    if (renderQueued === 2 || (renderQueued === 1 && soft)) return;
    var first = !renderQueued;
    renderQueued = soft ? 1 : 2;
    if (first) requestAnimationFrame(function () { var hard = renderQueued === 2; renderQueued = 0; render(!hard); });
  }
  function queueMeters() {
    if (metersQueued) return;
    metersQueued = true;
    requestAnimationFrame(function () { metersQueued = false; meters(); });
  }

  function tunerState(on) {
    if (on === S.tuner) return;
    S.tuner = on;
    if (on && S.screen !== 'tuner') { S.prevScreen = S.screen; S.screen = 'tuner'; S.tunerAuto = true; }
    else if (!on && S.screen === 'tuner' && S.tunerAuto) { S.screen = S.prevScreen; S.tunerAuto = false; }
    render();
  }

  // ---------------------------------------------------------------- helpers
  function el(html) { var d = document.createElement('div'); d.innerHTML = html.trim(); return d.firstChild; }
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]; }); }
  function val(id) { var p = S.params[id]; return S.values[id] !== undefined ? S.values[id] : (p ? p.def : 0); }
  function on(id) { return val(id) > 0.5; }
  function two(n) { return (n < 10 ? '0' : '') + n; }
  function flip(text, cls) { return '<span class="flip ' + (cls || '') + '">' + esc(text) + '</span>'; }
  function trim(v, d) { return (Math.round(v * Math.pow(10, d)) / Math.pow(10, d)).toFixed(d); }

  function fmt(id, v) {
    var p = S.params[id]; if (!p) return '';
    if (v === undefined) v = val(id);
    if (p.kind === 'toggle') return v > 0.5 ? 'On' : 'Off';
    if (p.kind === 'choice') return p.choices[Math.round(v)] || '';
    if (id === 'drone.sustain' && v >= 0.995) return 'Inf';
    if (id === 'reverb.swell' && v < 0.01) return 'Off';
    if (/gate$/.test(id) && v <= -89) return 'Off';
    switch (p.unit) {
      case '%': return trim(v * 100, 0) + '%';
      case 'Hz': return v >= 1000 ? trim(v / 1000, 1) + 'k' : v < 10 ? trim(v, v < 1 ? 2 : 1) + 'Hz' : trim(v, 0);
      case 'ms': return trim(v, 0) + 'ms';
      case 's': return trim(v, v < 10 ? 1 : 0) + 's';
      case 'dB': return (v > 0.05 ? '+' : '') + trim(v, Math.abs(v) < 10 ? 1 : 0);
      case '/s': return trim(v, 0) + '/s';
    }
    if (id === 'delay.lofiBits') return trim(v, 0) + ' bits';
    return trim(v, Math.abs(v) < 10 ? 1 : 0);
  }
  // slider position 0..1 <-> value, log where the parameter says so
  function toPos(p, v) { return p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min); }
  function fromPos(p, x) { x = Math.max(0, Math.min(1, x)); return p.log ? p.min * Math.pow(p.max / p.min, x) : p.min + (p.max - p.min) * x; }
  function stepOf(p) {
    if (p.unit === '%') return 0.01;
    if (p.id === 'delay.lofiBits') return 1;
    if (p.log) return null;
    var span = p.max - p.min;
    return span <= 1 ? 0.01 : span <= 20 ? 0.1 : 1;
  }

  var ICON = {
    left: '<svg width="18" height="18" viewBox="0 0 16 16" aria-hidden="true"><path d="M10 3 L5 8 L10 13" fill="none" stroke="#F2EDE4" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    right: '<svg width="18" height="18" viewBox="0 0 16 16" aria-hidden="true"><path d="M6 3 L11 8 L6 13" fill="none" stroke="#F2EDE4" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    back: '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><path d="M10 3 L5 8 L10 13" fill="none" stroke="#B9B2A6" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    chev: '<svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true"><path d="M6 3 L11 8 L6 13" fill="none" stroke="#8A847A" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    grip: '<svg width="14" height="20" viewBox="0 0 14 20" aria-hidden="true"><circle cx="4" cy="4" r="2" fill="#8A847A"/><circle cx="10" cy="4" r="2" fill="#8A847A"/><circle cx="4" cy="10" r="2" fill="#8A847A"/><circle cx="10" cy="10" r="2" fill="#8A847A"/><circle cx="4" cy="16" r="2" fill="#8A847A"/><circle cx="10" cy="16" r="2" fill="#8A847A"/></svg>',
    dots: '<svg width="20" height="20" viewBox="0 0 20 20" aria-hidden="true"><circle cx="4" cy="10" r="2" fill="#F2EDE4"/><circle cx="10" cy="10" r="2" fill="#F2EDE4"/><circle cx="16" cy="10" r="2" fill="#F2EDE4"/></svg>'
  };
  var PEBBLE = '<svg class="pebble" id="pebble" width="44" height="44" viewBox="0 0 200 200" role="img" aria-label="Heavy for his Size">' +
    '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#2329A8"/>' +
    '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#4B57FF"/>' +
    '<g class="eye"><circle cx="78" cy="80" r="18" fill="#FFFFFF"/><circle cx="80" cy="82" r="9" fill="#0B0B12"/></g>' +
    '<g class="eye"><circle cx="122" cy="80" r="18" fill="#FFFFFF"/><circle cx="124" cy="82" r="9" fill="#0B0B12"/></g>' +
    '<path d="M88 116 Q 100 126, 112 116" fill="none" stroke="#0B0B12" stroke-width="6" stroke-linecap="round"/></svg>';

  var PEBBLE_K = '<svg class="pebble" id="pebble" width="44" height="44" viewBox="0 0 200 200" role="img" aria-label="Heavy for his Size in shades and a pork pie hat">' +
    '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#B8661E"/>' +
    '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#F28C28"/>' +
    '<rect x="56" y="72" width="40" height="22" rx="8" fill="#141110"/><rect x="104" y="72" width="40" height="22" rx="8" fill="#141110"/><rect x="94" y="78" width="12" height="5" fill="#141110"/>' +
    '<path d="M88 120 Q 100 130, 112 120" fill="none" stroke="#141110" stroke-width="6" stroke-linecap="round"/>' +
    '<g transform="rotate(-9 100 52)"><ellipse cx="100" cy="52" rx="52" ry="9" fill="#4A2E1B"/><path d="M70 52 L74 24 Q100 17 126 24 L130 52Z" fill="#6B4429"/>' +
    '<path d="M71.5 40 Q100 35 128.5 40 L129.5 50 Q100 46 70.5 50Z" fill="#F6EBDD"/><path d="M118 40 Q132 14 146 10 Q136 26 124 42Z" fill="#F28C28"/></g></svg>';
  // Heavy for his Size, Hev for short, in his four moods (the "Heavy for his Size" canvas, 0.5.9)
  function hev(mood, size, label) {
    var body = '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#2329A8"/>' +
      '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#4B57FF"/>';
    var eye = function (x, px, py) { return '<circle cx="' + x + '" cy="80" r="18" fill="#fff"/><circle cx="' + px + '" cy="' + py + '" r="9" fill="#0B0B12"/>'; };
    var line = function (d) { return '<path d="' + d + '" fill="none" stroke="#0B0B12" stroke-width="6" stroke-linecap="round"/>'; };
    var face = mood === 'worried' ? eye(78, 78, 84) + eye(122, 122, 84) + line('M60 62 L92 52') + line('M108 52 L140 62') + line('M86 126 Q 100 112, 114 126') + '<path d="M150 96 Q 160 112, 150 118 Q 140 112, 150 96 Z" fill="#9FD3FF"/>'
      : mood === 'unsure' ? eye(78, 86, 86) + eye(122, 130, 86) + line('M60 58 L94 62') + line('M88 120 L 112 118')
      : mood === 'asleep' ? line('M62 84 Q 78 94, 94 84') + line('M106 84 Q 122 94, 138 84') + '<circle cx="100" cy="120" r="7" fill="#0B0B12"/><text x="148" y="58" font-size="26" font-weight="700" fill="#F2EDE4">z</text><text x="164" y="38" font-size="18" font-weight="700" fill="#B9B2A6">z</text>'
      : eye(78, 80, 82) + eye(122, 124, 82) + line('M84 114 Q 100 132, 116 114');
    return '<svg class="hev" width="' + size + '" height="' + size + '" viewBox="0 0 200 200" role="img" aria-label="' + esc(label || 'Heavy for his Size') + '">' + body + face + '</svg>';
  }
  // which parameter switches a module on and off (Kuddle's are named their own way)
  var KON = { 'k.mute': 'kuddle.muteOn', 'k.wah': 'kuddle.wahOn', 'k.opto': 'kuddle.optoOn', 'k.tape': 'kuddle.tapeOn', 'k.tone': 'kuddle.toneOn', 'k.room': 'kuddle.roomOn' };
  function onp(mod) { return KON[mod] || (S.params[mod + '.on'] ? mod + '.on' : ''); }
  function kud() { return S.rig === 1 && !kalOn(); }
  function kalOn() { return !!(S.kal && S.kal.on); }

  // ---- the rehearsal recorder
  function rec() { return (S.meter && S.meter.rec) || { on: false }; }
  function clock(sec) {
    sec = Math.max(0, Math.round(sec || 0));
    var h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, x = sec % 60;
    return (h ? h + ':' + two(m) : m) + ':' + two(x);
  }
  function recPill() {
    var r = rec();
    if (!connected() || !r.on) return '';
    return '<button type="button" id="recPill" class="recpill" data-go="recorder" aria-label="Open the recorder"><span class="rdot"></span>REC ' + clock(r.s) + '</button>';
  }
  function native() { return window.KeystoneNative && window.KeystoneNative.roomStart ? window.KeystoneNative : null; }
  function micOk() { var N = native(); return !!(N && N.micAllowed()); }
  // the phone records the room while Keystone records a take, then sends it across. The
  // phone app follows the takes itself (so it works with the screen off): this only tells it
  // whether the room track is on.
  function roomSync(r) {
    var N = native(); if (!N || !N.roomSetting) return;
    if (!S.values || S.values['rec.room'] == null) return;
    var want = Math.round(val('rec.room')) === 1;
    if (S.roomSent !== want) { S.roomSent = want; N.roomSetting(want); }
  }
  function roomStatus() { var N = native(); try { return N ? JSON.parse(N.roomStatus()) : null; } catch (e) { return null; } }
  function fmtBytes(b) { return b >= 1e9 ? (b / 1e9).toFixed(1) + ' GB' : Math.round(b / 1e6) + ' MB'; }
  function takeTitle(n, song) {
    // "2026-11-17 19-42-05" reads as "Tue 17 Nov, 19:42"
    var m = /^(\d{4})-(\d\d)-(\d\d) (\d\d)-(\d\d)/.exec(n);
    if (!m) return { t: song || n, s: '' };
    var d = new Date(+m[1], +m[2] - 1, +m[3]);
    var day = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getDay()] + ' ' + (+m[3]) + ' ' + ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][+m[2] - 1];
    return { t: song || 'Take', s: day + ', ' + m[4] + ':' + m[5] };
  }
  function recorderScreen() {
    var r = rec(), c = connected(), rs = roomStatus(), roomOn = Math.round(val('rec.room')) === 1;
    var live = !c ? '<section class="card"><span class="small">Not connected to Keystone.</span></section>' : r.on ?
      '<section class="card rec-live"><div style="display:flex;align-items:center;justify-content:space-between"><span class="label" style="color:#E5484D">Recording</span>' +
        '<span class="small">' + (r.pk != null ? 'Peak ' + r.pk + ' dB' : '') + (r.clip ? ', <span style="color:#F0997B">clipped</span>' : '') + '</span></div>' +
        '<div class="rec-time">' + clock(r.s).split('').map(function (ch) { return ch === ':' ? '<span class="colon">:</span>' : flip(ch); }).join('') + '</div>' +
        '<span class="small">' + esc(r.take ? 'Started ' + takeTitle(r.take).s.split(', ')[1] : '') + (roomOn ? (rs && rs.recording ? '. The phone is recording the room, screen off is fine.' : '. Room track off: ' + (micOk() ? 'the mic didn\'t start' : 'mic not allowed')) : '') + '</span>' +
        (r.marks && r.marks.length ? '<div class="marks">' + r.marks.slice(-4).map(function (mk) { return '<span><b>' + clock(mk[0]) + '</b> ' + esc(mk[1]) + '</span>'; }).join('') + '</div>' : '') +
        '<button type="button" class="wide" data-act="recStop">End this take</button></section>' :
      '<section class="card"><span style="font-size:17px;font-weight:700">' + (on('rec.on') ? 'Waiting for you to play' : 'Recorder off') + '</span>' +
        '<span class="small">' + (on('rec.on') ? 'A take starts by itself on your first note, with the two seconds before it. A minute of quiet ends it.' : 'Switch on Record when I play below.') + '</span></section>';
    var settings = '<section class="card" style="gap:4px">' +
      '<label class="row" style="padding:0"><span><span class="t">Record when I play</span><span class="s">Your bass dry and through the rig, kept on Keystone</span></span>' +
        '<button type="button" class="pill' + (on('rec.on') ? ' lit' : '') + '" data-onoff="rec.on">' + (on('rec.on') ? 'ON' : 'OFF') + '</button></label>' +
      '<label class="row" style="padding:0"><span><span class="t">Room track</span><span class="s">' +
        (!native() ? 'Needs the Keystone phone app' : roomOn && !micOk() ? 'Tap to allow the mic' : 'This phone\'s mic, sent to Keystone after the take') + '</span></span>' +
        '<button type="button" class="pill' + (roomOn && micOk() ? ' lit' : '') + '" data-act="roomToggle">' + (roomOn && micOk() ? 'ON' : 'OFF') + '</button></label>' +
      (rs && rs.waiting ? '<span class="small" style="padding-bottom:8px">' + esc(rs.status || 'Room track waiting to send') + (rs.pct >= 0 ? ', ' + rs.pct + '%' : '') + (rs.waiting > 1 ? ' (' + rs.waiting + ' waiting)' : '') + '</span>' : '') +
      '</section>';
    var list = S.takes == null ? '<span class="small">Loading takes</span>' : !S.takes.length ? '<span class="small">No takes yet.</span>' :
      S.takes.slice(0, 30).map(function (tk) {
        var tt = takeTitle(tk.name, tk.song);
        return '<button type="button" class="row" data-take="' + esc(tk.name) + '"><span><span class="t">' + esc(tt.t) + '</span><span class="s">' + esc(tt.s) + '</span></span><span class="r muted">' + clock(tk.seconds) + ' ›</span></button>';
      }).join('');
    return '<section class="screen" style="gap:14px">' + header('Recorder', { id: 'play', label: 'Play' }, recPill()) + live + settings +
      '<section class="group"><h2 class="label">Takes on Keystone' + (S.takesFree ? ', ' + fmtBytes(S.takesFree) + ' free' : '') + '</h2>' + list + '</section>' +
      '<div class="small">Takes are kept in Keystone\'s Recordings folder: a WAV with your dry bass on the left and the rig on the right, the room track beside it, and the song changes and Keeps marked. When Keystone runs low on space the oldest takes go first.</div>' +
      nav('health') + '</section>';
  }

  // ---- the listening pack: a stretch of a take through every song
  var player = new Audio(), cache = {}, cacheOrder = [], fetching = null, fetchQueue = [];
  function labelOf(i) { var h = headOf(i); return isPart(i) ? S.songs[h] + ', ' + S.songs[i] : S.songs[i]; }
  function songNoOf(label) { for (var i = 0; i < S.songs.length; i++) if (labelOf(i) === label) return songNo(i); return 0; }
  // files come from the laptop in pieces over the link, then play from the phone's memory
  function fetchFile(file, then) {
    if (cache[file]) { if (then) then(cache[file]); return; }
    if (fetching && fetching.file === file) { if (then) fetching.then = then; return; }
    if (fetching) { fetchQueue = fetchQueue.filter(function (q) { return q.file !== file; }); fetchQueue.unshift({ file: file, then: then }); return; }
    fetching = { file: file, then: then, parts: [], got: 0, total: 0, take: S.take };
    send({ t: 'listenGet', take: S.take, file: file, offset: 0 });
  }
  function onChunk(m) {
    var f = fetching;
    if (!f || m.file !== f.file || m.take !== f.take) return;
    if (!m.ok) { fetching = null; toast('COULD NOT FETCH THAT ONE'); nextFetch(); return; }
    var bin = atob(m.data || ''), bytes = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    f.parts.push(bytes); f.got += bytes.length; f.total = m.total;
    if (bytes.length && f.got < f.total) {
      send({ t: 'listenGet', take: f.take, file: f.file, offset: f.got });
      var lp = document.getElementById('load-' + rowId(f.file)); if (lp) lp.textContent = 'Loading ' + Math.round(100 * f.got / f.total) + '%';
      return;
    }
    var url = URL.createObjectURL(new Blob(f.parts, { type: 'audio/wav' }));
    cache[f.file] = url; cacheOrder.push(f.file);
    while (cacheOrder.length > 6) { var old = cacheOrder.shift(); if (S.playing !== old) { URL.revokeObjectURL(cache[old]); delete cache[old]; } }
    fetching = null;
    if (f.then) f.then(url);
    nextFetch();
  }
  function nextFetch() { var q = fetchQueue.shift(); if (q) fetchFile(q.file, q.then); }
  function rowId(file) { return file.replace(/[^a-z0-9]/gi, '_'); }
  function playFile(file) {
    if (S.playing === file && !player.paused) { player.pause(); render(true); return; }
    S.playing = file; S.lastPlayed = file; S.loadingFile = file;
    render(true);
    fetchFile(file, function (url) {
      if (S.playing !== file) return;
      S.loadingFile = null;
      player.src = url; player.currentTime = 0;
      player.play().catch(function () {});
      render(true);
      var nx = nextSongFile(file); if (nx) fetchFile(nx);   // ready for the one after
    });
  }
  function nextSongFile(file) {
    var fs = S.pack ? S.pack.files : [];
    for (var i = 0; i < fs.length - 1; i++) if (fs[i].file === file) return fs[i + 1].file;
    return null;
  }
  // one after another, so you can compare
  player.addEventListener('ended', function () { var nx = nextSongFile(S.playing); if (nx && S.screen === 'listen') playFile(nx); else { S.playing = null; render(true); } });
  player.addEventListener('timeupdate', function () {
    var el2 = document.getElementById('pos-' + rowId(S.playing || ''));
    if (el2) el2.textContent = 'playing ' + clock(player.currentTime) + ' of ' + clock(player.duration || 0);
  });
  function stopListening() {
    player.pause(); S.playing = null; fetchQueue = []; fetching = null;
    cacheOrder.forEach(function (f) { URL.revokeObjectURL(cache[f]); }); cache = {}; cacheOrder = [];
  }
  function takeScreen() {
    var i = S.takeInfo, tt = takeTitle(S.take, i && i.song), r = rec(), prog = r && S.meter.listen;
    var marks = i && i.marks && i.marks.length ? i.marks : [[0, 'From the start']];
    var pick = marks.map(function (mk, k) {
      var sel = Math.round(S.pickFrom || 0) === Math.round(mk[0]);
      return '<button type="button" class="pick' + (sel ? ' on' : '') + '" data-from="' + mk[0] + '"' + (k === marks.length - 1 ? ' style="border:none"' : '') + '><b>' + clock(mk[0]) + '</b><span>' + esc(mk[1]) + '</span><span class="rd"></span></button>';
    }).join('');
    var making = S.making || !!prog;
    var btn = making ? '<button type="button" class="wide go" disabled>Making it: ' + (prog ? prog.done + ' of ' + prog.of + ' songs' : 'starting') + '</button>'
      : '<button type="button" class="wide go" data-act="makePack">Make the listening pack</button>';
    return '<section class="screen" style="gap:14px">' + header('Take', { id: 'recorder', label: 'Recorder' }) +
      '<section class="card" style="gap:4px"><span style="font-size:20px;font-weight:700">' + esc(tt.t) + '</span><span class="small">' + esc(tt.s) +
        (i ? ', ' + clock(i.seconds) + ' long' + (i.room ? ', room track included' : '') : '') + '</span></section>' +
      (i && i.pack ? '<button type="button" class="wide" data-go="listen">Open the listening pack (from ' + clock(i.pack.from) + (i.pack.played ? ', ' + esc(i.pack.played) : '') + ')</button>' : '') +
      '<section class="card" style="gap:2px"><h2 class="label">Listening pack: pick a stretch</h2>' +
        '<span class="small" style="padding-bottom:6px">Thirty seconds of your bass from here, played through every song and part, so you can hear which sound suits it.</span>' +
        (i ? pick : '<span class="small">Loading the take</span>') + btn +
        '<span class="small">Takes about a minute on the ' + hostName() + '. It won\'t run while you\'re playing.</span></section>' +
      nav('health') + '</section>';
  }
  function listenScreen() {
    var p = S.pack;
    if (!p) return '<section class="screen" style="gap:12px">' + header('Listen', { id: 'take', label: 'Take' }) + '<span class="small">Loading the listening pack</span>' + nav('health') + '</section>';
    function row(f) {
      var on = S.playing === f.file, id = rowId(f.file), loading = S.loadingFile === f.file;
      var sub = f.kind === 'dry' ? 'Straight from the instrument' : f.kind === 'played' ? (p.played || 'The rig as it was') : 'Song ' + (songNoOf(f.label) || '?');
      var use = f.kind === 'song' && p.played && f.label !== p.played && (on || S.lastPlayed === f.file);
      return '<div class="lrow' + (on ? ' on' : '') + '"><button type="button" class="play' + (on && !loading && !player.paused ? ' on' : '') + '" data-listen="' + esc(f.file) + '" aria-label="Play ' + esc(f.label) + '">' +
        (on && !loading && !player.paused ? ICON_PAUSE : ICON_PLAY) + '</button><span style="flex:1;min-width:0"><span class="t">' + esc(f.label) + '</span><span class="s">' + esc(sub) +
        (loading ? ', <span id="load-' + id + '">Loading</span>' : on ? ', <span id="pos-' + id + '">playing</span>' : '') + '</span></span>' +
        (use ? '<button type="button" class="use" data-use="' + esc(f.label) + '">USE</button>' : '') + '</div>';
    }
    var top = p.files.filter(function (f) { return f.kind !== 'song'; }), songs = p.files.filter(function (f) { return f.kind === 'song'; });
    return '<section class="screen" style="gap:12px">' + header('Listen', { id: 'take', label: 'Take' }) +
      '<section class="card" style="gap:6px"><span style="font-size:17px;font-weight:700">' + esc((p.played || 'Your take') + ', from ' + clock(p.from)) + '</span>' +
      '<span class="small">Tap a sound to hear your thirty seconds through it. The phone plays them one after another so you can compare.</span></section>' +
      top.map(row).join('') + '<h2 class="label" style="margin-top:6px">Through every song' + (p.making ? ' (still making)' : '') + '</h2>' + songs.map(row).join('') +
      (p.played ? '<div class="small">USE copies that sound onto the song you played (' + esc(p.played) + ') so you can try it at the next rehearsal. Save keeps it, Revert brings the old sound back.</div>' : '') +
      nav('health') + '</section>';
  }
  var ICON_PLAY = '<svg width="16" height="16" viewBox="0 0 16 16"><path d="M4 2 L14 8 L4 14Z" fill="currentColor"/></svg>';
  var ICON_PAUSE = '<svg width="16" height="16" viewBox="0 0 16 16"><rect x="3" y="2" width="3.5" height="12" fill="currentColor"/><rect x="9.5" y="2" width="3.5" height="12" fill="currentColor"/></svg>';

  // ---- Keystone's own Wi-Fi: the phone joins the rig's network, no hotspot needed
  function wifiCard() {
    var w = S.wifi;
    if (!w || !w.can) return '';
    var st = w.status || (w.on ? 'Starting' : 'Off'), up = /^On/.test(st);
    return '<section class="card" style="gap:8px"><div style="display:flex;justify-content:space-between;align-items:center"><h2 class="label">Keystone\'s Wi-Fi</h2>' +
      '<button type="button" class="pill' + (w.on ? ' lit' : '') + '" data-act="wifiToggle">' + (w.on ? 'ON' : 'OFF') + '</button></div>' +
      '<span style="font-size:15px;color:' + (up ? '#5DCAA5' : w.on ? '#F0997B' : '#B9B2A6') + '">' + esc(st) + '</span>' +
      '<div class="wifirow"><span class="lab">Network</span><b>' + esc(w.ssid) + '</b></div>' +
      '<div class="wifirow"><span class="lab">Password</span><b class="pw">' + esc(w.pass) + '</b></div>' +
      (window.KeystoneNative && KeystoneNative.wifiAdd ? '<button type="button" class="wide go" data-act="wifiAdd">Save it on this phone</button>' : '') +
      '<span class="small">Save it once and the phone joins by itself whenever the rig is on, with no hotspot. Android may say it has no internet: just ignore that, and don\'t tap Keep or Stay connected, or Android sends everything through Keystone and mobile data stops working. You stay connected either way.</span></section>';
  }

  // ---- Update firmware (the laptop or the Pi) and the phone app, from GitHub
  function newer(a, b) {   // is version a newer than b?
    var x = String(a || '').split('.'), y = String(b || '').split('.');
    for (var i = 0; i < Math.max(x.length, y.length); i++) { var d = (+x[i] || 0) - (+y[i] || 0); if (d) return d > 0; }
    return false;
  }
  function phoneVersion() { try { return window.KeystoneNative ? KeystoneNative.version() : ''; } catch (e) { return ''; } }
  function updateCheck(force) {
    var N = window.KeystoneNative;
    if (!N || !N.updateCheck || S.upd.checking) return;
    if (!force && Date.now() - S.upd.checkedAt < 10 * 60 * 1000) return;
    S.upd.checking = true; S.upd.err = null;
    N.updateCheck();
  }
  function onUpdate(m) {
    var u = S.upd;
    if (m.t === 'latest') { u.latest = m.version; u.assets = m.assets || []; u.checkedAt = Date.now(); u.checking = false; }
    else if (m.t === 'progress') { u.busy = m; u.err = null; }
    else if (m.t === 'error' && m.what === 'watch') {
      u.busy = null; S.watchErr = m;
      if (m.kind === 'pair') { watchSetPaired(false); S.watchErr = null; S.watchAfterPair = true; go('pairWatch'); return; }
    }
    else if (m.t === 'error') { u.busy = null; u.checking = false; u.err = m; }
    else if (m.t === 'paired') {
      u.busy = null; S.watchErr = null; watchSetPaired(true); S.pairCode = '';
      go('health');
      if (S.watchAfterPair) { S.watchAfterPair = false; startWatchUpdate(); }
      return;
    }
    else if (m.t === 'done' && m.what === 'watch') { u.busy = null; S.watchErr = null; S.watchDone = m.version; }
    else if (m.t === 'done') {
      u.busy = null; u.msg = 'Installing ' + m.version + '. Keystone restarts by itself in about a minute, and goes back to the old version if the new one doesn\'t start.';
      // the watch too, if it's behind and that's switched on
      if (watchAuto() && watchNew()) setTimeout(function () { startWatchUpdate(); }, 300);
    }
    if (S.screen === 'health' || S.screen === 'pairWatch') render(true);
  }
  function asset(re) { for (var i = 0; i < S.upd.assets.length; i++) if (re.test(S.upd.assets[i].name)) return S.upd.assets[i]; return null; }

  // ---- Update watch: the phone installs the watch app over the watch's wireless debugging
  var PHONE_APK = /^Keystone-[\d.]+\.apk$/, WATCH_APK = /^Keystone-Watch-[\d.]+\.apk$/;
  function canWatch() { return !!(window.KeystoneNative && KeystoneNative.watchUpdate); }
  function watchInfo() {   // remembered, so the watch can still be updated while Keystone restarts
    var w = S.health && S.health.watch;
    if (w && w.on) { S.watchIp = w.ip; S.watchV = w.v; }   // only ever what the watch itself says
    return { v: S.watchV || '', ip: S.watchIp || '' };
  }
  function watchNew() { var u = S.upd, wi = watchInfo(); return !!(u.latest && wi.v && newer(u.latest, wi.v) && asset(WATCH_APK)); }
  function watchAuto() { try { return localStorage.getItem('ks.watchAuto') !== '0'; } catch (e) { return true; } }
  function watchPaired() { try { return KeystoneNative.watchPaired(); } catch (e) { return false; } }
  function watchSetPaired(on) { try { KeystoneNative.watchSetPaired(on); } catch (e) { } }
  function startWatchUpdate(manual) {
    var wa = asset(WATCH_APK), wi = watchInfo();
    if (!wa || !canWatch() || S.upd.busy) return;
    if (!watchPaired()) { S.watchAfterPair = true; go('pairWatch'); return; }
    S.watchErr = null; S.watchDone = ''; S.upd.err = null;
    S.upd.busy = { what: 'watch', stage: 'Downloading', pct: 0 };
    KeystoneNative.watchUpdate(wa.url, wa.size, S.upd.latest, wi.ip, manual || '');
    render(true);
  }
  function watchCards() {
    if (!canWatch()) return '';
    var wi = watchInfo(), e = S.watchErr, out = '';
    if (e && e.kind !== 'code' && e.kind !== 'nopair') {
      var off = e.kind === 'off';
      out += '<section class="card" style="gap:12px;border:2px solid #F0997B"><span style="font-size:19px;font-weight:700;color:#F0997B">' + (off ? 'Can\'t reach the watch' : 'The watch didn\'t update') + '</span>' +
        (off ? '<span class="small" style="font-size:15px">Wireless debugging is off, or the watch isn\'t on Keystone\'s Wi-Fi. The watch turns wireless debugging off when it restarts.</span>' +
          '<ol style="margin:0;padding-left:22px;display:flex;flex-direction:column;gap:8px;font-size:17px;line-height:1.3"><li>On the watch: Settings, Developer options</li><li>Turn <b>Wireless debugging</b> on</li><li>Open the Keystone app on the watch</li></ol>'
          : '<span class="small" style="font-size:15px">' + esc(e.why) + '</span>') +
        '<button type="button" class="wide go" style="margin-top:0" data-act="updateWatch">Try again</button>' +
        (off ? '<details><summary class="small" style="min-height:44px;display:flex;align-items:center">Type the address instead</summary><div class="note" style="margin-top:6px"><input id="watchAddr" type="text" inputmode="url" placeholder="IP address and port, as the watch shows" aria-label="Watch address and port"><button type="button" data-act="updateWatchTyped">Go</button></div>' +
          '<span class="small" style="font-size:13px">Wireless debugging on the watch shows it, like 192.168.137.110:41235.</span></details>' : '') +
        '<span class="small" style="font-size:14px;color:#8A847A">Bugjaeger still works as a backup.</span></section>';
    }
    if (S.watchDone && !S.upd.busy) out += '<section class="card" style="gap:8px;border:2px solid #5DCAA5"><span style="font-size:17px;font-weight:700;color:#5DCAA5">Watch app installed from ' + esc(S.watchDone) + '</span><span class="small">The watch app opens again by itself and reconnects. The Watch row above shows the version the watch reports once it\'s back.</span></section>';
    if (!wi.v) return out;
    var auto = watchAuto();
    return out + '<section class="card" style="gap:10px"><h2 class="label">Watch</h2>' +
      '<div style="display:flex;justify-content:space-between;align-items:center;gap:12px;min-height:48px"><span style="font-size:17px">Update the watch with Update firmware</span>' +
      '<button type="button" class="pill' + (auto ? ' lit' : '') + '" data-act="watchAuto">' + (auto ? 'ON' : 'OFF') + '</button></div>' +
      '<span class="small" style="color:#8A847A">' + (auto ? 'On: one tap on Update firmware does Keystone, then the watch.' : 'Off: update the watch with its own button.') + '</span>' +
      (watchPaired() ? '<button type="button" class="back" data-act="watchUnpair" style="min-height:44px;font-size:14px;align-self:flex-start">Pair the watch again</button>' : '') + '</section>';
  }
  function pairWatchScreen() {
    var b = S.upd.busy, e = S.watchErr, code = S.pairCode || '';
    var tiles = '';
    for (var i = 0; i < 6; i++) tiles += '<span class="flip' + (i === code.length ? ' blue' : '') + '" style="width:46px;height:66px;font-size:38px">' + esc(code.charAt(i)) + '</span>';
    return '<section class="screen" style="gap:16px">' + header('Pair watch', { id: 'health', label: 'Rig' }) +
      '<section class="card" style="gap:12px"><h2 class="label">On the watch</h2><ol style="margin:0;padding-left:22px;display:flex;flex-direction:column;gap:10px;font-size:17px;line-height:1.3">' +
      '<li>Settings, Developer options, <b>Wireless debugging</b>: turn it on</li><li>Tap <b>Pair new device</b></li><li>Type the six numbers it shows</li></ol></section>' +
      '<section class="card" style="gap:14px;align-items:center"><label for="pairCode" class="label">Pairing code</label>' +
      '<div style="position:relative;display:flex;gap:8px">' + tiles +
      '<input id="pairCode" type="text" inputmode="numeric" autocomplete="one-time-code" maxlength="6" value="' + esc(code) + '" aria-label="Six-digit pairing code" style="position:absolute;inset:0;opacity:0;width:100%;height:100%;font-size:16px"></div>' +
      (b ? '<span class="small" style="color:#5DCAA5">' + esc(b.stage) + '</span>' : e && (e.kind === 'code' || e.kind === 'nopair') ? '<span class="small" style="color:#F0997B">' + esc(e.why) + '</span>' : '<span class="small">Tap the boxes to type</span>') + '</section>' +
      '<button type="button" class="wide blue" data-act="pairWatch"' + (code.length === 6 && !b ? '' : ' disabled') + '>Pair</button>' +
      '<details><summary class="small" style="min-height:44px;display:flex;align-items:center">The phone can\'t find the watch? Type its address</summary><div class="note" style="margin-top:6px"><input id="pairAddr" type="text" inputmode="url" placeholder="IP address and port under the code" aria-label="Pairing address and port"></div></details>' +
      '<span class="small">You only do this once. The phone finds the watch by itself, so normally there\'s nothing else to type.</span></section>';
  }
  function rigDevice() { return S.health && S.health.device === 'pi' ? 'pi' : 'laptop'; }
  // Buffer: one tap finds the smallest size this laptop plays without dropouts
  function bufferCard() {
    var m = S.meter || {}, bf = m.bf, h = S.health;
    var ms = m.buf && m.sr ? Math.round(1000 * m.buf / m.sr * 10) / 10 : null;
    var head = '<span class="small" style="letter-spacing:.15em;font-weight:700">BUFFER</span>';
    var tried = function () {
      var out = [];
      (bf.steps || [3, 4, 6, 10]).forEach(function (x, i) { var r = bf.tried[i]; if (r > 0) out.push(x + ' ms: ' + r + ' dropout' + (r === 1 ? '' : 's') + ', too small'); });
      return out.join('. ');
    };
    if (bf && bf.on) {
      var t = tried();
      return '<section class="card" style="gap:10px">' + head +
        '<div style="font-size:19px;font-weight:700">Trying ' + bf.ms + ' ms <span class="small">(' + (bf.step + 1) + ' of 4)</span></div>' +
        '<div style="height:8px;border-radius:4px;background:#2a2a30;overflow:hidden"><div style="width:' + Math.round(100 * bf.progress) + '%;height:100%;background:#5DCAA5"></div></div>' +
        '<span class="small">' + (t ? esc(t) + '. ' : '') + 'Keep playing.</span>' +
        '<button type="button" class="wide" data-act="bufferStop">Stop and keep ' + bf.was + ' ms</button></section>';
    }
    if (bf && bf.done) {
      var quicker = bf.was - bf.done, t2 = tried();
      return '<section class="card" style="gap:10px">' + head +
        '<div style="font-size:19px;font-weight:700">Set to ' + bf.done + ' ms</div>' +
        '<span class="small">' + (bf.lat ? bf.lat + ' ms round trip' : '') + (quicker > 0 ? ', ' + quicker + ' ms quicker than before' : quicker < 0 ? ', ' + (-quicker) + ' ms more than before, so it plays clean' : '') + '. ' +
        (t2 ? esc(t2) + '. ' : '') + bf.done + ' ms ran clean for 15 seconds' + (bf.cpu ? ' with the ' + hostName() + ' at ' + bf.cpu + '%' : '') + '. Keystone remembers this for this ' + hostName() + '.</span></section>';
    }
    var drops = h && h.laptop ? h.laptop.dropouts : null;
    return '<section class="card" style="gap:10px">' + head +
      '<div style="display:flex;justify-content:space-between;align-items:center;gap:12px"><div><div style="font-size:19px;font-weight:700">' + (ms ? ms + ' ms' : 'Not playing') + '</div>' +
      '<div class="small">' + (m.lat ? m.lat + ' ms round trip' : '') + (drops != null ? (m.lat ? ', ' : '') + (drops ? drops + ' dropout' + (drops === 1 ? '' : 's') + ' since it started' : 'no dropouts') : '') + '</div></div>' +
      '<button type="button" data-act="bufferFind" style="padding:12px 16px;background:none;border:2px solid #5DCAA5;color:#5DCAA5;border-radius:12px;font-weight:700;font-size:16px;flex:none">Find the best</button></div>' +
      '<span class="small">Tries each size for 15 seconds, smallest first, and keeps the smallest with no dropouts. Takes about a minute. The sound cuts for a moment between tries, so do it at soundcheck, playing your busiest song.</span></section>';
  }

  function updateCard() {
    var u = S.upd, h = S.health, rigV = h ? h.version : '', phoneV = phoneVersion();
    if (!window.KeystoneNative || !KeystoneNative.updateCheck) return '';
    var dev = rigDevice(), devName = dev === 'pi' ? 'Pi' : 'Laptop';
    var fwAsset = asset(dev === 'pi' ? /linux-arm64\.tar\.gz$/ : /-Setup\.exe$/), apk = asset(PHONE_APK), wi = watchInfo();
    var fwNew = u.latest && rigV && newer(u.latest, rigV) && fwAsset, appNew = u.latest && phoneV && newer(u.latest, phoneV) && apk;
    function row(name, now, isNew, act, label) {
      var st = !u.latest ? (u.checking ? 'Checking' : '') : isNew ? u.latest + ' is out' : 'Up to date';
      return '<div class="urow"><span style="display:flex;flex-direction:column;flex:1;min-width:0"><span style="font-size:17px;font-weight:500">' + esc(name) + '</span>' +
        '<span class="small" style="font-size:13px">' + esc(now) + (st ? ', <span style="color:' + (isNew ? '#5DCAA5' : '#8A847A') + '">' + st + '</span>' : '') + '</span></span>' +
        (isNew ? '<button type="button" class="ubtn" data-act="' + act + '"' + (u.busy ? ' disabled' : '') + '>' + label + '</button>' : '') + '</div>';
    }
    var busy = u.busy ? '<div class="small">' + esc(u.busy.stage) + (u.busy.what === 'app' ? ' the phone app' : u.busy.what === 'watch' && u.busy.stage === 'Downloading' ? ' the watch app' : '') + ', ' + u.busy.pct + '%</div><div class="bar" style="height:8px"><div style="width:' + u.busy.pct + '%;background:#5DCAA5;height:8px"></div></div>' : '';
    return '<section class="card" style="gap:6px"><div style="display:flex;justify-content:space-between;align-items:center"><h2 class="label">Updates</h2>' +
      '<button type="button" class="back" data-act="updateCheck" style="min-height:32px;font-size:14px">' + (u.checking ? 'Checking' : 'Check again') + '</button></div>' +
      (connected() && h ? row(devName, 'Keystone ' + rigV, fwNew, 'updateFirmware', 'Update firmware') : '') +
      (canWatch() && wi.v ? row('Watch', 'App ' + wi.v, watchNew(), 'updateWatch', 'Update watch') : '') +
      row('This phone', 'App ' + (phoneV || '?'), appNew, 'updateApp', 'Update app') + busy +
      (u.err ? '<div class="small" style="color:#F0997B">' + esc(u.err.why) + '</div>' : '') +
      (u.msg && !u.busy ? '<div class="small" style="color:#5DCAA5">' + esc(u.msg) + '</div>' : '') + '</section>';
  }

  function pill() {
    if (S.demo) return '<button type="button" id="linkPill" class="pill demo" data-go="health" style="border:none" aria-label="Open Rig"><span class="dot" style="background:#F0C27B"></span>Demo</button>';
    var c = S.link === 'connected', w = warnings().length;
    var label = !c ? 'Searching' : w ? w + (w > 1 ? ' problems' : ' problem') : 'Connected';
    return '<button type="button" id="linkPill" class="pill" data-go="health" style="border:none" aria-label="Open Health"><span class="dot ' + (!c ? 'searching' : w ? 'warn' : '') + '"></span>' + label + '</button>';
  }

  // ---- Health: what to check on stage, in plain words
  function warnings() {
    var h = S.health, out = [];
    if (!connected() || !h) return out;
    if (!h.audio.ok) out.push(/NOT THE AUDIO INTERFACE/.test(h.audio.text) ? 'Not using the UM2: plug it in, Keystone switches over by itself' : 'Audio interface not running: ' + h.audio.text);
    else if (h['in'].dead) out.push('No signal at all from the interface. Check Windows microphone access and the cable.');
    else if (h['in'].db > -1) out.push('Bass input clipping: turn the input gain down');
    if (!h.fs.ok) out.push('Footswitches not connected');
    if (h.laptop.recent > 0) out.push(h.laptop.recent + ' dropouts in the last minute');
    if (h.laptop.cpu > 80) out.push(hostName(true) + ' CPU at ' + h.laptop.cpu + '%');
    if (h.sys && h.sys.uv) out.push('Power dipping: use the official 27 W supply');
    if (h.sys && h.sys.temp >= 80) out.push(hostName(true) + ' running hot (' + Math.round(h.sys.temp) + '\u00B0C): give it some air');
    if (S.meter && S.meter.lat > 16) out.push('Round trip ' + S.meter.lat + ' ms: too slow to play through. ' + (Math.round(val('output.mode')) === 2 ? 'Tap ASIO panel on Health and set the buffer to 128' : 'Set the audio buffer lower'));
    if (S.healthAt && Date.now() - S.healthAt > 5000) out.push('Keystone has gone quiet: no news for ' + Math.round((Date.now() - S.healthAt) / 1000) + ' s');
    return out;
  }
  function stripText(w) { return w[0] + (w.length > 1 ? ' (+' + (w.length - 1) + ' more)' : ''); }
  function strip() {
    var el = document.getElementById('warnStrip'), w = warnings();
    var pl = document.getElementById('linkPill');
    if (pl) { var np = pill(); if (pl.outerHTML !== np) pl.outerHTML = np; }
    if (S.screen !== 'play' || !w.length) { if (el) el.remove(); return; }
    var html = '<button type="button" id="warnStrip" data-go="health" class="warnstrip">' + esc(stripText(w)) + '</button>';
    if (el) { if (el.textContent !== stripText(w)) el.outerHTML = html; }
    else { var top = document.querySelector('.screen .top'); if (top) top.insertAdjacentHTML('afterend', html); }
  }
  function ago(sec) { return sec < 0 ? 'never' : sec < 60 ? sec + ' s ago' : Math.round(sec / 60) + ' min ago'; }
  // Hev reads the Health list: happy when it's all green, unsure at amber, worried at red
  function hevCard(rows, c) {
    var bad = rows.filter(function (r) { return r.bad; }), amber = rows.filter(function (r) { return !r.ok && !r.bad; });
    var mood = !c ? 'asleep' : bad.length ? 'worried' : amber.length ? 'unsure' : 'happy';
    var say = !c ? 'Looking for Keystone.' : bad.length ? bad[0].n + ' needs you now.' : amber.length ? 'Have a look at ' + amber[0].n.toLowerCase() + '.' : 'All good. Play on.';
    var more = !c ? 'He wakes up the moment the phone finds it.' : bad.length || amber.length ? 'It\'s the ' + (bad.length ? 'red' : 'amber') + ' row below.' : 'Everything below is green.';
    return '<section class="card" style="flex-direction:row;align-items:center;gap:16px">' + hev(mood, 88, 'Heavy for his Size, ' + mood) +
      '<span style="display:flex;flex-direction:column;gap:4px;min-width:0"><span class="label" style="font-size:12px">Heavy for his Size says</span>' +
      '<span style="font-size:22px;font-weight:700;line-height:1.15">' + esc(say) + '</span><span class="small">' + esc(more) + '</span></span></section>';
  }
  function healthScreen() {
    var h = S.health, c = connected(), g = '#5DCAA5', o = '#F0997B', w = warnings();
    var head = !c ? '<span class="small" style="color:#F0997B">Not connected</span>' : w.length ? '<span class="small" style="color:#F0997B">' + w.length + (w.length > 1 ? ' problems' : ' problem') + '</span>' : '<span class="small" style="color:#5DCAA5">All good</span>';
    var rows = [];
    if (!c || !h) rows.push({ n: 'Phone link', d: S.linkDetail || 'Looking for Keystone', ok: false, st: c ? 'WAITING' : 'SEARCHING' });
    else {
      rows.push({ n: 'Audio interface', d: h.audio.text, ok: h.audio.ok });
      rows.push({ n: 'Footswitches', d: h.fs.text + (h.fs.ago >= 0 ? ', last press ' + ago(h.fs.ago) + ' (' + h.fs.last + ')' : ''), ok: h.fs.ok });
      rows.push({ n: 'Expression', d: h.exp.ago < 0 ? 'Not moved yet' : 'At ' + Math.round(h.exp.pos * 100) + '%, moved ' + ago(h.exp.ago) + (h.exp.cal ? '' : ', not calibrated') + (h.exp.rev ? ', reversed' : ''), ok: h.exp.ago >= 0, st: h.exp.ago < 0 ? 'IDLE' : 'OK' });
      rows.push({ n: 'Output', d: h.out.db + ' dB peak' + (h.out.lim > 0.5 ? ', limiter working ' + h.out.lim.toFixed(1) + ' dB' : ', limiter idle'), ok: true });
      rows.push({ n: hostName(true), d: 'CPU ' + h.laptop.cpu + '%, ' + (h.laptop.dropouts ? h.laptop.dropouts + ' dropouts, ' + h.laptop.recent + ' in the last minute' : 'no dropouts') + (h.laptop.light ? ', effects light' : '') + ', up ' + Math.round(h.uptime / 60) + ' min', ok: h.laptop.cpu <= 80 && h.laptop.recent === 0 });
      // the machine itself (0.5.8): temperature and power on the Pi, the interface, storage, memory
      var sy = h.sys;
      if (sy) {
        if (sy.temp >= 0) rows.push({ n: 'Temperature', d: Math.round(sy.temp) + '\u00B0C' + (sy.fan >= 0 ? (sy.fan > 0 ? ', fan ' + sy.fan.toLocaleString('en-GB') + ' rpm' : ', fan resting') : '') + (sy.temp >= 80 ? ', slowing down to cool' : sy.temp >= 75 ? ', running hot' : ''),
          ok: sy.temp < 75, bad: sy.temp >= 80, st: sy.temp >= 80 ? 'HOT' : sy.temp >= 75 ? 'WARM' : 'OK' });
        if (sy.power) rows.push({ n: 'Power', d: sy.uv ? 'Voltage dipping now: use the official 27 W supply' : sy.uvEver ? 'Dipped since start, steady now' : sy.thr ? 'Slowed for heat' : sy.thrEver ? 'Slowed earlier, fine now' : 'Steady since start',
          ok: !sy.uv && !sy.uvEver && !sy.thr && !sy.thrEver, bad: sy.uv || sy.thr, st: sy.uv ? 'LOW' : sy.uvEver ? 'DIPPED' : sy.thr ? 'SLOWED' : sy.thrEver ? 'CHECK' : 'OK' });
        rows.push({ n: 'Interface', d: sy.reconnects ? 'Dropped and came back ' + sy.reconnects + (sy.reconnects === 1 ? ' time' : ' times') + ' since start: check the USB cable' : 'Never dropped since start',
          ok: !sy.reconnects, st: sy.reconnects ? 'CHECK' : 'OK' });
        if (sy.free >= 0) rows.push({ n: 'Storage', d: (sy.free >= 10240 ? Math.round(sy.free / 1024) + ' GB' : (sy.free / 1024).toFixed(1) + ' GB') + ' free' + (sy.free < 5120 ? ': the recorder is clearing your oldest takes' : ''),
          ok: sy.free >= 5120, st: sy.free >= 5120 ? 'OK' : 'LOW' });
        if (sy.memTotal > 0) rows.push({ n: 'Memory', d: sy.memUsed.toLocaleString('en-GB') + ' MB of ' + (sy.memTotal >= 1024 ? Math.round(sy.memTotal / 1024) + ' GB' : sy.memTotal + ' MB') + ' in use',
          ok: sy.memUsed / sy.memTotal < 0.85, st: sy.memUsed / sy.memTotal < 0.85 ? 'OK' : 'HIGH' });
      }
      rows.push({ n: 'Phone link', d: (S.linkDetail || '') + (S.rtt != null ? ', ' + S.rtt + ' ms round trip' : ''), ok: true });
    }
    var inDb = h ? h['in'].db : null;
    var frac = inDb == null ? 0 : Math.max(0, Math.min(1, (inDb + 60) / 60));
    var inCol = inDb == null || inDb < -50 ? '#8A847A' : inDb > -1 ? o : g;
    return '<section class="screen" style="gap:14px">' + header('Rig', { id: 'play', label: 'Play' }, head) +
      (S.demo ? '<section class="card" style="gap:8px;border:2px solid #F0C27B"><span style="font-size:17px;font-weight:700">Demo</span><span class="small">A pretend Keystone in this phone. Switches, songs, banks, modes, the tuner and meters all work; recording, updates and captures need the real rig. It hands over to the real Keystone the moment it\'s found.</span><button type="button" class="wide" data-act="demoOff">Leave the demo</button></section>' : '') +
      hevCard(rows, c) +
      '<section class="card" style="gap:10px"><h2 class="label">Bass in</h2><div class="bar" style="height:14px"><div style="width:' + (frac * 100) + '%;background:' + inCol + '"></div></div>' +
      '<div style="display:flex;justify-content:space-between;font-size:14px"><span>' + esc(inputText()) + '</span><span class="muted">' + (h ? 'Input ' + h['in'].ch : '') + '</span></div></section>' +
      '<section class="card" style="padding:6px 16px;gap:0">' + rows.map(function (r) {
        var col = r.bad ? '#E5534B' : r.ok ? g : o;
        return '<div style="display:flex;align-items:center;gap:12px;min-height:54px;border-bottom:1px solid #262626"><span style="width:10px;height:10px;border-radius:50%;background:' + col + ';flex:none"></span>' +
          '<span style="display:flex;flex-direction:column;flex:1;min-width:0"><span style="font-size:17px;font-weight:500">' + esc(r.n) + '</span><span class="small" style="font-size:13px">' + esc(r.d) + '</span></span>' +
          '<span style="font-size:13px;font-weight:700;color:' + col + '">' + (r.st || (r.ok ? 'OK' : 'CHECK')) + '</span></div>';
      }).join('') + '</section>' +
      (Math.round(val('output.mode')) === 2 ? '<button type="button" class="wide" data-act="asioPanel">ASIO panel (opens on the laptop)</button>' : '') +
      bufferCard() +
      '<div class="pair"><button type="button" class="wide" data-act="restartAudio">Restart audio</button><button type="button" class="wide" data-act="light">' + (Math.round(val('output.quality')) === 2 ? 'Effects full' : 'Effects light') + '</button></div>' +
      '<section class="card"><h2 class="label">Kaleidoscope</h2>' + gigLockRow() + '<span class="small">Fourteen pedal plugins to mess about with, one at a time. Tap the name on Play to get there. Gig lock keeps it shut and never costs the band rig anything.</span></section>' +
      wifiCard() +
      updateCard() + watchCards() +
      '<button type="button" class="wide" data-go="recorder">Recorder' + (rec().on ? ': recording ' + clock(rec().s) : on('rec.on') ? ': ready' : ': off') + '</button>' +
      '<div class="small">If something goes wrong on stage, a warning strip appears across the top of Play. Tap it to come here.</div>' +
      nav('health') + '</section>';
  }
  function nav(active) {
    function b(id, label) { return '<button type="button" data-go="' + id + '" class="' + (active === id ? 'on' : '') + '">' + label + '</button>'; }
    // Rig: health, the recorder, updates and Keystone's Wi-Fi, with a light for how it's going
    var r = rec(), dot = r.on ? '#E5484D' : !connected() || warnings().length ? '#F0997B' : '#5DCAA5';
    var rig = ['health', 'recorder', 'take', 'listen'].indexOf(active) >= 0;
    return '<nav class="nav">' + b('play', kalOn() ? 'Pedals' : 'Play') + b('tuner', 'Tuner') + b('tone', 'Tone') +
      '<button type="button" data-go="health" class="rigbtn' + (rig ? ' on' : '') + '"><span class="rdot2' + (r.on ? ' rec' : '') + '" style="background:' + dot + '"></span>Rig</button></nav>';
  }
  function header(title, backTo, right) {
    return '<div class="top">' + (backTo ? '<button type="button" class="back" data-go="' + backTo.id + '">' + ICON.back + backTo.label + '</button>' : '') +
      '<span class="title">' + esc(title) + '</span>' + (right || '<span></span>') + '</div>';
  }

  // ---------------------------------------------------------------- banks and parts
  var BANKS = [
    { name: 'Grit', col: '#F0997B', pedal: 'Dirt blend', zones: ['Clean', 'Drive', 'Bitrot'] },
    { name: 'Groove', col: '#B38CF0', pedal: 'Volume', zones: ['Heel', '', 'Toe'] },
    { name: 'Glow', col: '#6FA8FF', pedal: 'Bloom', zones: ['Speckle', '', 'Wash'] }];
  var JOBS = ['Dirt blend', 'Wah', 'Volume', 'Bloom', 'Bitrot'];
  // Bitrot's pedal configurations (Pedal does), heel and toe
  var BIT_PEDAL = [null,
    { name: 'Rev', zones: ['Song bend', '', '3 oct up'] },
    { name: 'Sweep', zones: ['Closed', '', 'Open'] },
    { name: 'Rev and sweep', zones: ['Low, closed', '', 'High, open'] },
    { name: 'Corrupt', zones: ['Tight', '', 'Wild'] },
    { name: 'Morph', zones: ['Sub', 'Square', 'Oscillator'] }];
  function isPart(i) { return !!S.parts[i] && i > 0; }
  function songNo(i) { var n = 0; for (var k = 0; k <= i && k < S.songs.length; k++) if (!isPart(k)) n++; return n; }
  function songsOnly() { return songNo(S.songs.length - 1); }
  function headOf(i) { while (i > 0 && isPart(i)) i--; return i; }
  // what taps A to C do in each bank, and their state now
  function bankTiles(b) {
    var lit = function (id) { return on(id) ? 'ON' : 'OFF'; };
    if (b === 1) return [
      { k: 'A', name: 'Filter', st: lit('filter.on'), id: 'filter.on', mod: 'filter' },
      { k: 'B', name: 'Dimension', st: lit('dim.on'), id: 'dim.on', mod: 'dim' },
      { k: 'C', name: 'Wah', st: lit('wah.on'), id: 'wah.on', mod: 'wah' }];
    if (b === 2) return [
      { k: 'A', name: 'Speckle', st: lit('grains.on'), id: 'grains.on', mod: 'grains' },
      { k: 'B', name: 'Delay', st: lit('delay.on'), id: 'delay.on', mod: 'delay' },
      { k: 'C', name: 'Reverb', st: on('reverb.on') ? fmt('reverb.type').toUpperCase() : 'OFF', id: 'reverb.on', mod: 'reverb' }];
    if (b === 3) return [
      { k: 'A', name: 'Mute', st: lit('kuddle.muteOn'), id: 'kuddle.muteOn', mod: 'k.mute' },
      { k: 'B', name: 'Auto wah', st: lit('kuddle.wahOn'), id: 'kuddle.wahOn', mod: 'k.wah' },
      { k: 'C', name: 'Room', st: lit('kuddle.roomOn'), id: 'kuddle.roomOn', mod: 'k.room' },
      { k: 'D', name: 'Tape push', st: lit('kuddle.tapeOn'), id: 'kuddle.tapeOn', mod: 'k.tape' }];
    var voice = Math.round(val('drive.voice')), driveOn = on('drive.on');
    return [
      { k: 'A', name: 'Drive', st: !driveOn ? 'OFF' : voice ? 'DRIVE' : 'EDGE', col: !driveOn ? '#8A847A' : voice ? '#F0997B' : '#5DCAA5', border: !driveOn ? '#262626' : voice ? '#F0997B' : '#5DCAA5', id: 'drive.on', mod: 'drive' },
      { k: 'B', name: 'Bitrot', st: lit('bitrot.on'), id: 'bitrot.on', mod: 'bitrot' },
      { k: 'C', name: 'Growl/Cut', st: on('preamp.flip') ? 'FLIPPED' : 'OFF', id: 'preamp.flip', mod: 'preamp' }];
  }
  function fmtBpm() { var b = val('tempo.bpm'); return (Math.round(b * 10) / 10) + '<span class="unit"> BPM</span>'; }
  // a dot that blinks on the beat, so you can check the tempo against the drummer
  var beatTimer = null;
  function beatLoop() {
    clearTimeout(beatTimer);
    var dot = document.getElementById('beat');
    if (dot) { dot.classList.add('on'); setTimeout(function () { dot.classList.remove('on'); }, 90); }
    beatTimer = setTimeout(beatLoop, 60000 / Math.max(40, val('tempo.bpm') || 120));
  }
  beatLoop();
  function pedalInfo() {
    if (kud()) {
      var mv = S.pedalDrag != null ? S.pedalDrag : (S.meter.melt || 0);
      return { job: 4, name: 'Melt', zones: ['Open', 'Warm', 'Melted'], v: mv, text: Math.round(mv * 100) + '%' };
    }
    var bitCfg = on('bitrot.on') ? BIT_PEDAL[Math.round(val('bitrot.pedal'))] : null;
    var m = S.meter, job = m.job != null ? m.job : [bitCfg ? 4 : 0, on('wah.on') ? 1 : 2, 3][S.bank];
    if (job === 4 && !bitCfg) bitCfg = BIT_PEDAL[1];
    var zones = job === 0 ? BANKS[0].zones : job === 3 ? BANKS[2].zones : job === 4 ? bitCfg.zones : ['Heel', '', 'Toe'];
    var v = S.pedalDrag != null ? S.pedalDrag : (m.jobv != null ? m.jobv : job === 3 ? (m.bloom || 0) : 1);
    var text = !m.jobOn && job !== 2 ? 'Saved sound, move the pedal' : m.wait ? 'Waiting: pedal at ' + Math.round((m.pedal || 0) * 100) + '%' : Math.round(v * 100) + '%';
    return { job: job, name: job === 4 ? 'Bitrot: ' + bitCfg.name : JOBS[job], zones: zones, v: v, text: text };
  }
  // Stomp or Song mode, shown above the banks; tap to swap (C and D together on the floor)
  function modeBar() {
    var song = S.footMode === 1;
    return '<button type="button" class="mode' + (song ? ' song' : '') + '" data-act="footMode">' +
      '<span class="mlab">' + (song ? 'SONG MODE' : 'STOMP MODE') + '</span>' +
      '<span class="small">' + (song ? 'A last song · B next part · C next song' : 'C and D together for Song mode') + '</span></button>';
  }
  function bankBar() {
    return '<div class="banks">' + BANKS.map(function (b, i) {
      var cur = i === S.bank;
      return '<button type="button" data-bank="' + i + '" style="' + (cur ? 'background:' + b.col + ';color:#0E0E10;font-weight:700' : 'color:' + b.col) + '">' + b.name.toUpperCase() + '</button>';
    }).join('') + '</div>';
  }

  // ---------------------------------------------------------------- screens
  // Save as my default: what a new song starts with, and what Reset goes back to
  function defaultsCard(mod, title) {
    var has = S.schema && S.schema.params.some(function (p) { return p.module === mod && !p.global && !p.hidden && !/\.on$/.test(p.id); });
    if (!has || mod === 'tempo' || mod === 'recorder' || mod === 'footswitch' || mod === 'output') return '';
    var a = S.defAck && S.defAck.module === mod ? S.defAck : null, after = '';
    if (a && a.op === 'save' && a.ok) after = a.stale > 0
      ? '<div class="defnote"><span style="font-size:16px;font-weight:700">Saved. ' + a.stale + ' song' + (a.stale === 1 ? '' : 's') + ' still use' + (a.stale === 1 ? 's' : '') + ' the old ' + esc(title) + ' default.</span>' +
        '<span class="small">Update them too? Songs where you changed ' + esc(title) + ' keep their own settings.</span>' +
        '<div class="pair"><button type="button" class="wide go" data-def="apply">Update those ' + a.stale + '</button><button type="button" class="wide" data-def="done">Just new songs</button></div></div>'
      : '<div class="defnote"><span class="small">Saved. New songs start with it.</span></div>';
    else if (a && a.op === 'apply' && a.ok) after = '<div class="defnote"><span class="small">Done: ' + a.moved + ' song' + (a.moved === 1 ? '' : 's') + ' updated.</span></div>';
    else if (a && a.op === 'reset' && a.ok) after = '<div class="defnote"><span class="small">Back to your default. Save the song to keep it, or Revert to undo.</span></div>';
    else if (a && !a.ok) after = '<div class="defnote"><span class="small">' + esc(a.why || 'That didn\'t work') + '</span></div>';
    return '<section class="card" style="gap:10px"><h2 class="label">Your default for ' + esc(title) + '</h2>' +
      '<span class="small">What a new song starts with, and what Reset goes back to. Switching it on or off isn\'t part of it.</span>' +
      '<div class="pair"><button type="button" class="wide go" data-def="save">Save as my default</button><button type="button" class="wide" data-def="reset">Reset to my default</button></div>' + after + '</section>';
  }

  // which mode the footswitches are in, big and first, so it's never in doubt
  function modePanel() {
    var song = S.footMode === 1, col = song ? '#4B57FF' : BANKS[S.bank].col;
    var keys = [['A', 'LAST SONG'], ['B', 'NEXT PART'], ['C', 'NEXT SONG'], ['D', 'DRONE']];
    return '<div class="modep" style="border-color:' + col + '"><div class="mhead"><span style="display:flex;flex-direction:column;min-width:0">' +
      '<span class="mk">YOUR FOOTSWITCHES NOW</span><span class="mname" style="color:' + col + '">' + (song ? 'SONG MODE' : 'STOMP MODE') + '</span></span>' +
      '<button type="button" class="mswap" data-act="footMode" aria-label="Swap to ' + (song ? 'Stomp' : 'Song') + ' mode">SWAP<span>C + D</span></button></div>' +
      (song ? '<div class="mkeys">' + keys.map(function (k) {
          var d = k[0] === 'D';
          return '<div class="mkey" style="' + (d ? '' : 'background:' + col + ';color:#0E0E10') + '"><span class="l">' + k[0] + '</span><span class="w">' + k[1] + '</span></div>';
        }).join('') + '</div><span class="small">Each press acts straight away, on the beat. The effects below switch from the phone only.</span>'
        : '<span class="small">Tap A, B, C for the effects below, D for the drone. Hold A for the next bank. Hold C: ' + ['tuner', 'vinyl brake', 'runaway'][S.bank] + '.</span>') + '</div>';
  }

  function playScreen() {
    var n = songsOnly(), i = S.current, head = headOf(i), K = kud();
    var song = S.songs[head] || 'No songs yet', part = isPart(i) ? S.songs[i] : '';
    var layers = S.meter.drone || 0;
    var tiles = K ? bankTiles(3) : bankTiles(S.bank).concat([{ k: 'D', name: 'Drone', st: layers ? 'HOLDING' : 'READY', mod: 'drone', drone: true }]);
    var bc = K ? '#F28C28' : BANKS[S.bank].col, offCol = K ? '#8A6A50' : '#8A847A', offBorder = K ? '#4A3426' : '#262626', pi = pedalInfo();
    // tap a tile for its page; the small button in the corner switches it
    var stomps = tiles.map(function (t) {
      var lit = t.st !== 'OFF' && t.st !== 'READY';
      var hi = t.drone ? '#5DCAA5' : bc;
      var col = t.col || (lit ? hi : offCol), border = t.border || (lit ? hi : offBorder);
      var tog = t.drone ? '<button type="button" class="tog' + (lit ? ' lit' : '') + '" data-stomp="D" aria-label="Hold or let go of the drone" style="' + (lit ? 'background:' + hi + ';border-color:' + hi : '') + '">' + (lit ? 'LET GO' : 'HOLD') + '</button>'
        : '<button type="button" class="tog' + (on(t.id) ? ' lit' : '') + '" data-onoff="' + t.id + '" aria-label="' + esc(t.name) + ' on or off" style="' + (on(t.id) ? 'background:' + bc + ';border-color:' + bc : '') + '">' + (on(t.id) ? 'ON' : 'OFF') + '</button>';
      return '<div class="stomp" role="button" tabindex="0" data-module="' + t.mod + '" style="border-color:' + border + '">' +
        // in Song mode A to C change songs, so only D keeps its letter
        '<span class="head">' + (!K && S.footMode === 1 && t.k !== 'D' ? '' : flip(t.k)) + '<span class="nm">' + t.name + '</span></span>' +
        '<span class="st" style="color:' + col + '">' + t.st + '</span>' + tog + '</div>';
    }).join('');
    var offline = S.link !== 'connected' ? '<div class="card"><span style="display:flex;align-items:center;gap:14px">' + hev('asleep', 64, 'Heavy for his Size, asleep') + '<span style="font-size:17px;font-weight:700">Looking for Keystone</span></span>' +
      '<span class="small">Switch Keystone on and join its Keystone Wi-Fi from this phone\'s Wi-Fi settings. Hev wakes up and this screen connects within a few seconds. No internet or mobile data needed.</span>' +
      (S.linkDetail ? '<span class="small" style="color:#F2EDE4">' + esc(S.linkDetail) + '</span>' : '') +
      '<div class="note"><input id="manualIp" inputmode="decimal" placeholder="Keystone\'s address, e.g. 10.42.0.1" aria-label="Keystone\'s address" value="' + esc(S.manualIp || '') + '">' +
      '<button type="button" data-act="manual">Connect</button></div>' +
      (window.KSDemo ? '<button type="button" class="wide go" data-act="demoOn">Try the demo instead</button><span class="small">Plays with a pretend Keystone in this phone: every screen works, nothing is sent anywhere.</span>' : '') +
      '<span class="small">Type Keystone\'s address, not the phone\'s. On Keystone\'s own Wi-Fi it\'s 10.42.0.1.</span></div>' : '';
    var note = Date.now() < S.noteUntil ? '<div class="note"><input id="noteText" maxlength="200" placeholder="Add a note to that Keep (optional)" aria-label="Note for the last Keep"><button type="button" data-act="note">Save</button></div>' : '';
    return '<section class="screen">' +
      '<div class="top"><button type="button" class="brand rig" data-act="rig" aria-label="Switch to ' + rigNext().name + '">' + (K ? PEBBLE_K : PEBBLE) +
      '<span style="display:flex;flex-direction:column;align-items:flex-start;gap:3px"><span class="title">' + (K ? 'Kuddle' : 'Keystone') + '</span><span class="swap">TAP FOR ' + rigNext().name.toUpperCase() + '</span></span></button><span class="tops">' + recPill() + pill() + '</span></div>' +
      offline +
      '<div class="songbar"><button type="button" class="arrow" data-act="prev" aria-label="Previous song">' + ICON.left + '</button>' +
      '<button type="button" class="song" data-go="setlist" aria-label="Open the setlist">' + flip(n ? two(songNo(i)) : '--', 'blue') +
      '<span style="display:flex;flex-direction:column;min-width:0"><span class="name">' + esc(song) + (part ? '<span class="partname">, ' + esc(part) + '</span>' : '') + '</span><span class="pos">' + (K ? 'Preset ' : 'Song ') + songNo(i) + ' of ' + n +
      (S.edited ? ', <span class="edited">edited</span>' : K ? '. Hold B for next.' : S.footMode === 1 ? '. C for next song.' : '. Hold B for next.') + '</span></span></button>' +
      '<button type="button" class="arrow" data-act="next" aria-label="Next song">' + ICON.right + '</button></div>' +
      saveBar() +
      (K ? '' : modePanel()) +
      '<div class="card meter">' + meterSvg() + '<div class="label">Output</div></div>' +
      (K ? '' : S.footMode === 1 ? '<span class="label">Effects in this song (phone only)</span>' : bankBar()) +
      '<div class="stomps' + (!K && S.footMode === 1 ? ' phoneonly' : '') + '">' + stomps + '</div>' + note +
      (K ? '' : '<div class="card tempo"><span class="tlab">TEMPO</span><span class="tbpm" id="tbpm">' + fmtBpm() + '</span><span class="beat" id="beat"></span>' +
        '<button type="button" class="tap" data-act="tapTempo">TAP</button></div>') +
      '<div class="card pedal" id="pedal"><div class="label" style="display:flex;justify-content:space-between"><span id="pedalName">Pedal: ' + pi.name + '</span><span id="pedalPct">' + pi.text + '</span></div>' +
      '<div class="bar"><div id="pedalBar" style="width:' + (pi.v * 100) + '%;background:' + (K ? 'linear-gradient(90deg,#F28C28,#8A3F0A)' : bc) + '"></div></div>' +
      '<div class="zones">' + pi.zones.map(function (z) { return '<span>' + z + '</span>'; }).join('') + '</div>' +
      '<span class="small">' + (K ? 'Hold A or B for the last or next preset. Hold C: tuner. Hold D: squeeze. Hold C and D: Keep.' : S.footMode === 1 ? 'The pedal keeps its job from Stomp mode. C and D together to go back.' : 'Hold A for the next bank. Changing bank never changes the sound.') + '</span></div>' +
      nav('play') + '</section>';
  }

  // needle angle from dB, matching the canvas scale: -20 left, -10, -5 top, 0, +3 right
  function dbAngle(db) {
    // -40 at the left, -20, -12 at the top, -6, 0 into the red: playing level sits mid-scale
    var pts = [[-60, 180], [-40, 180], [-30, 155], [-20, 122], [-12, 90], [-6, 58], [0, 25], [3, 8]];
    if (db <= -40) return 180;
    for (var i = 1; i < pts.length; i++) if (db <= pts[i][0]) {
      var a = pts[i - 1], b = pts[i], t = (db - a[0]) / (b[0] - a[0]);
      return a[1] + (b[1] - a[1]) * t;
    }
    return 8;
  }
  function needle(angle) {
    var r = angle * Math.PI / 180;
    return { x: 155 + 110 * Math.cos(r), y: 140 - 110 * Math.sin(r) };
  }
  function meterSvg() {
    var p = needle(dbAngle(S.meter.out != null ? S.meter.out : -60));
    return '<svg width="310" height="150" viewBox="0 -14 310 164" role="img" aria-label="Output level meter">' +
      '<path d="M35 140 A 120 120 0 0 1 263.8 89.3" fill="none" stroke="#F2EDE4" stroke-width="2"/>' +
      '<path d="M263.8 89.3 A 120 120 0 0 1 273.8 123.3" fill="none" stroke="#F0997B" stroke-width="6"/>' +
      '<line x1="35.0" y1="140.0" x2="24.0" y2="140.0" stroke="#F2EDE4" stroke-width="2"/><line x1="91.4" y1="38.2" x2="85.6" y2="28.9" stroke="#F2EDE4" stroke-width="2"/><line x1="155.0" y1="20.0" x2="155.0" y2="9.0" stroke="#F2EDE4" stroke-width="2"/><line x1="218.6" y1="38.2" x2="224.4" y2="28.9" stroke="#F2EDE4" stroke-width="2"/><line x1="263.8" y1="89.3" x2="273.7" y2="84.6" stroke="#F0997B" stroke-width="2"/>' +
      '<text x="10.0" y="144.0" font-size="13" fill="#B9B2A6" text-anchor="middle">-40</text><text x="78.2" y="21.0" font-size="13" fill="#B9B2A6" text-anchor="middle">-20</text><text x="155.0" y="-1.0" font-size="13" fill="#B9B2A6" text-anchor="middle">-12</text><text x="231.8" y="21.0" font-size="13" fill="#B9B2A6" text-anchor="middle">-6</text><text x="286.4" y="82.7" font-size="13" fill="#F0997B" text-anchor="middle">0</text>' +
      '<line id="needle" x1="155" y1="140" x2="' + p.x.toFixed(1) + '" y2="' + p.y.toFixed(1) + '" stroke="#F2EDE4" stroke-width="3" stroke-linecap="round"/>' +
      '<circle cx="155" cy="140" r="9" style="fill:var(--blue)"/></svg>';
  }

  function tunerView() {
    var t = S.meter.tuner || {};
    var valid = !!t.valid, cents = valid ? t.cents : 0, inTune = valid && Math.abs(cents) <= 2;
    return {
      note: valid ? t.note.replace('#', '\u266F') : '-', oct: valid ? String(t.oct) : ' ',
      p: needle(90 - Math.max(-50, Math.min(50, cents)) * 1.8), col: inTune ? '#5DCAA5' : '#F2EDE4',
      cents: valid ? (cents > 0 ? '+' : '') + Math.round(cents) + ' cents' : '--',
      msg: !valid ? 'Play a string' : inTune ? 'In tune' : Math.abs(cents) < 10 ? 'Nearly there. Green when within 2 cents.' : cents < 0 ? 'Flat: tune up' : 'Sharp: tune down'
    };
  }
  // Hev's eyes tune, as Pebble's LED eyes do on the pedal: left red when flat, right red when
  // sharp, both green in tune (0.5.9)
  function hevEyes(t) {
    var valid = t && t.valid, c = valid ? t.cents : 0;
    if (!valid) return ['#FFFFFF', '#FFFFFF'];
    if (Math.abs(c) <= 2) return ['#5DCAA5', '#5DCAA5'];
    return c < 0 ? ['#E5534B', '#FFFFFF'] : ['#FFFFFF', '#E5534B'];
  }
  function tunerHev(t) {
    var e = hevEyes(t);
    return '<svg width="56" height="56" viewBox="0 0 200 200" role="img" aria-label="Heavy for his Size, his eyes show flat and sharp">' +
      '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#2329A8"/>' +
      '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#4B57FF"/>' +
      '<circle id="hevL" cx="78" cy="80" r="18" fill="' + e[0] + '"/><circle cx="80" cy="82" r="9" fill="#0B0B12"/>' +
      '<circle id="hevR" cx="122" cy="80" r="18" fill="' + e[1] + '"/><circle cx="124" cy="82" r="9" fill="#0B0B12"/>' +
      '<path d="M84 114 Q 100 132, 116 114" fill="none" stroke="#0B0B12" stroke-width="6" stroke-linecap="round"/></svg>';
  }
  function updateTuner() {
    var v = tunerView(), q = function (id) { return document.getElementById(id); };
    if (!q('tnote')) return;
    var e = hevEyes(S.meter.tuner);
    if (q('hevL')) { q('hevL').setAttribute('fill', e[0]); q('hevR').setAttribute('fill', e[1]); }
    if (q('tnote').textContent !== v.note) { q('tnote').textContent = v.note; q('toct').textContent = v.oct; }
    q('tneedle').setAttribute('x2', v.p.x.toFixed(1)); q('tneedle').setAttribute('y2', v.p.y.toFixed(1)); q('tneedle').setAttribute('stroke', v.col);
    q('tcents').textContent = v.cents; q('tcents').style.color = v.col;
    q('tmsg').textContent = v.msg;
    var il = q('inLevel'); if (il) il.textContent = inputText();
  }

  function tunerScreen() {
    var t = S.meter.tuner || {};
    var valid = t.valid, cents = valid ? t.cents : 0;
    var inTune = valid && Math.abs(cents) <= 2;
    var p = needle(90 - Math.max(-50, Math.min(50, cents)) * 1.8);
    var drop = Math.round(val('drone.tuning')) === 1;
    var msg = !valid ? 'Play a string' : inTune ? 'In tune' : Math.abs(cents) < 10 ? 'Nearly there. Green when within 2 cents.' : cents < 0 ? 'Flat: tune up' : 'Sharp: tune down';
    var muted = on('tuner.mute');
    return '<section class="screen" style="gap:20px">' +
      '<div class="top"><span class="brand">' + tunerHev(t) + '<span class="title">Tuner</span></span><div class="pill"><span class="dot" style="background:' + (muted ? '#F0997B' : '#5DCAA5') + '"></span>' + (muted ? 'Output muted' : 'Output live') + '</div></div>' +
      '<div class="tnote"><span class="flip big" id="tnote">' + esc(valid ? t.note.replace('#', '\u266F') : '-') + '</span><span class="flip oct" id="toct">' + esc(valid ? t.oct : ' ') + '</span></div>' +
      '<div class="card" style="align-items:center;gap:4px;padding:16px"><svg width="310" height="150" viewBox="0 -14 310 164" role="img" aria-label="Tuning needle">' +
      '<path d="M35 140 A 120 120 0 0 1 275 140" fill="none" stroke="#3A3A40" stroke-width="2"/>' +
      '<path d="M135 22 A 120 120 0 0 1 175 22" fill="none" stroke="#5DCAA5" stroke-width="8"/><line x1="155" y1="20" x2="155" y2="4" stroke="#5DCAA5" stroke-width="3"/>' +
      '<line x1="35" y1="140" x2="23" y2="140" stroke="#F0997B" stroke-width="2"/><line x1="275" y1="140" x2="287" y2="140" stroke="#F0997B" stroke-width="2"/>' +
      '<line x1="70" y1="55" x2="62" y2="47" stroke="#8A847A" stroke-width="2"/><line x1="240" y1="55" x2="248" y2="47" stroke="#8A847A" stroke-width="2"/>' +
      '<text x="30" y="126" font-size="13" fill="#F0997B" text-anchor="middle">Flat</text><text x="280" y="126" font-size="13" fill="#F0997B" text-anchor="middle">Sharp</text>' +
      '<line id="tneedle" x1="155" y1="140" x2="' + p.x.toFixed(1) + '" y2="' + p.y.toFixed(1) + '" stroke="' + (inTune ? '#5DCAA5' : '#F2EDE4') + '" stroke-width="3" stroke-linecap="round"/>' +
      '<circle cx="155" cy="140" r="9" style="fill:var(--blue)"/></svg>' +
      '<div class="cents" id="tcents" style="color:' + (inTune ? '#5DCAA5' : '#F2EDE4') + '">' + (valid ? (cents > 0 ? '+' : '') + Math.round(cents) + ' cents' : '--') + '</div>' +
      '<div class="small" id="tmsg">' + msg + '</div></div>' +
      '<div style="display:flex;flex-direction:column;gap:10px"><div class="label">Tuning</div><div class="seg" style="grid-template-columns:repeat(2,minmax(0,1fr))">' +
      '<button type="button" class="' + (drop ? '' : 'on') + '" data-set="drone.tuning" data-v="0" style="min-height:52px;font-size:17px">Standard E</button>' +
      '<button type="button" class="' + (drop ? 'on' : '') + '" data-set="drone.tuning" data-v="1" style="min-height:52px;font-size:17px">Drop D</button></div></div>' +
      '<label class="switch">Mute output while tuning<input type="checkbox" data-toggle="tuner.mute"' + (muted ? ' checked' : '') + '></label>' +
      '<div class="small" id="inLevel">' + inputText() + '</div>' +
      '<div class="small">Reference A = ' + Math.round(val('tuner.ref')) + ' Hz. Tap any footswitch to leave the tuner (in Grit, hold C again).</div>' +
      nav('tuner') + '</section>';
  }

  // what the bass input is doing, in words
  function inputText() {
    if (S.health && S.health['in'].dead) return 'Input: no signal at all. Check Windows microphone access.';
    var d = S.meter.in;
    if (d == null) return 'Input: waiting for Keystone';
    if (d < -70) return 'Input: silent. Check the cable, the input gain and Windows microphone access.';
    if (d < -40) return 'Input: very quiet (' + Math.round(d) + ' dB). Turn the input gain up.';
    if (d > -1) return 'Input: clipping (' + Math.round(d) + ' dB). Turn the input gain down.';
    return 'Input: ' + Math.round(d) + ' dB, good';
  }

  function moduleState(m) {
    var id = m.id;
    if (id === 'drone') return { s: (S.meter.drone ? 'HOLDING' : on('drone.on') ? 'READY' : 'OFF') };
    if (id === 'drive') return { s: !on('drive.on') ? 'OFF' : Math.round(val('drive.voice')) ? 'DRIVE' : 'EDGE' };
    if (id === 'bloom') return { s: 'GLOW' };
    if (id === 'pedal') return { s: BANKS[S.bank].name.toUpperCase() };
    if (id === 'footswitches') return { s: 'A to D' };
    if (id === 'output') return { s: on('output.limit') ? 'ON' : 'NO LIMIT' };
    if (id === 'split') return { s: 'ON' };
    var op = onp(id);
    if (id === 'k.di' || id === 'k.input' || id === 'k.output') return { s: 'ON' };
    if (id === 'k.melt') return { s: 'EXP' };
    return { s: op ? (on(op) ? 'ON' : 'OFF') : '' };
  }
  function summary(m) {
    switch (m.id) {
      case 'split': return 'Bottom end under ' + fmt('split.freq') + ' Hz, full sound above ' + fmt('split.subCut') + ' Hz';
      case 'lowcomp': return on('lowcomp.slap') ? 'Slap mode on' : 'Light, slap mode ready';
      case 'drive': {
        var e = Math.round(val('drive.edge.source')) === 0 && S.capture ? 'capture' : 'model';
        var d = Math.round(val('drive.source')) === 0 && S.capture ? 'capture' : 'model';
        return 'Edge on the ' + e + ', Drive on the ' + d;
      }
      case 'dim': return 'Mode ' + fmt('dim.mode') + ' on the ' + (Math.round(val('dim.path')) ? 'full sound' : 'bottom end');
      case 'grains': return fmt('grains.effect') + ' ' + fmt('grains.variant') + ', ' + fmt('grains.subdiv') + (Math.round(val('grains.pitch')) ? ', ' + fmt('grains.pitch') + ' up' : '');
      case 'tempo': return fmt('tempo.bpm') + ', tap on Play';
      case 'delay': return fmt('delay.type') + ', ' + (Math.round(val('delay.sync')) ? fmt('delay.sync') + ' of the tempo' : fmt('delay.time')) + ', ' + fmt('delay.feedback') + ' repeats';
      case 'reverb': return fmt('reverb.type') + (Math.round(val('reverb.type')) === 11 ? (S.space ? ', ' + S.space : ', no space loaded') : Math.round(val('reverb.type')) === 10 ? ', ' + fmt('reverb.length') : ', ' + fmt('reverb.decay'));
      case 'wah': return 'Heel ' + fmt('wah.low') + ' Hz to toe ' + fmt('wah.high') + ' Hz';
      case 'pedal': return 'Now: ' + pedalInfo().name + ' in ' + BANKS[S.bank].name;
      case 'preamp': return 'Valve gain ' + Math.round(val('preamp.gain') * 100) + '%, ' + Math.round(val('preamp.blend') * 100) + '% Cut' + (val('preamp.tape') > 0.01 ? ', tape' : '');
    }
    return m.summary;
  }

  // the modules in the order the sound goes through them, grouped by path
  function routedModules() {
    var by = {}; S.modules.forEach(function (m) { by[m.id] = m; });
    var dimFull = Math.round(val('dim.path')) === 1, place = Math.round(val('bitrot.place'));
    if (kud()) {
      var kl = [['Kuddle', ['k.input', 'k.mute', 'k.wah', 'k.di', 'k.opto', 'k.tape', 'k.tone', 'k.room']], ['Control', ['k.melt', 'footswitches', 'k.output', 'output']]];
      return kl.map(function (pg) { return [pg[0], pg[1].filter(function (id) { return by[id]; }).map(function (id) { return by[id]; })]; });
    }
    var paths = [
      ['Split', ['split']],
      ['Bottom end dry', ['lowcomp']],
      ['Bottom end wet', dimFull ? ['drone'] : ['dim', 'drone']],
      ['Full sound', (place === 0 ? ['bitrot', 'drive', 'preamp'] : place === 1 ? ['drive', 'bitrot', 'preamp'] : ['drive', 'preamp', 'bitrot']).concat(['filter', 'wah'], dimFull ? ['dim'] : [], ['grains', 'delay', 'reverb'])],
      ['Control', ['tempo', 'footswitches', 'pedal', 'bloom', 'output']]];
    var seen = {}, out = [];
    paths.forEach(function (pg) {
      var list = pg[1].filter(function (id) { return by[id]; }).map(function (id) { seen[id] = 1; return by[id]; });
      if (list.length) out.push([pg[0], list]);
    });
    var rest = S.modules.filter(function (m) { return !seen[m.id] && m.id.indexOf('k.') !== 0 && m.id !== 'recorder'; });   // the recorder has its own page   // anything new still shows
    if (rest.length) out[out.length - 1][1] = out[out.length - 1][1].concat(rest);
    return out;
  }
  function toneScreen() {
    var html = routedModules().map(function (pg) {
      return '<section class="group"><h2 class="label">' + esc(pg[0]) + '</h2>' + pg[1].map(function (m) {
        var st = moduleState(m).s, sw = onp(m.id);
        var col = st === 'OFF' || st === 'NO LIMIT' ? '#8A847A' : '#5DCAA5';
        var right = sw ? '<button type="button" class="pill' + (on(sw) ? ' lit' : '') + '" data-onoff="' + sw + '" aria-label="' + esc(m.title) + ' on or off">' + (on(sw) ? 'ON' : 'OFF') + '</button>'
                       : '<span style="color:' + col + '">' + st + '</span>';
        return '<div class="row routed"><button type="button" class="main" data-module="' + m.id + '"><span><span class="t">' + esc(m.title) + '</span><span class="s">' + esc(summary(m)) + '</span></span>' + ICON.chev + '</button>' + right + '</div>';
      }).join('') + '</section>';
    }).join('');
    return '<section class="screen" style="gap:14px"><div class="top"><span class="title">Tone</span><span class="small">Song: ' + esc(S.songs[S.current] || '') + (S.edited ? ' <span class="edited">edited</span>' : '') + '</span></div>' +
      html + nav('tone') + '</section>';
  }

  // ---- generic module page, laid out like the canvas's Drive and Grains pages
  // "delay.type=2", "delay.type=7|11", "grains.effect!=0"
  function visible(p) {
    if (!p.showIf) return true;
    var m = /^([\w.]+)(!?=)(.+)$/.exec(p.showIf);
    if (!m) return true;
    var cur = Math.round(val(m[1])), hit = m[3].split('|').some(function (x) { return +x === cur; });
    return m[2] === '=' ? hit : !hit;
  }
  function paramsOf(mod) {
    var list = [];
    S.schema.params.forEach(function (p) { if (p.module === mod && !p.hidden && visible(p)) list.push(p); });
    return list;
  }

  // one line under a choice, saying what it does
  var DELAY_TYPES = [
    'Digital: clean, exact repeats.',
    'Bucket: warm analogue repeats that darken as the time gets longer.',
    'Tape: a tape echo with three machines. Fixed and Multi heads change tape speed, Single slides the record head.',
    'Dual: two delays, side by side or one into the other.',
    'Pattern: rhythmic taps across the beat.',
    'Reverse: every repeat plays backwards.',
    'Ice: repeats climb in pitch, glassy and bright.',
    'Swell: notes fade in, so only the repeats bloom.',
    'Trem: tremolo on the repeats.',
    'Filter: a slow sweeping filter on the repeats.',
    'Lo-fi: crushed bits and a low sample rate.',
    'Galaxy: swelling, smeared repeats that turn into a wash.'];
  var TAPE_MODES = [
    ['Fixed A: one head.', 'Fixed B: two heads, half and full time.', 'Fixed C: all three heads, an echo machine.'],
    ['Multi A: heads 1 and 2 together.', 'Multi B: heads 2 and 3 together.', 'Multi C: heads 1 and 3 together.'],
    ['Single A: one sliding head.', 'Single B: the sliding head and a half-time head.', 'Single C: the sliding head and a three-quarter head.']];
  var GRAIN_FX = [
    ['normal and double speed loops', 'normal and half speed', 'all double speed', 'half, normal, double and quad'],
    ['filtered, shuffled slices', 'normal and half speed bounce', 'overlapping layers with sweeps', 'crushed layers'],
    ['half to normal', 'double to half', 'normal to double', 'both directions at once'],
    ['a soft cloud of grains drifting out of what you just played', 'many random grains', 'normal and double speed grains', 'normal and half speed grains'],
    ['a drone that stretches and squeezes', 'a drone with sweeps', 'a resonant band-pass drone', 'a drone that tightens after each note'],
    ['the last note, repeated', 'overlapping copies that phase', 'cascading recent notes', 'cascades with double speed grains'],
    ['rearranged with bursts of runs', 'rearranged and pitch shifted', 'soft, filtered glitches', 'pitch shifted and crushed'],
    ['rearranged interruptions', 'pitch shifted', 'filter sweeps and echoes', 'crushed and drastic'],
    ['basic arpeggios of your notes', 'pitched patterns', 'a random filter per note', 'crushed'],
    ['classic, even repeats', 'dotted', 'off-beat', 'triplet bursts'],
    ['an envelope filter on each tap', 'resonant band-pass taps', 'octave and fifth shifted taps', 'taps that fade into grains']];
  var REVERB_TYPES = [
    'Room: small and close, with early reflections off the walls.',
    'Hall: big, smooth and even.',
    'Plate: bright and dense, straight in with no gap.',
    'Spring: an amp-style spring tank, with drip.',
    'Chamber: a dark stone room with strong early reflections.',
    'Cloud: huge and soft, it blurs where one note ends and the next begins.',
    'Bloom: swells up after each note instead of starting straight away.',
    'Chorale: the tail sings, through vowel shapes.',
    'Shimmer: the tail climbs an octave or a fifth.',
    'Magneto: a multi-head tape echo washing into the room.',
    'Nonlinear: a shaped burst, gated, reversed, swooshing or ramped.',
    'Impulse: plays your own .wav spaces from Documents\\Keystone\\Spaces.'];
  function describe(id) {
    if (id === 'reverb.type') return REVERB_TYPES[Math.round(val(id))] || '';
    if (id === 'delay.type') return DELAY_TYPES[Math.round(val(id))] || '';
    if (id === 'delay.tapeMode') return (TAPE_MODES[Math.round(val('delay.tapeMachine'))] || [])[Math.round(val(id))] || '';
    if (id === 'grains.variant') {
      var e = Math.round(val('grains.effect')), v = Math.round(val('grains.variant'));
      var name = S.params['grains.effect'].choices[e];
      return name + ' ' + 'ABCD'[v] + ': ' + (GRAIN_FX[e] || [])[v] + '.';
    }
    if (id === 'bitrot.subInt') return 'The sub plays the note divided by this: 2 is an octave down, 3 an octave and a fifth, 4 two octaves.';
    if (id === 'bitrot.oscInt') return 'The oscillator locks onto this harmonic of the note: x2 an octave up, x3 an octave and a fifth, x4 two octaves.';
    if (id === 'bitrot.floor') return 'Lifts any sub that falls below the floor by octaves, so low notes stay felt rather than flapping.';
    if (id === 'bitrot.corrupt') return 'Low keeps the loop tight on the note. High lets it hunt, dive and glitch between notes.';
    if (id === 'bitrot.gate') return 'How quiet a note can be and still play the synth. It follows how hard you play, so hiss never plays it.';
    if (id === 'bitrot.clean') return 'How much of your clean bass stays under the synth, lows included.';
    if (id === 'grains.pitch') return 'Off plays Speckle on your full sound. The others pitch it up first, always in key, to sit above the band.';
    if (id === 'grains.subdiv') return 'Steps of the song\'s tempo, so tap tempo sets Speckle too.';
    if (id === 'grains.filter') return 'Fully up is open. Down darkens the effect, and right down mutes it.';
    if (id === 'grains.space') return 'A dotted echo and a reverb on the effect only. Fully up is all space.';
    if (id === 'preamp.gain') return 'Two valve stages. Low is warm and clean, the middle thickens as you dig in, the top growls. The level stays put.';
    if (id === 'preamp.sag') return 'How much the valve gives under a hard note: more sag compresses and fattens your attack.';
    if (id === 'preamp.bass') return 'A real passive tone stack: the three knobs interact like an amp, so the middle shapes how the bass and treble sit.';
    if (id === 'preamp.ultraLo') return 'Deeper lows and a scooped middle, for big open notes.';
    if (id === 'preamp.ultraHi') return 'Extra top end, for slap and clank.';
    if (id === 'preamp.blend') return 'Valve is warm and round. Cut is the tight, modern voice that keeps you clear in a busy mix. Blend the two.';
    if (id === 'preamp.tape') return 'Tape saturation with its head bump and soft top: rounder attacks and a fuller low end.';
    if (id === 'preamp.wow') return 'The slight drift of a tape machine. Keep it low: felt, not heard.';
    if (id === 'output.iron') return 'An output transformer on everything, bottom end included: thicker lows and softer peaks.';
    if (id === 'split.subCut') return 'The full sound path is your whole bass with just the sub cut, so drive, Speckle and the spaces have body without mud.';
    if (id === 'split.lowLevel') return 'Bottom end dry: the clean low band under everything, below the crossover.';
    if (id === 'split.wetLevel') return 'Bottom end wet: how much of the Dimension chorus and the drone join the bottom end.';
    if (id === 'split.highLevel') return 'Full sound: the drive, Bitrot, preamp, filter, Speckle, delay and reverb path.';
    if (id === 'dim.path') return 'Bottom end widens your low band. Full sound puts it on the drive and effects path instead.';
    return '';
  }
  function renderParams(list) {
    var sections = [], by = {};
    list.forEach(function (p) {
      if (p.section === '') return;   // the On toggle sits in the header
      if (!by[p.section]) { by[p.section] = []; sections.push(p.section); }
      by[p.section].push(p);
    });
    return sections.map(function (sec) {
      var ps = by[sec], body = '';
      var flips = ps.filter(function (p) { return p.kind === 'flip'; });
      ps.forEach(function (p) {
        if (p.kind === 'choice') {
          var cols = p.choices.length > 6 ? 3 : Math.min(4, p.choices.length), cur = Math.round(val(p.id));
          var showLabel = ps.length > 1 && p.label !== sec;
          body += (showLabel ? '<div class="small">' + esc(p.label) + '</div>' : '') +
            '<div class="seg" style="grid-template-columns:repeat(' + cols + ',minmax(0,1fr))">' + p.choices.map(function (c, i) {
              return '<button type="button" class="' + (i === cur ? 'on' : '') + '" data-set="' + p.id + '" data-v="' + i + '">' + esc(c) + '</button>';
            }).join('') + '</div>' + (describe(p.id) ? '<div class="small">' + esc(describe(p.id)) + '</div>' : '');
        } else if (p.kind === 'toggle') {
          body += '<label class="switch" style="background:#262626">' + esc(p.label) + '<input type="checkbox" data-toggle="' + p.id + '"' + (on(p.id) ? ' checked' : '') + '></label>';
        } else if (p.kind === 'slider') {
          var ends = p.choices.length === 2 ? p.choices : ['', ''];
          body += '<label class="slider"><span class="ends"><span>' + esc(ends[0] || p.label) + '</span><span>' + esc(fmt(p.id)) + '</span><span>' + esc(ends[1]) + '</span></span>' +
            '<input type="range" min="0" max="1000" value="' + Math.round(toPos(p, val(p.id)) * 1000) + '" data-range="' + p.id + '" aria-label="' + esc(p.label) + '"></label>';
        }
      });
      if (flips.length) body += '<div class="flips">' + flips.map(function (p) {
        return '<button type="button" class="fbtn' + (S.sel === p.id ? ' sel' : '') + '" data-flip="' + p.id + '">' + flip(fmt(p.id)) + '<span class="lb">' + esc(p.label) + '</span></button>';
      }).join('') + '</div>';
      var title = sec === 'Character' && S.params['reverb.type'] ? fmt('reverb.type') : sec;
      return '<section class="card"><h2 class="label">' + esc(title) + '</h2>' + body + '</section>';
    }).join('');
  }

  function moduleScreen() {
    var mod = S.module, m = S.modules.filter(function (x) { return x.id === mod; })[0] || { title: mod };
    var list = paramsOf(mod), extra = '', pre = '';
    var hasOn = onp(mod);
    var right = hasOn ? '<label class="onToggle">On<input type="checkbox" data-toggle="' + hasOn + '"' + (on(hasOn) ? ' checked' : '') + '></label>' : '<span style="width:44px"></span>';

    if (mod === 'drive') {
      // the voice being edited is the voice playing; its settings show below
      var voice = Math.round(val('drive.voice'));
      var pfx = voice ? 'drive.drive.' : 'drive.edge.';
      list = list.filter(function (p) { return !/^drive\.(edge|drive)\./.test(p.id) || p.id.indexOf(pfx) === 0; });
      var srcId = voice ? 'drive.source' : 'drive.edge.source';   // each voice picks its own source
      var src = Math.round(val(srcId));
      var capName = S.capture ? S.capture : (S.namAvailable ? 'No capture loaded' : 'Captures not supported');
      var capSub = S.capture ? 'NAM, loaded from your captures' : 'Put .nam files in Documents\\Keystone\\Captures';
      // source section: the capture row sits above Capture / Model
      var srcHtml = '<section class="card"><h2 class="label">Source</h2><button type="button" class="source" data-act="captures"><span><span class="t">' + esc(capName) + '</span><span class="s">' + esc(capSub) + '</span></span><span style="font-size:14px;color:#5DCAA5">Change</span></button>' +
        (S.showCaptures ? '<div class="group">' + (S.captures.length ? S.captures.map(function (f) {
          return '<button type="button" class="row" data-capture="' + esc(f) + '" style="min-height:48px;background:#262626"><span class="t" style="font-size:16px">' + esc(f.replace(/\.nam$/i, '')) + '</span><span class="r" style="color:#5DCAA5">' + (f === S.captureFile ? 'LOADED' : '') + '</span></button>';
        }).join('') : '<span class="small">No captures on Keystone yet.</span>') + '</div>' : '') +
        '<div class="seg" style="grid-template-columns:repeat(2,minmax(0,1fr))"><button type="button" class="' + (src === 0 ? 'on' : '') + '" data-set="' + srcId + '" data-v="0">Capture</button><button type="button" class="' + (src === 1 ? 'on' : '') + '" data-set="' + srcId + '" data-v="1">Model</button></div>' +
        '<span class="small">' + (voice ? 'Drive' : 'Edge') + ' plays through ' + (src === 0 ? 'the capture.' : 'the model.') + (src === 0 && !voice ? ' A capture saved at high gain stays heavy, even on Edge.' : '') + '</span></section>';
      var rest = list.filter(function (p) { return p.id !== 'drive.source' && p.id !== 'drive.edge.source'; });
      var html = renderParams(rest);
      // put Source after "Editing voice", as on the canvas
      var cut = html.indexOf('</section>') + '</section>'.length;
      pre = html.slice(0, cut) + srcHtml;
      list = null; extra = html.slice(cut);
    }
    if (mod === 'reverb' && Math.round(val('reverb.type')) === 11) {
      var spName = S.space ? S.space : (S.spaces.length ? 'No space loaded' : 'No spaces yet');
      var spCard = '<section class="card"><h2 class="label">Impulse file</h2><button type="button" class="source" data-act="spaces"><span><span class="t">' + esc(spName) + '</span>' +
        '<span class="s">' + esc(S.space ? '.wav from your Spaces folder' : 'Put .wav files in Documents\\Keystone\\Spaces') + '</span></span><span style="font-size:14px;color:#5DCAA5">Change</span></button>' +
        (S.showSpaces ? '<div class="group">' + (S.spaces.length ? S.spaces.map(function (f) {
          return '<button type="button" class="row" data-space="' + esc(f) + '" style="min-height:48px;background:#262626"><span class="t" style="font-size:16px">' + esc(f.replace(/\.wav$/i, '')) + '</span><span class="r" style="color:#5DCAA5">' + (f === S.spaceFile ? 'LOADED' : '') + '</span></button>';
        }).join('') : '<span class="small">No spaces on Keystone yet.</span>') + '</div>' : '') + '</section>';
      var rvHtml = renderParams(list);
      var rvCut = rvHtml.indexOf('</section>') + '</section>'.length;
      pre = rvHtml.slice(0, rvCut) + spCard;
      list = null; extra = rvHtml.slice(rvCut);
    }
    if (mod === 'pedal') {
      pre = '<section class="card"><h2 class="label">One pedal, a job per bank</h2><span class="small">Grit: dirt blend. Heel is clean, the drive is fully in by halfway and Bitrot joins from Bitrot joins at up to the toe. It only blends dirt you have switched on.</span>' +
        '<span class="small">With Bitrot on and its Pedal does set to anything but Blend, the pedal in Grit works Bitrot alone: Rev, Sweep, Rev and sweep, Corrupt or Morph. Bitrot plays at full level and the drive stays where it was.</span>' +
        '<span class="small">Groove: with the wah on (tap C) the pedal sweeps it, on the full sound so the bottom end stays solid. With the wah off it is a volume pedal before the delay and reverb, so tails ring as you pull back.</span>' +
        '<span class="small">Glow: Bloom, see Bloom sweep.</span>' +
        '<span class="small">After a bank change the pedal waits until it passes the value its job was left at, then follows, so nothing jumps. Volume starts full and always waits. A new song or part puts every job back to its saved sound until you move the pedal.</span></section>';
    }
    if (mod === 'bloom') {
      var bl = S.meter.bloom || 0;
      pre = '<section class="card"><h2 class="label">Sweep now</h2><div class="bar"><div id="pedalBar" style="width:' + bl * 100 + '%"></div></div>' +
        '<div class="zones"><span>Speckle</span><span>Wash</span></div><span class="small">The pedal only pushes effects you have switched on. Heel to halfway: Speckle comes further in. Halfway to toe: delay and reverb climb towards self-oscillation, held back by the feedback limiter. The drone is not on the pedal: tap D to hold a note, tap D again to let it trail off.</span></section>';
    }
    if (mod === 'drone') {
      pre = '<section class="card"><h2 class="label">Held notes</h2><div class="pair"><button type="button" class="wide" data-stomp="D">Capture</button><button type="button" class="wide" data-act="droneClear">Clear</button></div>' +
        '<span class="small">Tap D to hold the note you are playing. Tap D again and it trails off over the Trail off time.</span></section>';
    }
    if (mod === 'delay') {
      pre = '<section class="card"><h2 class="label">Tap tempo</h2><button type="button" class="wide" data-act="tap">Tap</button></section>';
    }
    if (mod === 'footswitches' && kud()) {
      list = [];
      pre = '<section class="card"><h2 class="label">Kuddle: tap and hold</h2>' + [['A', 'Mute', 'Last preset'], ['B', 'Auto wah', 'Next preset'], ['C', 'Room', 'Tuner'], ['D', 'Tape push', 'Squeeze while held']].map(function (j) {
        return '<div style="display:flex;align-items:center;gap:12px">' + flip(j[0]).replace('class="flip "', 'class="flip" style="width:36px;height:42px;font-size:22px"') +
          '<span style="flex:1"><span style="display:block;font-size:16px">Tap: ' + j[1] + '</span><span class="small">Hold: ' + j[2] + '</span></span></div>';
      }).join('') + '<div class="small">Hold A and B for a second: back to Keystone. Hold C and D: Keep. Learn and pedal calibration are shared with Keystone: switch to Keystone to change them.</div></section>';
    }
    else if (mod === 'footswitches') {
      var jobs = [['A', 'Grit: Edge / Drive. Groove: auto filter. Glow: Speckle', 'Next bank. Song mode: last song, on the press'], ['B', 'Grit: Bitrot. Groove: Dimension. Glow: delay', 'Next song or part. Song mode: next part, on the press'],
                  ['C', 'Grit: Growl/Cut flip. Groove: wah. Glow: reverb', 'Grit: tuner. Groove: vinyl brake. Glow: runaway. Song mode: next song, on the press'], ['D', 'Drone hold / trail off, in every bank', 'Slap']];
      pre = '<section class="card"><h2 class="label">Banks</h2>' + bankBar() +
        '<span class="small">Hold A to step Grit, Groove, Glow. The bank sets what A, B and C do and what the pedal does: dirt blend in Grit, wah in Groove (volume with the wah off), Bloom in Glow. Changing bank never changes the sound. Each song opens in its own bank (This song, below).</span></section>' +
        '<section class="card"><h2 class="label">Tap and hold</h2>' + jobs.map(function (j, i) {
        return '<div style="display:flex;align-items:center;gap:12px">' + flip(j[0]).replace('class="flip "', 'class="flip" style="width:36px;height:42px;font-size:22px"') +
          '<span style="flex:1"><span style="display:block;font-size:16px">Tap: ' + j[1] + '</span><span class="small">Hold: ' + j[2] + '</span></span>' +
          '<button type="button" class="wide" style="height:44px;padding:0 14px;font-size:14px" data-learn="' + i + '">Learn</button></div>';
      }).join('') + '<div class="small">Press C and D together to swap Stomp and Song mode. Hold A and B for a second (Stomp mode) for Kuddle. Keep is the button on the phone.</div></section>' +
        '<section class="card"><h2 class="label">Expression</h2><div class="small">Rock the pedal to heel and press Heel, then to toe and press Toe. If heel and toe still work the wrong way round, press Reverse.</div>' +
        '<div class="pair"><button type="button" class="wide" data-cal="0">Heel</button><button type="button" class="wide" data-cal="1">Toe</button></div>' +
        '<div class="pair"><button type="button" class="wide" data-cal="3">' + (S.health && S.health.exp && S.health.exp.rev ? 'Reversed: on' : 'Reverse') + '</button></div>' +
        '<div class="pair"><button type="button" class="wide" data-learn="4">Learn pedal</button><button type="button" class="wide" data-cal="2">Reset</button></div></section>' +
        '<section class="card"><h2 class="label">Status</h2><span class="small">' + esc(S.footswitch || 'Unknown') + '</span></section>';
    }
    if (mod === 'output') {
      var mt = S.meter;
      extra = '<section class="card"><h2 class="label">Keep</h2><label class="switch" style="background:#262626">Tag Keeps as Live<input type="checkbox" data-act="keeperLive"' + (S.keeperLive ? ' checked' : '') + '></label></section>' +
        '<section class="card"><h2 class="label">Keystone</h2><span class="small">Audio: ' + esc(S.audio || 'unknown') + '</span>' +
        '<span class="small" id="inLevel">' + inputText() + '</span>' +
        '<span class="small">CPU ' + (mt.cpu != null ? mt.cpu : '-') + '%' + (mt.light ? ', effects in light mode' : '') + '. Round trip ' + (mt.lat || '-') + ' ms.</span>' +
        '<span class="small">Version ' + esc(S.schema.version) + '</span></section>';
    }
    var body = list ? renderParams(list) : '';
    extra += defaultsCard(mod, m.title);
    return '<section class="screen" style="gap:14px">' + header(m.title, { id: 'tone', label: 'Tone' }, right) + pre + body + extra + nav('tone') + '</section>';
  }

  function setlistScreen() {
    var rows = S.songs.map(function (name, i) {
      var cur = i === S.current, pt = isPart(i);
      var sub = cur ? (S.edited ? 'Playing now, <span class="edited">edited</span>' : 'Playing now') : pt ? 'Part of ' + esc(S.songs[headOf(i)]) : 'Tap to load';
      var inner = '';
      if (S.renameRow === i) inner = '<div class="rename"><input id="renameText" maxlength="48" value="' + esc(name) + '" aria-label="Song name"><button type="button" data-act="renameSave" data-i="' + i + '">Save</button></div>';
      else if (S.confirmDel === i) inner = '<div class="acts" style="grid-template-columns:2fr 1fr 1fr"><span class="small" style="align-self:center">Delete ' + esc(name) + '?</span><button type="button" class="del" data-act="delYes" data-i="' + i + '">Delete</button><button type="button" data-act="delNo">Keep</button></div>';
      else if (S.openRow === i) inner = '<div class="acts"><button type="button" data-act="rename" data-i="' + i + '">Rename</button><button type="button" data-act="copy" data-i="' + i + '">Copy</button>' +
        '<button type="button" data-act="up" data-i="' + i + '" aria-label="Move up">Up</button><button type="button" data-act="down" data-i="' + i + '" aria-label="Move down">Down</button>' +
        (kud() ? '' : '<button type="button" data-act="addPart" data-i="' + i + '">Add part</button>') +
        (i > 0 && !kud() ? '<button type="button" data-act="part" data-i="' + i + '">' + (pt ? 'Own song' : 'Part of above') + '</button>' : '<span></span>') +
        '<span></span><button type="button" class="del" data-act="del" data-i="' + i + '">Delete</button></div>';
      return '<div class="srow' + (cur ? ' cur' : '') + (pt ? ' part' : '') + '" data-row="' + i + '"><div class="main">' +
        '<button type="button" class="grip" data-grip="' + i + '" aria-label="Drag to reorder">' + ICON.grip + '</button>' +
        '<button type="button" class="sload" data-song="' + i + '">' + (pt ? '<span class="partmark" aria-hidden="true"></span>' : flip(two(songNo(i)), cur ? 'blue' : '')) +
        '<span style="display:flex;flex-direction:column;min-width:0"><span class="n">' + esc(name) + '</span><span class="s">' + sub + '</span></span></button>' +
        '<button type="button" class="dots" data-menu="' + i + '" aria-label="Song options">' + ICON.dots + '</button></div>' + inner + '</div>';
    }).join('');
    var fullSongs = songsOnly() >= S.maxSongs || S.songs.length >= S.maxEntries;
    var K = kud();
    return '<section class="screen" style="gap:14px">' + header(K ? 'Presets' : 'Setlist', { id: 'play', label: 'Play' }, '<span class="small">' + songsOnly() + ' of ' + S.maxSongs + (K ? ' presets' : ' songs') + '</span>') +
      '<div class="group" id="rows">' + rows + '</div>' +
      '<div class="pair"><button type="button" class="wide" data-act="add"' + (fullSongs ? ' disabled style="opacity:0.4"' : '') + '>' + (K ? 'Add preset' : 'Add song') + '</button><button type="button" class="wide blue" data-act="save">Save edits</button></div>' +
      '<div class="small">Save writes your current changes into the loaded song or part. Changing song without saving puts it back as it was.</div>' +
      (K ? '' : S.confirmBand ? '<div class="acts" style="grid-template-columns:2fr 1fr 1fr"><span class="small" style="align-self:center">Replace this setlist with the band\'s? Keystone keeps a copy of this one.</span><button type="button" class="del" data-act="bandYes">Replace</button><button type="button" data-act="bandNo">Keep</button></div>'
        : '<button type="button" class="wide" data-act="band">Load the band setlist</button>') +
      (K ? '' : '<div class="small">Parts split a song, for a big change in the middle: hold B and everything changes at once, with the delay and reverb tails ringing over and a held drone carrying on. Add part copies the song\'s settings as a starting point.</div>') +
      nav('play') + '</section>';
  }

  // ---------------------------------------------------------------- Kaleidoscope
  // Tapping the name goes Keystone, Kuddle, Kaleidoscope and round again (Gig lock skips it)
  function rigNext() {
    if (kalOn()) return { name: 'Keystone', rig: 0 };
    if (S.rig === 0) return { name: 'Kuddle', rig: 1 };
    if (S.kal && !S.kal.gigLock) return { name: 'Kaleidoscope', kal: true };
    return { name: 'Keystone', rig: 0 };
  }
  function pedalMeta(id) {
    for (var k = 0; k < S.pedalTable.length; k++) if (S.pedalTable[k].id === id) return S.pedalTable[k];
    var blank = { label: '', tap: 'Its own job', hold: '' };
    return { id: id, name: id.charAt(0).toUpperCase() + id.slice(1), from: 'A plugin in the Pedals folder', colour: '#B9B2A6', sw: [blank, blank, blank, blank], combos: '' };
  }
  var kalPending = {}, kalTimer = null;
  function kalSet(i, v) {          // throttled while dragging, like setParam
    var p = S.kalParams[i]; if (!p) return;
    p.v = v;
    kalPending[i] = v;
    if (!kalTimer) kalTimer = setTimeout(function () {
      kalTimer = null;
      for (var k in kalPending) send({ t: 'kalSet', i: +k, v: kalPending[k] });
      kalPending = {};
    }, 25);
  }
  function kalFlip(letter, colour) {
    return '<span class="flip" style="width:36px;height:42px;font-size:22px;flex:none;' + (colour ? 'background:' + colour + ';color:#111111' : '') + '">' + esc(letter) + '</span>';
  }
  function gigLockRow() {
    return '<label class="switch"><span style="display:flex;flex-direction:column;gap:2px"><span>Gig lock</span><span class="small">Hides Kaleidoscope so nothing loads mid-set</span></span>' +
      '<input type="checkbox" data-act="gigLock"' + (S.kal && S.kal.gigLock ? ' checked' : '') + '></label>';
  }
  function kalPedalsScreen() {
    var ids = S.kal.installed || [], loaded = S.kal.loaded, lm = loaded ? pedalMeta(loaded) : null;
    if (S.kalSel == null || ids.indexOf(S.kalSel) < 0) S.kalSel = loaded || ids[S.kal.pick] || ids[0] || null;
    var sel = S.kalSel ? pedalMeta(S.kalSel) : null;
    var grid = ids.map(function (id) {
      var m = pedalMeta(id), on = id === S.kalSel;
      return '<button type="button" class="kpedal" data-kalpick="' + esc(id) + '" style="border-color:' + (on ? m.colour : 'var(--card)') + '">' +
        '<span class="flip" style="width:36px;height:42px;font-size:22px;flex:none;' + (on ? 'background:' + m.colour + ';color:#111111' : 'color:' + m.colour) + '">' + esc(m.name.charAt(0)) + '</span>' +
        '<span class="kp"><span class="n">' + esc(m.name) + '</span><span class="f">' + esc(m.from) + '</span></span></button>';
    }).join('');
    var loadBtn = !sel ? '' : sel.id === loaded
      ? '<button type="button" class="wide" disabled>' + esc(sel.name) + ' is loaded</button>'
      : '<button type="button" class="wide" data-act="kalLoad" style="background:' + sel.colour + ';color:#111111">Load ' + esc(sel.name) + '</button>';
    return '<section class="screen">' +
      '<div class="top"><button type="button" class="brand rig" data-act="rig" aria-label="Switch to Keystone">' + PEBBLE +
      '<span style="display:flex;flex-direction:column;align-items:flex-start;gap:3px"><span class="title">Kaleidoscope</span><span class="swap">TAP FOR KEYSTONE</span></span></button><span class="tops">' + pill() + '</span></div>' +
      (lm ? '<section class="card" style="flex-direction:row;align-items:center;gap:14px">' + kalFlip(lm.name.charAt(0), lm.colour) +
        '<span style="flex:1;display:flex;flex-direction:column;gap:2px;min-width:0"><span class="label">Loaded now</span><span style="font-size:22px;font-weight:700">' + esc(lm.name) + '</span><span class="small">' + esc(lm.from) + '</span>' + (S.kalId === loaded ? kalDots(loaded) : '') + '</span>' +
        '<button type="button" class="wide" style="height:44px;padding:0 16px" data-go="tone">Controls</button></section>'
        : '<section class="card"><span style="font-size:17px;font-weight:700">No pedal loaded</span><span class="small">Pick one and load it. On the floor: tap A and B to look through them, D to load, hold A and B to go back to Keystone.</span></section>') +
      '<h2 class="label">Pedals</h2>' +
      (ids.length ? '<div class="kgrid">' + grid + '</div>' + loadBtn +
        '<span class="small" style="text-align:center">The old pedal fades out, then the new one fades in.</span>'
        : '<section class="card"><span class="small">No pedals found. Updating Keystone puts them in its Pedals folder, or drop a .vst3 into Documents\\Keystone\\Pedals.</span></section>') +
      (loaded ? '<button type="button" class="wide" data-act="kalUnload" style="color:#F0997B">Put ' + esc(lm.name) + ' away</button>' : '') +
      gigLockRow() + nav('play') + '</section>';
  }
  // A pedal's page: its face first (laid out like the plugin's own panel), the four
  // footswitches lit while their jobs are on, then the rest folded into the plugin's groups.
  function kalLayout(id) { return (window.KS_PEDAL_LAYOUTS && window.KS_PEDAL_LAYOUTS[id]) || {}; }
  function kalByName() {
    var m = {};
    S.kalParams.forEach(function (p) { if (!(p.name in m)) m[p.name] = p; });
    return m;
  }
  function kalLabel(L, name) { return (L.rename && L.rename[name]) || name; }
  function kalChoice(L, p, c) { var r = L.choices && L.choices[p.name]; return (r && r[c]) || c; }
  function kalSteps(p) { return p.choices ? p.choices.length : 0; }
  // what lights each footswitch: on, off, or no light for a tap job
  function kalFeet(id, by) {
    var L = kalLayout(id), m = pedalMeta(id);
    return [0, 1, 2, 3].map(function (k) {
      var f = (L.feet && L.feet[k]) || {}, w = m.sw[k] || {};
      var lp = f.lit && by[f.lit], tp = f.text && by[f.text], pp = f.press && by[f.press];
      return { k: k, f: f, w: w, lit: lp ? (lp.v > 0.5) !== !!f.inv : null, text: tp ? tp.text : '', press: pp, litP: lp };
    });
  }
  function kalDots(id) {
    var by = kalByName(), col = pedalMeta(id).colour;
    return '<span class="kdots" aria-label="Footswitches">' + kalFeet(id, by).map(function (t) {
      return '<span class="kdot" style="' + (t.lit ? 'background:' + col + ';color:#111111' : t.lit === false ? '' : 'opacity:.45') + '">' + 'ABCD'.charAt(t.k) + '</span>';
    }).join('') + '</span>';
  }
  function kalDialSvg(v, col) {
    var A0 = Math.PI * 0.75, SPAN = Math.PI * 1.5, r = 26, c = 36;
    function pt(a, rr) { return [(c + rr * Math.cos(a)).toFixed(1), (c + rr * Math.sin(a)).toFixed(1)]; }
    function arc(a0, a1) { return 'M ' + pt(a0, r).join(' ') + ' A ' + r + ' ' + r + ' 0 ' + (a1 - a0 > Math.PI ? 1 : 0) + ' 1 ' + pt(a1, r).join(' '); }
    var a = A0 + SPAN * Math.max(0, Math.min(1, v)), tip = pt(a, 17);
    return '<svg width="72" height="72" viewBox="0 0 72 72" aria-hidden="true"><circle cx="36" cy="36" r="30" fill="#232327"/>' +
      '<path d="' + arc(A0, A0 + SPAN) + '" fill="none" stroke="#3A3A40" stroke-width="5" stroke-linecap="round"/>' +
      (v > 0.005 ? '<path d="' + arc(A0, a) + '" fill="none" stroke="' + col + '" stroke-width="5" stroke-linecap="round"/>' : '') +
      '<line x1="36" y1="36" x2="' + tip[0] + '" y2="' + tip[1] + '" stroke="#F2EDE4" stroke-width="3" stroke-linecap="round"/></svg>';
  }
  function kalDial(L, p, label, col) {
    return '<div class="kdial" data-kdial="' + p.i + '" role="slider" tabindex="0" aria-label="' + esc(kalLabel(L, p.name)) + '" aria-valuetext="' + esc(p.text) + '">' +
      '<span class="ksvg">' + kalDialSvg(p.v, col) + '</span><span class="kn">' + esc(label) + '</span><span class="kv" data-kaltext="' + p.i + '">' + esc(p.text) + '</span></div>';
  }
  function kalSeg(L, p, col, cols, vertical) {
    var n = kalSteps(p), cur = Math.round(p.v * (n - 1));
    return '<div class="seg kseg" style="' + (vertical ? 'grid-template-columns:1fr' : 'grid-template-columns:repeat(' + cols + ',minmax(0,1fr))') + '">' + p.choices.map(function (c, k) {
      return '<button type="button" class="' + (k === cur ? 'on' : '') + '" style="' + (k === cur ? 'background:' + col + ';color:#111111' : '') + '" data-kalset="' + p.i + '" data-v="' + (n > 1 ? k / (n - 1) : 0) + '">' + esc(kalChoice(L, p, c)) + '</button>';
    }).join('') + '</div>';
  }
  function kalControl(L, p, col) {
    var name = esc(kalLabel(L, p.name));
    if (p.kind === 'toggle')
      return '<label class="switch" style="background:#262626">' + name + '<input type="checkbox" data-kaltoggle="' + p.i + '"' + (p.v > 0.5 ? ' checked' : '') + ' style="accent-color:' + col + '"></label>';
    if (p.kind === 'choice') {
      var n = p.choices.length;
      return '<div class="small">' + name + '</div>' + kalSeg(L, p, col, n <= 4 ? n : 3, false);
    }
    return '<label class="slider"><span class="ends"><span>' + name + '</span><span data-kaltext="' + p.i + '">' + esc(p.text) + '</span></span>' +
      '<input type="range" min="0" max="1000" value="' + Math.round(p.v * 1000) + '" data-kalrange="' + p.i + '" aria-label="' + name + '" style="accent-color:' + col + '"></label>';
  }
  function kalToneScreen() {
    var id = S.kal.loaded;
    if (!id) return kalPedalsScreen();
    var m = pedalMeta(id), col = m.colour, L = kalLayout(id);
    var head = '<div class="top"><button type="button" class="back" data-go="play">' + ICON.back + 'Pedals</button><span class="title" style="color:' + col + '">' + esc(m.name) + '</span><span></span></div>';
    if (!S.kalParams.length || S.kalId !== id)
      return '<section class="screen" style="gap:14px">' + head + '<section class="card"><span class="small">Getting ' + esc(m.name) + '\'s controls…</span></section>' + nav('tone') + '</section>';
    var by = kalByName(), used = {};
    (L.hide || []).forEach(function (n) { used[n] = true; });
    function take(n) { var p = by[n.split('|')[0]]; if (p) used[p.name] = true; return p; }

    // the face
    var face = '';
    (L.top || []).forEach(function (n) {
      var p = take(n); if (!p || !p.choices) return;
      var c = p.choices.length;
      face += '<div class="kgroup"><span class="klabel">' + esc(kalLabel(L, p.name).toUpperCase()) + '</span>' + kalSeg(L, p, col, c > 4 ? 4 : c, false) + '</div>';
    });
    (L.rows || []).forEach(function (row) {
      face += '<div class="kcols" style="grid-template-columns:repeat(' + row.length + ',minmax(0,1fr))">' + row.map(function (c) {
        var out = '<div class="kcol"><span class="klabel" style="text-align:center">' + esc(c.title || '') + '</span>';
        var sp = c.seg && take(c.seg);
        if (sp && sp.choices) out += kalSeg(L, sp, col, 1, true);
        (c.knobs || []).forEach(function (k) {
          var p = take(k); if (!p) return;
          out += kalDial(L, p, k.indexOf('|') > 0 ? k.split('|')[1] : kalLabel(L, p.name), col);
        });
        return out + '</div>';
      }).join('') + '</div>';
    });
    (L.steps || []).forEach(function (n) {
      var p = take(n); if (!p) return;
      if (!p.choices) { face += kalControl(L, p, col); return; }
      var c = p.choices.length;
      face += '<div class="kgroup"><span class="klabel">' + esc(kalLabel(L, p.name).toUpperCase()) + '</span>' + kalSeg(L, p, col, c <= 4 ? c : c <= 6 ? 3 : 5, false) + '</div>';
    });
    if (!face) face = '<span class="small">This pedal has no face yet: its controls are below.</span>';

    // the footswitches, lit while their jobs are on; the phone can tap them too
    var feet = kalFeet(id, by).map(function (t) {
      var can = !!(t.press || t.litP);
      var state = t.lit === true ? 'ON' : t.lit === false ? 'OFF' : (t.text || '');
      return '<button type="button" class="kfoot' + (t.lit ? ' lit' : '') + '"' + (can ? ' data-kfoot="' + t.k + '"' : ' disabled') +
        ' style="' + (t.lit ? 'background:' + col + ';color:#111111' : '') + '" aria-pressed="' + (t.lit ? 'true' : 'false') + '">' +
        '<span class="kk">' + 'ABCD'.charAt(t.k) + '</span><span class="kl">' + esc(t.w.label || '') + '</span>' +
        '<span class="ks">' + esc(state || (can ? '' : 'On the floor')) + '</span>' +
        '<span class="kh">' + esc(t.w.tap || '') + (t.w.hold ? ' · hold: ' + esc(t.w.hold) : '') + '</span></button>';
    }).join('');

    // going deeper: the plugin's groups, folded
    var secs = (L.sections || []).map(function (sec, si) {
      var ps = sec.params.map(function (n) { var p = by[n]; return p && used[p.name] !== true && used[p.name] !== 'sec' ? p : null; }).filter(Boolean);
      ps.forEach(function (p) { used[p.name] = 'sec'; });
      return { name: sec.name, sum: sec.sum, ps: ps, key: id + si };
    }).filter(function (x) { return x.ps.length; });
    (L.feet || []).forEach(function (f) { ['lit', 'press', 'text'].forEach(function (k) { if (f[k] && !used[f[k]]) used[f[k]] = 'feet'; }); });   // on the tiles already
    var rest = S.kalParams.filter(function (p) { return !used[p.name]; });
    if (rest.length) secs.push({ name: 'Everything else', sum: rest.length + ' more', ps: rest, key: id + 'rest' });
    var deep = secs.map(function (x) {
      var open = !!S.kalOpen[x.key];
      return '<div class="kacc"><button type="button" class="kacch" data-kacc="' + esc(x.key) + '" aria-expanded="' + open + '"><span class="kat"><span class="kan">' + esc(x.name) + '</span><span class="small">' + esc(x.sum) + '</span></span><span class="kchev">' + (open ? '−' : '+') + '</span></button>' +
        (open ? '<div class="kaccb">' + x.ps.map(function (p) { return kalControl(L, p, col); }).join('') + '</div>' : '') + '</div>';
    }).join('');

    return '<section class="screen" style="gap:14px">' + head +
      '<section class="card kface"><span class="kstrip" style="background:' + col + '"></span><span class="klabel">' + esc(m.from.toUpperCase()) + '</span>' + face + '</section>' +
      '<h2 class="label">Footswitches · tap here or stomp</h2><div class="kfeet">' + feet + '</div>' +
      (m.combos ? '<span class="small">' + esc(m.combos) + ' Hold C and D for Keep.</span>' : '') +
      (deep ? '<h2 class="label">Going deeper</h2>' + deep : '') +
      '<button type="button" class="wide" data-act="kalUnload" style="color:#F0997B">Put ' + esc(m.name) + ' away</button>' +
      nav('tone') + '</section>';
  }

  // ---------------------------------------------------------------- render
  var app = document.getElementById('app');
  var lastScreenKey = '';
  function render(soft) {
    if (!S.schema && S.screen !== 'play' && S.screen !== 'health' && S.screen !== 'pairWatch') S.screen = 'play';
    var active = document.activeElement;
    if (soft && active && active.tagName === 'INPUT' && active.type === 'text') return;   // don't wipe typing
    if (soft && S.dragging) return;
    var html;
    switch (S.screen) {
      case 'tuner': html = tunerScreen(); break;
      case 'tone': html = kalOn() ? kalToneScreen() : S.schema ? toneScreen() : playScreen(); break;
      case 'module': html = kalOn() ? kalToneScreen() : S.schema ? moduleScreen() : playScreen(); break;
      case 'setlist': html = setlistScreen(); break;
      case 'health': html = healthScreen(); break;
      case 'pairWatch': html = pairWatchScreen(); break;
      case 'recorder': html = recorderScreen(); break;
      case 'take': html = takeScreen(); break;
      case 'listen': html = listenScreen(); break;
      default: html = kalOn() ? kalPedalsScreen() : playScreen();
    }
    document.body.classList.toggle('kuddle', kud());
    document.body.classList.toggle('kal', kalOn());
    var key = S.screen + (S.module || '');
    var y = window.scrollY;
    app.innerHTML = html;
    if (key === lastScreenKey) window.scrollTo(0, y); else window.scrollTo(0, 0);
    lastScreenKey = key;
    if (S.sel) sheet();
    else if (S.save) saveSheet();
    strip();
  }

  // meters only touch what moves
  var needleDb = -60;
  function meters() {
    var m = S.meter;
    if (S.screen === 'play') {
      var target = m.out != null ? m.out : -60;
      needleDb = target > needleDb ? target : needleDb + (target - needleDb) * 0.25;   // fast up, gentle fall
      var p = needle(dbAngle(needleDb)), n = document.getElementById('needle');
      if (n) { n.setAttribute('x2', p.x.toFixed(1)); n.setAttribute('y2', p.y.toFixed(1)); }
      if (S.pedalDrag == null) {
        var bar = document.getElementById('pedalBar'), pct = document.getElementById('pedalPct');
        var pi = pedalInfo(), pn = document.getElementById('pedalName');
        if (bar) bar.style.width = (pi.v * 100) + '%';
        if (pct) pct.textContent = pi.text;
        if (pn && pn.textContent !== 'Pedal: ' + pi.name) queueRender(true);
      }
      var droneTile = document.querySelector('[data-stomp="D"] .st');
      var want = m.drone ? 'HOLDING' + (m.drone > 1 ? ' ' + m.drone : '') : 'READY';
      if (droneTile && droneTile.textContent !== want) render(true);
    } else if (S.screen === 'tuner') {
      updateTuner();
    } else if (S.screen === 'module' && S.module === 'output') {
      var il = document.getElementById('inLevel'); if (il) il.textContent = inputText();
    } else if (S.screen === 'module' && S.module === 'bloom') {
      var b2 = document.getElementById('pedalBar'); if (b2) b2.style.width = (m.bloom * 100) + '%';
    }
  }

  // ---------------------------------------------------------------- save sheet
  // Play's Save: update the song you're on, or put this sound somewhere else
  function saveBar() {
    if (!S.songs.length) return '';
    return '<div class="savebar"><button type="button" class="wide savebtn' + (S.edited ? ' lit' : '') + '" data-act="saveOpen">' +
      (S.edited ? 'SAVE CHANGES' : 'SAVE') + '</button>' +
      (S.edited ? '<button type="button" class="wide undo" data-act="saveUndo">UNDO</button>' : '') + '</div>';
  }
  function saveSheet() {
    var v = S.save; if (!v) { sheetEl.hidden = true; return; }
    var K = kud(), i = S.current, head = headOf(i), here = labelOf(i);
    var full = songsOnly() >= S.maxSongs || S.songs.length >= S.maxEntries, fullAll = S.songs.length >= S.maxEntries;
    var top = function (title, backTo) {
      return '<div class="label" style="display:flex;justify-content:space-between;align-items:center"><span>' + title + '</span>' +
        '<button type="button" class="back" data-act="' + (backTo ? 'saveBack' : 'saveClose') + '" style="min-height:24px">' + (backTo ? 'Back' : 'Cancel') + '</button></div>';
    };
    var html;
    if (v.step === 'name') {
      var part = v.kind === 'part';
      var def = part ? 'Part ' + (partsIn(head) + 1) : (K ? 'Preset ' : 'Song ') + (songsOnly() + 1);
      html = top(part ? 'New part of ' + esc(S.songs[head]) : K ? 'New preset' : 'New song', true) +
        '<input id="saveName" class="namebox" maxlength="48" placeholder="' + esc(def) + '" aria-label="Name" value="' + esc(v.name || '') + '">' +
        '<span class="small">' + (part ? 'Goes after the last part of ' + esc(S.songs[head]) + '.' : 'Goes at the end of the setlist, as ' + (K ? 'preset ' : 'song ') + (songsOnly() + 1) + '.') +
        ' Keystone moves you onto it, and ' + esc(here) + ' keeps its old sound.</span>' +
        '<button type="button" class="wide blue" data-act="saveNew">SAVE</button>';
    } else if (v.step === 'pick') {
      html = top('Save over', true) + '<div class="picklist">' + S.songs.map(function (n, k) {
        var pt = isPart(k), cur = k === i;
        return '<button type="button" class="row pick' + (pt ? ' part' : '') + '" data-savepick="' + k + '"' + (cur ? ' disabled' : '') + '>' +
          '<span><span class="t">' + (pt ? esc(n) : songNo(k) + '. ' + esc(n)) + '</span>' + (cur ? '<span class="s">Playing now</span>' : '') + '</span></button>';
      }).join('') + '</div>';
    } else if (v.step === 'confirm') {
      html = top('Save over ' + esc(labelOf(v.target)) + '?', true) +
        '<span class="small">Its old sound is replaced with what you\'re playing now. Keystone moves you onto it. Last night\'s backup still has the old one.</span>' +
        '<div class="pair"><button type="button" class="wide" data-act="saveBack">BACK</button><button type="button" class="wide blue" data-act="saveOver">REPLACE</button></div>';
    } else {
      html = top('Save this sound') +
        '<button type="button" class="wide blue big" data-act="saveHere"' + (S.edited ? '' : ' disabled') + '><span>UPDATE ' + esc(S.songs[i]).toUpperCase() + '</span>' +
        '<span class="sub">' + (S.edited ? 'Replaces the saved sound of ' + esc(here) : esc(here) + ' hasn\'t changed') + '</span></button>' +
        '<span class="label" style="margin-top:4px">Or save it as</span>' +
        (K ? '' : '<button type="button" class="row" data-savekind="part"' + (fullAll ? ' disabled' : '') + '><span><span class="t">A new part of ' + esc(S.songs[head]) + '</span><span class="s">' + (fullAll ? 'The setlist is full' : 'After its last part') + '</span></span><span class="r">' + ICON.right + '</span></button>') +
        '<button type="button" class="row" data-savekind="song"' + (full ? ' disabled' : '') + '><span><span class="t">' + (K ? 'A new preset' : 'A new song') + '</span><span class="s">' + (full ? songsOnly() + ' of ' + S.maxSongs + ', the setlist is full' : (K ? 'Preset ' : 'Song ') + (songsOnly() + 1) + ' of ' + S.maxSongs) + '</span></span><span class="r">' + ICON.right + '</span></button>' +
        '<button type="button" class="row" data-act="savePick"><span><span class="t">' + (K ? 'Another preset' : 'Another song or part') + '</span><span class="s">Pick it from the setlist</span></span><span class="r">' + ICON.right + '</span></button>';
    }
    sheetEl.hidden = false;
    sheetEl.innerHTML = html;
    if (v.step === 'name') { var nb = document.getElementById('saveName'); if (nb && !v.typed) { v.typed = true; setTimeout(function () { nb.focus(); }, 30); } }
  }
  function partsIn(head) { var n = 0; for (var k = head + 1; k < S.songs.length && isPart(k); k++) n++; return n; }
  function saveClose() { S.save = null; sheetEl.hidden = true; render(true); }

  // ---------------------------------------------------------------- adjuster sheet
  var sheetEl = document.getElementById('sheet');
  function sheet() {
    var p = S.params[S.sel];
    if (!p) { sheetEl.hidden = true; return; }
    sheetEl.hidden = false;
    sheetEl.innerHTML = '<div class="label" style="display:flex;justify-content:space-between"><span>' + esc(p.label) + '</span><button type="button" class="back" data-act="sheetClose" style="min-height:24px">Done</button></div>' +
      '<div class="val"><button type="button" class="step" data-step="-1" aria-label="Down">−</button>' + flip(fmt(p.id)) + '<button type="button" class="step" data-step="1" aria-label="Up">+</button></div>' +
      '<input type="range" min="0" max="1000" value="' + Math.round(toPos(p, val(p.id)) * 1000) + '" data-range="' + p.id + '" aria-label="' + esc(p.label) + '">';
  }
  function step(dir) {
    var p = S.params[S.sel]; if (!p) return;
    var v = val(p.id), st = stepOf(p);
    if (st) v = Math.round((v + dir * st) / st) * st;
    else v = fromPos(p, toPos(p, v) + dir * 0.01);
    v = Math.max(p.min, Math.min(p.max, v));
    setParam(p.id, v);
    render(true); sheet();
  }

  // ---------------------------------------------------------------- toast and Pebble
  var toastEl = document.getElementById('toast'), toastTimer = null;
  function toast(text) {
    toastEl.innerHTML = flip(text.toUpperCase(), 'flipping');
    toastEl.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { toastEl.classList.remove('show'); }, 1500);
  }
  function nod() {
    var p = document.getElementById('pebble');
    if (p) { p.classList.remove('nod'); void p.offsetWidth; p.classList.add('nod'); }
  }

  // ---------------------------------------------------------------- events
  function go(screen) {
    if (screen === 'tuner' && !S.tuner) { send({ t: 'tuner', on: true }); S.tunerAuto = false; }
    if (S.screen === 'tuner' && screen !== 'tuner' && S.tuner) { send({ t: 'tuner', on: false }); S.tuner = false; }
    if (screen === 'recorder') send({ t: 'takes' });
    if (screen === 'take' && S.take) send({ t: 'takeInfo', take: S.take });
    if (screen === 'listen' && S.take && !S.pack) send({ t: 'listenList', take: S.take });
    if (S.screen === 'listen' && screen !== 'listen') stopListening();
    if (screen === 'health') updateCheck(false);
    S.screen = screen; S.sel = null; S.save = null; sheetEl.hidden = true; S.openRow = -1; S.renameRow = -1; S.confirmDel = -1; S.showCaptures = false; S.showSpaces = false;
    render();
  }

  document.addEventListener('click', function (e) {
    var t = e.target.closest('button, input[type=checkbox], [data-module]');
    if (!t) return;
    var d = t.dataset;
    if (d.go) return go(d.go);
    if (d.stomp) {
      // show the change at once; Keystone's answer confirms it
      if (send({ t: 'stomp', k: d.stomp })) {
        var tl = d.stomp !== 'D' ? bankTiles(S.bank)['ABC'.indexOf(d.stomp)] : null;
        if (tl && tl.id) S.values[tl.id] = on(tl.id) ? 0 : 1;
        else if (d.stomp === 'A' && on('drive.on')) S.values['drive.voice'] = Math.round(val('drive.voice')) ? 0 : 1;
        render(true);
      }
      return;
    }
    if (d.def) {
      if (d.def === 'done') { S.defAck = null; return render(true); }
      send({ t: 'default', op: d.def, module: S.module }); return;
    }
    if (d.take) { if (S.take !== d.take) { S.take = d.take; S.takeInfo = null; S.pickFrom = null; S.pack = null; } return go('take'); }
    if (d.from != null) { S.pickFrom = +d.from; return render(true); }
    if (d.listen) return playFile(d.listen);
    if (d.use) { if (S.pack) send({ t: 'listenUse', take: S.take, from: d.use, onto: S.pack.played }); return; }
    if (d.bank) { if (send({ t: 'bank', i: +d.bank })) { S.bank = +d.bank; render(true); } return; }
    if (d.onoff) { setParam(d.onoff, on(d.onoff) ? 0 : 1); return render(true); }
    if (d.module) { S.module = d.module; return go('module'); }
    if (d.set) { setParam(d.set, +d.v); return render(true); }
    if (d.toggle) { setParam(d.toggle, t.checked ? 1 : 0); return render(true); }
    if (d.flip) { S.sel = S.sel === d.flip ? null : d.flip; render(true); if (!S.sel) sheetEl.hidden = true; return; }
    if (d.step) return step(+d.step);
    if (d.song) { if (send({ t: 'song', i: +d.song })) { S.current = +d.song; render(true); } return; }
    if (d.menu) { var i = +d.menu; S.openRow = S.openRow === i ? -1 : i; S.renameRow = -1; S.confirmDel = -1; return render(); }
    if (d.capture) { send({ t: 'capture', file: d.capture }); S.showCaptures = false; return render(); }
    if (d.space) { send({ t: 'space', file: d.space }); S.showSpaces = false; return render(); }
    if (d.learn) { send({ t: 'learn', sw: +d.learn }); return; }
    if (d.cal) { send({ t: 'calibrate', step: +d.cal }); return; }
    if (d.savekind) { S.save = { step: 'name', kind: d.savekind }; return saveSheet(); }
    if (d.savepick) { S.save = { step: 'confirm', target: +d.savepick }; return saveSheet(); }
    if (d.kalpick) { S.kalSel = d.kalpick; return render(true); }
    if (d.kacc) { S.kalOpen[d.kacc] = !S.kalOpen[d.kacc]; return render(true); }
    if (d.kalset != null) { kalSet(+d.kalset, +d.v); return render(true); }
    if (d.kaltoggle != null) { kalSet(+d.kaltoggle, t.checked ? 1 : 0); return render(true); }
    var i2 = d.i != null ? +d.i : -1;
    switch (d.act) {
      case 'saveOpen': S.sel = null; S.save = { step: 'menu' }; saveSheet(); break;
      case 'saveClose': saveClose(); break;
      case 'saveBack': S.save = { step: S.save && S.save.step === 'confirm' ? 'pick' : 'menu' }; saveSheet(); break;
      case 'savePick': S.save = { step: 'pick' }; saveSheet(); break;
      case 'saveHere': send({ t: 'setlist', op: 'save' }); saveClose(); break;
      case 'saveUndo': send({ t: 'setlist', op: 'revert' }); break;
      case 'saveOver': send({ t: 'setlist', op: 'saveTo', i: S.save.target }); saveClose(); break;
      case 'saveNew':
        var nm = (document.getElementById('saveName') || {}).value || '';
        send({ t: 'setlist', op: S.save.kind === 'part' ? 'saveNewPart' : 'saveNewSong', i: S.current, name: nm.trim() }); saveClose(); break;
      case 'keep': send({ t: 'keep' }); break;
      case 'updateCheck': updateCheck(true); render(true); break;
      case 'updateFirmware':
        var fa = asset(rigDevice() === 'pi' ? /linux-arm64\.tar\.gz$/ : /-Setup\.exe$/);
        if (fa && connected()) { S.upd.msg = ''; S.upd.err = null; S.upd.busy = { what: 'firmware', stage: 'Downloading', pct: 0 }; KeystoneNative.updateFirmware(fa.url, fa.name, fa.size, fa.digest || '', S.upd.latest); render(true); }
        break;
      case 'updateApp':
        var ap = asset(PHONE_APK);
        if (ap) { S.upd.err = null; S.upd.busy = { what: 'app', stage: 'Downloading', pct: 0 }; KeystoneNative.updateApp(ap.url, ap.size); render(true); }
        break;
      case 'updateWatch': startWatchUpdate(); break;
      case 'updateWatchTyped': var wa2 = document.getElementById('watchAddr'); startWatchUpdate(wa2 ? wa2.value : ''); break;
      case 'watchAuto': try { localStorage.setItem('ks.watchAuto', watchAuto() ? '0' : '1'); } catch (e) { } render(true); break;
      case 'watchUnpair': watchSetPaired(false); S.watchAfterPair = false; go('pairWatch'); break;
      case 'pairWatch':
        var pa = document.getElementById('pairAddr');
        if ((S.pairCode || '').length === 6 && !S.upd.busy) {
          S.watchErr = null; S.upd.busy = { what: 'watch', stage: 'Looking for the watch', pct: 0 };
          KeystoneNative.watchPair(S.pairCode, watchInfo().ip, pa ? pa.value : '');
          render(true);
        }
        break;
      case 'makePack':
        if (send({ t: 'listen', take: S.take, from: S.pickFrom || 0, secs: 30 })) { S.making = true; S.pack = null; render(true); }
        break;
      case 'demoOn': demo(true); break;
      case 'demoOff': demo(false); break;
      case 'wifiToggle': if (S.wifi) send({ t: 'wifi', on: !S.wifi.on }); break;
      case 'wifiAdd': if (S.wifi && window.KeystoneNative && KeystoneNative.wifiAdd) KeystoneNative.wifiAdd(S.wifi.ssid, S.wifi.pass); break;
      case 'recStop': send({ t: 'recStop' }); setTimeout(function () { send({ t: 'takes' }); }, 1500); break;
      case 'roomToggle':
        var N = native();
        if (!N) { toast('NEEDS THE PHONE APP'); break; }
        if (!N.micAllowed()) { setParam('rec.room', 1); N.askMic(); break; }
        setParam('rec.room', Math.round(val('rec.room')) === 1 ? 0 : 1); render(true); break;
      case 'tapTempo': send({ t: 'tap' }); t.classList.add('hit'); setTimeout(function () { t.classList.remove('hit'); }, 120); break;
      case 'footMode': if (send({ t: 'footMode', m: S.footMode ? 0 : 1 })) { S.footMode = S.footMode ? 0 : 1; render(true); } break;
      case 'rig':
        var nx = rigNext();
        if (nx.kal) { if (send({ t: 'kal', on: true })) { S.kal.on = true; S.screen = 'play'; render(); } }
        else if (kalOn()) { if (send({ t: 'kal', on: false, rig: nx.rig })) { S.kal.on = false; S.rig = nx.rig; render(); } }
        else if (send({ t: 'rig', r: nx.rig })) { S.rig = nx.rig; render(); }
        break;
      case 'kalLoad': if (S.kalSel && send({ t: 'kalLoad', id: S.kalSel })) toast('LOADING'); break;
      case 'kalUnload': if (send({ t: 'kalLoad', id: '' })) { S.kalSel = null; go('play'); } break;
      case 'gigLock': send({ t: 'gigLock', on: t.checked }); break;
      case 'manual': var mi = document.getElementById('manualIp'); if (mi && mi.value.trim()) { S.manualIp = mi.value.trim(); if (window.KeystoneNative && KeystoneNative.connectTo) KeystoneNative.connectTo(S.manualIp); toast('TRYING'); } break;
      case 'prev': if (send({ t: 'songStep', d: -1 }) && S.songs.length) { S.current = Math.max(0, S.current - 1); render(true); } break;
      case 'next': if (send({ t: 'songStep', d: 1 }) && S.songs.length) { S.current = Math.min(S.songs.length - 1, S.current + 1); render(true); } break;
      case 'restartAudio': send({ t: 'restartAudio' }); break;
      case 'bufferFind': send({ t: 'bufferFind' }); break;
      case 'bufferStop': send({ t: 'bufferStop' }); break;
      case 'asioPanel': send({ t: 'asioPanel' }); break;
      case 'light': setParam('output.quality', Math.round(val('output.quality')) === 2 ? 0 : 2); render(true); break;
      case 'note': var nt = document.getElementById('noteText'); if (nt && nt.value.trim()) send({ t: 'keepNote', text: nt.value.trim() }); S.noteUntil = 0; render(); break;
      case 'sheetClose': S.sel = null; sheetEl.hidden = true; render(true); break;
      case 'captures': S.showCaptures = !S.showCaptures; render(); break;
      case 'spaces': S.showSpaces = !S.showSpaces; render(); break;
      case 'droneClear': send({ t: 'drone', op: 'clear' }); break;
      case 'tap': send({ t: 'tap' }); break;
      case 'keeperLive': send({ t: 'keeperLive', on: t.checked }); break;
      case 'add': send({ t: 'setlist', op: 'add' }); break;
      case 'save': send({ t: 'setlist', op: 'save' }); break;
      case 'rename': S.renameRow = i2; S.openRow = -1; render(); var r = document.getElementById('renameText'); if (r) { r.focus(); r.select(); } break;
      case 'renameSave': var rt = document.getElementById('renameText'); if (rt && rt.value.trim()) send({ t: 'setlist', op: 'rename', i: i2, name: rt.value.trim() }); S.renameRow = -1; render(); break;
      case 'copy': send({ t: 'setlist', op: 'copy', i: i2 }); S.openRow = -1; break;
      case 'band': S.confirmBand = true; render(); break;
      case 'bandYes': send({ t: 'setlist', op: 'band' }); S.confirmBand = false; break;
      case 'bandNo': S.confirmBand = false; render(); break;
      case 'addPart': send({ t: 'setlist', op: 'addPart', i: i2 }); S.openRow = -1; break;
      case 'part': send({ t: 'setlist', op: 'part', i: i2 }); S.openRow = -1; break;
      case 'up': if (i2 > 0) { send({ t: 'setlist', op: 'move', i: i2, to: i2 - 1 }); S.openRow = i2 - 1; } break;
      case 'down': if (i2 < S.songs.length - 1) { send({ t: 'setlist', op: 'move', i: i2, to: i2 + 1 }); S.openRow = i2 + 1; } break;
      case 'del': S.confirmDel = i2; S.openRow = -1; render(); break;
      case 'delYes': send({ t: 'setlist', op: 'delete', i: i2 }); S.confirmDel = -1; break;
      case 'delNo': S.confirmDel = -1; render(); break;
    }
  });

  document.addEventListener('pointerdown', function (e) { if (e.target.type === 'range') S.dragging = true; });
  document.addEventListener('pointerup', function () { if (S.dragging && S.pedalDrag == null && !drag) { S.dragging = false; queueRender(true); } });
  document.addEventListener('pointercancel', function () { if (S.pedalDrag == null && !drag) S.dragging = false; });

  document.addEventListener('input', function (e) {
    var t = e.target;
    if (t.id === 'pairCode') {
      S.pairCode = t.value.replace(/\D/g, '').slice(0, 6);
      var tl = t.parentNode.querySelectorAll('.flip');
      for (var k = 0; k < tl.length; k++) { tl[k].textContent = S.pairCode.charAt(k); tl[k].classList.toggle('blue', k === S.pairCode.length); }
      var pb = document.querySelector('[data-act="pairWatch"]'); if (pb) pb.disabled = S.pairCode.length !== 6 || !!S.upd.busy;
      return;
    }
    if (t.dataset && t.dataset.kalrange != null) {
      S.kalDrag = +t.dataset.kalrange;
      kalSet(S.kalDrag, t.value / 1000);
      return;
    }
    if (t.dataset && t.dataset.range) {
      var p = S.params[t.dataset.range];
      var v = fromPos(p, t.value / 1000), st = stepOf(p);
      if (st) v = Math.round(v / st) * st;
      setParam(p.id, v);
      // update labels without rebuilding the slider being dragged
      var f = sheetEl.querySelector('.flip'); if (f && S.sel === p.id) f.textContent = fmt(p.id);
      var lab = t.parentNode.querySelector('.ends span:nth-child(2)'); if (lab) lab.textContent = fmt(p.id);
      var fb = document.querySelector('[data-flip="' + p.id + '"] .flip'); if (fb) fb.textContent = fmt(p.id);
    }
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' && e.target.id === 'renameText') { var b = document.querySelector('[data-act="renameSave"]'); if (b) b.click(); }
    if (e.key === 'Enter' && e.target.id === 'manualIp') { var mb = document.querySelector('[data-act="manual"]'); if (mb) mb.click(); }
    if (e.key === 'Enter' && e.target.id === 'noteText') { var n = document.querySelector('[data-act="note"]'); if (n) n.click(); }
  });

  // Kaleidoscope: drag a knob up or down to turn it, like the plugin's own dials, and press a
  // footswitch tile to work it (held while your finger is down, for its hold job)
  var kd = null, kfHeld = null;
  function kalFootDown(k) {
    var id = S.kal.loaded; if (!id) return;
    var t = kalFeet(id, kalByName())[k]; if (!t) return;
    if (t.press) { send({ t: 'kalSet', i: t.press.i, v: 1 }); kfHeld = t.press; if (t.press.kind !== 'slider' || t.litP === t.press) t.press.v = 1; }
    else if (t.litP) { var nv = t.litP.v > 0.5 ? 0 : 1; t.litP.v = nv; send({ t: 'kalSet', i: t.litP.i, v: nv }); }
    render(true);
  }
  function kalFootUp() {
    if (!kfHeld) return;
    send({ t: 'kalSet', i: kfHeld.i, v: 0 });
    kfHeld.v = 0; kfHeld = null;
    setTimeout(function () { render(true); }, 60);
  }
  document.addEventListener('pointerdown', function (e) {
    var d = e.target.closest && e.target.closest('[data-kdial]');
    if (d) {
      var p = S.kalParams[+d.dataset.kdial]; if (!p) return;
      kd = { el: d, p: p, y: e.clientY, v: p.v };
      S.dragging = true; S.kalDrag = p.i;
      if (d.setPointerCapture) d.setPointerCapture(e.pointerId);
      e.preventDefault();
      return;
    }
    var f = e.target.closest && e.target.closest('[data-kfoot]');
    if (f) { e.preventDefault(); kalFootDown(+f.dataset.kfoot); }
  });
  document.addEventListener('pointermove', function (e) {
    if (!kd) return;
    var p = kd.p, v = Math.max(0, Math.min(1, kd.v + (kd.y - e.clientY) / 220));
    var n = p.choices ? p.choices.length : 0;
    if (n > 1) v = Math.round(v * (n - 1)) / (n - 1);
    if (Math.abs(v - p.v) < 0.0005) return;
    p.v = v;
    var col = pedalMeta(S.kal.loaded).colour;
    var svg = kd.el.querySelector('.ksvg'); if (svg) svg.innerHTML = kalDialSvg(v, col);
    if (n > 1) { var lab = kd.el.querySelector('.kv'); if (lab) lab.textContent = p.choices[Math.round(v * (n - 1))]; }
    kalSet(p.i, v);
  });
  function kalPointerUp() {
    if (kd) { kd = null; S.dragging = false; S.kalDrag = null; queueRender(true); }
    kalFootUp();
  }
  document.addEventListener('pointerup', kalPointerUp);
  document.addEventListener('pointercancel', kalPointerUp);

  // The pedal bar on Play: drag it to do the pedal's job when there's no pedal
  function pedalAt(e) {
    var b = document.getElementById('pedal'); if (!b) return null;
    var bar = b.querySelector('.bar').getBoundingClientRect();
    var x = (e.touches ? e.touches[0].clientX : e.clientX);
    return Math.max(0, Math.min(1, (x - bar.left) / bar.width));
  }
  var pedalSendTimer = null;
  function pedalMove(e) {
    if (S.pedalDrag == null) return;
    var v = pedalAt(e); if (v == null) return;
    e.preventDefault();
    S.pedalDrag = v;
    var bar = document.getElementById('pedalBar'), pct = document.getElementById('pedalPct');
    if (bar) bar.style.width = (v * 100) + '%';
    if (pct) pct.textContent = Math.round(v * 100) + '%';
    if (!pedalSendTimer) pedalSendTimer = setTimeout(function () { pedalSendTimer = null; send({ t: 'pedal', v: S.pedalDrag }); }, 20);
  }
  function pedalDown(e) {
    if (!e.target.closest || !e.target.closest('#pedal')) return;
    S.pedalDrag = pedalAt(e); S.dragging = true; pedalMove(e);
  }
  function pedalUp() {
    if (S.pedalDrag == null) return;
    send({ t: 'pedal', v: S.pedalDrag });
    setTimeout(function () { S.pedalDrag = null; S.dragging = false; }, 150);
  }
  document.addEventListener('touchstart', pedalDown, { passive: false });
  document.addEventListener('touchmove', pedalMove, { passive: false });
  document.addEventListener('touchend', pedalUp);
  document.addEventListener('mousedown', pedalDown);
  document.addEventListener('mousemove', function (e) { if (e.buttons) pedalMove(e); });
  document.addEventListener('mouseup', pedalUp);

  // Setlist: drag the grip to reorder
  var drag = null;
  document.addEventListener('touchstart', function (e) {
    var g = e.target.closest && e.target.closest('[data-grip]'); if (!g) return;
    e.preventDefault();
    drag = { from: +g.dataset.grip, to: +g.dataset.grip };
    S.dragging = true;
    var row = g.closest('.srow'); if (row) row.classList.add('dragging');
  }, { passive: false });
  document.addEventListener('touchmove', function (e) {
    if (!drag) return;
    e.preventDefault();
    var y = e.touches[0].clientY, rows = document.querySelectorAll('.srow');
    for (var k = 0; k < rows.length; k++) {
      var r = rows[k].getBoundingClientRect();
      if (y >= r.top && y <= r.bottom) drag.to = k;
      rows[k].style.borderTopColor = '';
    }
    if (rows[drag.to]) rows[drag.to].style.borderTopColor = drag.to !== drag.from ? '#5DCAA5' : '';
  }, { passive: false });
  document.addEventListener('touchend', function () {
    if (!drag) return;
    if (drag.to !== drag.from) send({ t: 'setlist', op: 'move', i: drag.from, to: drag.to });
    drag = null; S.dragging = false; render();
  });

  // heartbeat so a dead link is noticed quickly
  setInterval(function () { if (S.link === 'connected') send({ t: 'ping', ts: Date.now() }); }, 2000);
  // the Keep note field times out on its own
  setInterval(function () { if (S.noteUntil && Date.now() > S.noteUntil) { S.noteUntil = 0; render(true); } }, 1000);

  // Android back button: step back through the screens, false when already on Play
  function back() {
    if (S.sel) { S.sel = null; sheetEl.hidden = true; render(true); return true; }
    if (S.save) { saveClose(); return true; }
    if (S.screen === 'module') { go('tone'); return true; }
    if (S.screen === 'listen') { go('take'); return true; }
    if (S.screen === 'take') { go('recorder'); return true; }
    if (S.screen === 'recorder') { go('play'); return true; }
    if (S.screen !== 'play') { go('play'); return true; }
    return false;
  }

  loadOffline();
  setInterval(function () { if (connected()) strip(); }, 1000);
  render();
  return { onMessage: onMessage, onLink: onLink, back: back, state: S, refresh: function () { render(true); }, onUpdate: onUpdate };
})();
