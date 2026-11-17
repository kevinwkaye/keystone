/* Keystone phone controls.
   Talks to the Keystone app through window.KeystoneNative (the Android shell finds Keystone
   on the hotspot and carries the JSON lines). Every Tone page is built from the parameter
   list Keystone sends, so new controls appear here without a phone update. */
'use strict';

var KS = (function () {
  var S = {
    link: 'searching', linkDetail: '',
    schema: null, params: {}, modules: [], values: {},
    songs: [], parts: [], current: 0, edited: false, maxSongs: 15, maxEntries: 40, bank: 0, rig: 0, footMode: 0,
    tuner: false, captures: [], capture: '', captureFile: '', namAvailable: true, spaces: [], space: '', spaceFile: '',
    footswitch: '', audio: '', keeperLive: false,
    meter: {}, screen: 'play', module: null, prevScreen: 'play', tunerAuto: false,
    editVoice: null, sel: null, openRow: -1, renameRow: -1, confirmDel: -1,
    noteFor: 0, noteUntil: 0, pedalDrag: null,
    health: null, rtt: null, healthAt: 0
  };

  // ---------------------------------------------------------------- transport
  var NAV_ONLY = { hello: 1, ping: 1, tuner: 1 };
  function connected() { return S.link === 'connected'; }
  function send(o) {
    if (!connected() && !NAV_ONLY[o.t]) { toast('NOT CONNECTED'); return false; }
    var line = JSON.stringify(o);
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
        S.namAvailable = m.namAvailable; S.footswitch = m.footswitch; S.audio = m.audio; S.keeperLive = m.keeperLive;
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
        queueMeters(); break;
      case 'toast':
        toast(m.text);
        if (m.text === 'KEPT') { S.noteUntil = Date.now() + 20000; nod(); render(true); }
        break;
    }
  }

  function onLink(status, detail) {
    if (status !== 'connected') { S.health = null; S.rtt = null; }
    S.link = status; S.linkDetail = detail || '';
    if (status === 'connected') send({ t: 'hello', client: 'android', v: 1 });
    render(true);
  }

  function useSchema(m) {
    S.schema = m; S.params = {}; S.modules = m.modules;
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
  var PEBBLE = '<svg class="pebble" id="pebble" width="44" height="44" viewBox="0 0 200 200" role="img" aria-label="Pebble">' +
    '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#2329A8"/>' +
    '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#4B57FF"/>' +
    '<g class="eye"><circle cx="78" cy="80" r="18" fill="#FFFFFF"/><circle cx="80" cy="82" r="9" fill="#0B0B12"/></g>' +
    '<g class="eye"><circle cx="122" cy="80" r="18" fill="#FFFFFF"/><circle cx="124" cy="82" r="9" fill="#0B0B12"/></g>' +
    '<path d="M88 116 Q 100 126, 112 116" fill="none" stroke="#0B0B12" stroke-width="6" stroke-linecap="round"/></svg>';

  var PEBBLE_K = '<svg class="pebble" id="pebble" width="44" height="44" viewBox="0 0 200 200" role="img" aria-label="Pebble in shades and a pork pie hat">' +
    '<path transform="translate(7 9)" d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#B8661E"/>' +
    '<path d="M100 22 C 146 20, 176 54, 174 100 C 172 146, 140 176, 98 174 C 54 172, 24 142, 26 98 C 28 56, 58 24, 100 22 Z" fill="#F28C28"/>' +
    '<rect x="56" y="72" width="40" height="22" rx="8" fill="#141110"/><rect x="104" y="72" width="40" height="22" rx="8" fill="#141110"/><rect x="94" y="78" width="12" height="5" fill="#141110"/>' +
    '<path d="M88 120 Q 100 130, 112 120" fill="none" stroke="#141110" stroke-width="6" stroke-linecap="round"/>' +
    '<g transform="rotate(-9 100 52)"><ellipse cx="100" cy="52" rx="52" ry="9" fill="#4A2E1B"/><path d="M70 52 L74 24 Q100 17 126 24 L130 52Z" fill="#6B4429"/>' +
    '<path d="M71.5 40 Q100 35 128.5 40 L129.5 50 Q100 46 70.5 50Z" fill="#F6EBDD"/><path d="M118 40 Q132 14 146 10 Q136 26 124 42Z" fill="#F28C28"/></g></svg>';
  // which parameter switches a module on and off (Kuddle's are named their own way)
  var KON = { 'k.mute': 'kuddle.muteOn', 'k.wah': 'kuddle.wahOn', 'k.opto': 'kuddle.optoOn', 'k.tape': 'kuddle.tapeOn', 'k.tone': 'kuddle.toneOn', 'k.room': 'kuddle.roomOn' };
  function onp(mod) { return KON[mod] || (S.params[mod + '.on'] ? mod + '.on' : ''); }
  function kud() { return S.rig === 1; }

  function pill() {
    var c = S.link === 'connected', w = warnings().length;
    var label = !c ? 'Searching' : w ? w + (w > 1 ? ' problems' : ' problem') : 'Connected';
    return '<button type="button" class="pill" data-go="health" style="border:none" aria-label="Open Health"><span class="dot ' + (!c ? 'searching' : w ? 'warn' : '') + '"></span>' + label + '</button>';
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
    if (h.laptop.cpu > 80) out.push('Laptop CPU at ' + h.laptop.cpu + '%');
    if (S.meter && S.meter.lat > 16) out.push('Round trip ' + S.meter.lat + ' ms: too slow to play through. ' + (Math.round(val('output.mode')) === 2 ? 'Tap ASIO panel on Health and set the buffer to 128' : 'Set the audio buffer lower'));
    if (S.healthAt && Date.now() - S.healthAt > 5000) out.push('Keystone has gone quiet: no news for ' + Math.round((Date.now() - S.healthAt) / 1000) + ' s');
    return out;
  }
  function stripText(w) { return w[0] + (w.length > 1 ? ' (+' + (w.length - 1) + ' more)' : ''); }
  function strip() {
    var el = document.getElementById('warnStrip'), w = warnings();
    var pl = document.querySelector('.pill');
    if (pl) { var np = pill(); if (pl.outerHTML !== np) pl.outerHTML = np; }
    if (S.screen !== 'play' || !w.length) { if (el) el.remove(); return; }
    var html = '<button type="button" id="warnStrip" data-go="health" class="warnstrip">' + esc(stripText(w)) + '</button>';
    if (el) { if (el.textContent !== stripText(w)) el.outerHTML = html; }
    else { var top = document.querySelector('.screen .top'); if (top) top.insertAdjacentHTML('afterend', html); }
  }
  function ago(sec) { return sec < 0 ? 'never' : sec < 60 ? sec + ' s ago' : Math.round(sec / 60) + ' min ago'; }
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
      rows.push({ n: 'Laptop', d: 'CPU ' + h.laptop.cpu + '%, ' + (h.laptop.dropouts ? h.laptop.dropouts + ' dropouts, ' + h.laptop.recent + ' in the last minute' : 'no dropouts') + (h.laptop.light ? ', effects light' : '') + ', up ' + Math.round(h.uptime / 60) + ' min', ok: h.laptop.cpu <= 80 && h.laptop.recent === 0 });
      rows.push({ n: 'Phone link', d: (S.linkDetail || '') + (S.rtt != null ? ', ' + S.rtt + ' ms round trip' : ''), ok: true });
    }
    var inDb = h ? h['in'].db : null;
    var frac = inDb == null ? 0 : Math.max(0, Math.min(1, (inDb + 60) / 60));
    var inCol = inDb == null || inDb < -50 ? '#8A847A' : inDb > -1 ? o : g;
    return '<section class="screen" style="gap:14px">' + header('Health', { id: 'play', label: 'Play' }, head) +
      '<section class="card" style="gap:10px"><h2 class="label">Bass in</h2><div class="bar" style="height:14px"><div style="width:' + (frac * 100) + '%;background:' + inCol + '"></div></div>' +
      '<div style="display:flex;justify-content:space-between;font-size:14px"><span>' + esc(inputText()) + '</span><span class="muted">' + (h ? 'Input ' + h['in'].ch : '') + '</span></div></section>' +
      '<section class="card" style="padding:6px 16px;gap:0">' + rows.map(function (r) {
        var col = r.ok ? g : o;
        return '<div style="display:flex;align-items:center;gap:12px;min-height:54px;border-bottom:1px solid #262626"><span style="width:10px;height:10px;border-radius:50%;background:' + col + ';flex:none"></span>' +
          '<span style="display:flex;flex-direction:column;flex:1;min-width:0"><span style="font-size:17px;font-weight:500">' + esc(r.n) + '</span><span class="small" style="font-size:13px">' + esc(r.d) + '</span></span>' +
          '<span style="font-size:13px;font-weight:700;color:' + col + '">' + (r.st || (r.ok ? 'OK' : 'CHECK')) + '</span></div>';
      }).join('') + '</section>' +
      (Math.round(val('output.mode')) === 2 ? '<button type="button" class="wide" data-act="asioPanel">ASIO panel (opens on the laptop)</button>' : '') +
      '<div class="pair"><button type="button" class="wide" data-act="restartAudio">Restart audio</button><button type="button" class="wide" data-act="light">' + (Math.round(val('output.quality')) === 2 ? 'Effects full' : 'Effects light') + '</button></div>' +
      '<div class="small">If something goes wrong on stage, a warning strip appears across the top of Play. Tap it to come here.</div>' +
      nav('play') + '</section>';
  }
  function nav(active) {
    function b(id, label) { return '<button type="button" data-go="' + id + '" class="' + (active === id ? 'on' : '') + '">' + label + '</button>'; }
    return '<nav class="nav">' + b('play', 'Play') + b('tuner', 'Tuner') + b('tone', 'Tone') +
      '<button type="button" class="keep" data-act="keep">Keep</button></nav>';
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
  var JOBS = ['Dirt blend', 'Wah', 'Volume', 'Bloom'];
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
    var m = S.meter, job = m.job != null ? m.job : [0, on('wah.on') ? 1 : 2, 3][S.bank];
    var zones = job === 0 ? BANKS[0].zones : job === 3 ? BANKS[2].zones : ['Heel', '', 'Toe'];
    var v = S.pedalDrag != null ? S.pedalDrag : (m.jobv != null ? m.jobv : job === 3 ? (m.bloom || 0) : 1);
    var text = !m.jobOn && job !== 2 ? 'Saved sound, move the pedal' : m.wait ? 'Waiting: pedal at ' + Math.round((m.pedal || 0) * 100) + '%' : Math.round(v * 100) + '%';
    return { job: job, name: JOBS[job], zones: zones, v: v, text: text };
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
    var offline = S.link !== 'connected' ? '<div class="card"><span style="font-size:17px;font-weight:700">Looking for Keystone</span>' +
      '<span class="small">Turn on this phone\'s hotspot and switch on the laptop. Keystone joins the hotspot by itself and this screen connects within a few seconds. No internet or mobile data needed.</span>' +
      (S.linkDetail ? '<span class="small" style="color:#F2EDE4">' + esc(S.linkDetail) + '</span>' : '') +
      '<div class="note"><input id="manualIp" inputmode="decimal" placeholder="Laptop address, e.g. 10.42.0.17" aria-label="Laptop address" value="' + esc(S.manualIp || '') + '">' +
      '<button type="button" data-act="manual">Connect</button></div>' +
      '<span class="small">Type the laptop\'s address, not the phone\'s. Your hotspot settings list the laptop under Connected devices, and Keystone writes its address in Documents\\Keystone\\keystone.log.</span></div>' : '';
    var note = Date.now() < S.noteUntil ? '<div class="note"><input id="noteText" maxlength="200" placeholder="Add a note to that Keep (optional)" aria-label="Note for the last Keep"><button type="button" data-act="note">Save</button></div>' : '';
    return '<section class="screen">' +
      '<div class="top"><button type="button" class="brand rig" data-act="rig" aria-label="Switch to ' + (K ? 'Keystone' : 'Kuddle') + '">' + (K ? PEBBLE_K : PEBBLE) +
      '<span style="display:flex;flex-direction:column;align-items:flex-start;gap:3px"><span class="title">' + (K ? 'Kuddle' : 'Keystone') + '</span><span class="swap">TAP FOR ' + (K ? 'KEYSTONE' : 'KUDDLE') + '</span></span></button>' + pill() + '</div>' +
      offline +
      '<div class="songbar"><button type="button" class="arrow" data-act="prev" aria-label="Previous song">' + ICON.left + '</button>' +
      '<button type="button" class="song" data-go="setlist" aria-label="Open the setlist">' + flip(n ? two(songNo(i)) : '--', 'blue') +
      '<span style="display:flex;flex-direction:column;min-width:0"><span class="name">' + esc(song) + (part ? '<span class="partname">, ' + esc(part) + '</span>' : '') + '</span><span class="pos">' + (K ? 'Preset ' : 'Song ') + songNo(i) + ' of ' + n +
      (S.edited ? ', <span class="edited">edited</span>' : K ? '. Hold B for next.' : S.footMode === 1 ? '. C for next song.' : '. Hold B for next.') + '</span></span></button>' +
      '<button type="button" class="arrow" data-act="next" aria-label="Next song">' + ICON.right + '</button></div>' +
      '<div class="card meter">' + meterSvg() + '<div class="label">Output</div></div>' +
      (K ? '' : modeBar() + (S.footMode === 1 ? '' : bankBar())) +
      '<div class="stomps">' + stomps + '</div>' + note +
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
    var pts = [[-40, 180], [-20, 180], [-10, 135], [-5, 90], [0, 30], [3, 0]];
    if (db <= -20) return 180;
    for (var i = 1; i < pts.length; i++) if (db <= pts[i][0]) {
      var a = pts[i - 1], b = pts[i], t = (db - a[0]) / (b[0] - a[0]);
      return a[1] + (b[1] - a[1]) * t;
    }
    return 0;
  }
  function needle(angle) {
    var r = angle * Math.PI / 180;
    return { x: 155 + 110 * Math.cos(r), y: 140 - 110 * Math.sin(r) };
  }
  function meterSvg() {
    var p = needle(dbAngle(S.meter.out != null ? S.meter.out : -60));
    return '<svg width="310" height="150" viewBox="0 0 310 150" role="img" aria-label="Output level meter">' +
      '<path d="M35 140 A 120 120 0 0 1 259 80" fill="none" stroke="#F2EDE4" stroke-width="2"/>' +
      '<path d="M259 80 A 120 120 0 0 1 275 140" fill="none" stroke="#F0997B" stroke-width="6"/>' +
      '<line x1="35" y1="140" x2="23" y2="140" stroke="#F2EDE4" stroke-width="2"/><line x1="70" y1="55" x2="62" y2="47" stroke="#F2EDE4" stroke-width="2"/>' +
      '<line x1="155" y1="20" x2="155" y2="8" stroke="#F2EDE4" stroke-width="2"/><line x1="259" y1="80" x2="269" y2="74" stroke="#F0997B" stroke-width="2"/>' +
      '<text x="22" y="128" font-size="13" fill="#B9B2A6" text-anchor="middle">-20</text><text x="54" y="44" font-size="13" fill="#B9B2A6" text-anchor="middle">-10</text>' +
      '<text x="155" y="6" font-size="13" fill="#B9B2A6" text-anchor="middle" dominant-baseline="hanging">-5</text><text x="276" y="68" font-size="13" fill="#F0997B" text-anchor="middle">0</text>' +
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
  function updateTuner() {
    var v = tunerView(), q = function (id) { return document.getElementById(id); };
    if (!q('tnote')) return;
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
      '<div class="top"><span class="title">Tuner</span><div class="pill"><span class="dot" style="background:' + (muted ? '#F0997B' : '#5DCAA5') + '"></span>' + (muted ? 'Output muted' : 'Output live') + '</div></div>' +
      '<div class="tnote"><span class="flip big" id="tnote">' + esc(valid ? t.note.replace('#', '\u266F') : '-') + '</span><span class="flip oct" id="toct">' + esc(valid ? t.oct : ' ') + '</span></div>' +
      '<div class="card" style="align-items:center;gap:4px;padding:16px"><svg width="310" height="150" viewBox="0 0 310 150" role="img" aria-label="Tuning needle">' +
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
      '<div class="small">Reference A = ' + Math.round(val('tuner.ref')) + ' Hz. Hold C, or tap any footswitch, to leave the tuner.</div>' +
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
    var rest = S.modules.filter(function (m) { return !seen[m.id] && m.id.indexOf('k.') !== 0; });   // anything new still shows
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
        }).join('') : '<span class="small">No .nam files found in Documents\\Keystone\\Captures on the laptop.</span>') + '</div>' : '') +
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
        }).join('') : '<span class="small">No .wav files found in Documents\\Keystone\\Spaces on the laptop.</span>') + '</div>' : '') + '</section>';
      var rvHtml = renderParams(list);
      var rvCut = rvHtml.indexOf('</section>') + '</section>'.length;
      pre = rvHtml.slice(0, rvCut) + spCard;
      list = null; extra = rvHtml.slice(rvCut);
    }
    if (mod === 'pedal') {
      pre = '<section class="card"><h2 class="label">One pedal, a job per bank</h2><span class="small">Grit: dirt blend. Heel is clean, the drive is fully in by halfway and Bitrot joins from Bitrot joins at up to the toe. It only blends dirt you have switched on.</span>' +
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
                  ['C', 'Grit: Growl/Cut flip. Groove: wah. Glow: reverb', 'Tuner. Song mode: next song, on the press'], ['D', 'Drone hold / trail off, in every bank', 'Slap']];
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

  // ---------------------------------------------------------------- render
  var app = document.getElementById('app');
  var lastScreenKey = '';
  function render(soft) {
    if (!S.schema && S.screen !== 'play' && S.screen !== 'health') S.screen = 'play';
    var active = document.activeElement;
    if (soft && active && active.tagName === 'INPUT' && active.type === 'text') return;   // don't wipe typing
    if (soft && S.dragging) return;
    var html;
    switch (S.screen) {
      case 'tuner': html = tunerScreen(); break;
      case 'tone': html = S.schema ? toneScreen() : playScreen(); break;
      case 'module': html = S.schema ? moduleScreen() : playScreen(); break;
      case 'setlist': html = setlistScreen(); break;
      case 'health': html = healthScreen(); break;
      default: html = playScreen();
    }
    document.body.classList.toggle('kuddle', kud());
    var key = S.screen + (S.module || '');
    var y = window.scrollY;
    app.innerHTML = html;
    if (key === lastScreenKey) window.scrollTo(0, y); else window.scrollTo(0, 0);
    lastScreenKey = key;
    if (S.sel) sheet();
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
    S.screen = screen; S.sel = null; sheetEl.hidden = true; S.openRow = -1; S.renameRow = -1; S.confirmDel = -1; S.showCaptures = false; S.showSpaces = false;
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
    var i2 = d.i != null ? +d.i : -1;
    switch (d.act) {
      case 'keep': send({ t: 'keep' }); break;
      case 'tapTempo': send({ t: 'tap' }); t.classList.add('hit'); setTimeout(function () { t.classList.remove('hit'); }, 120); break;
      case 'footMode': if (send({ t: 'footMode', m: S.footMode ? 0 : 1 })) { S.footMode = S.footMode ? 0 : 1; render(true); } break;
      case 'rig': if (send({ t: 'rig', r: kud() ? 0 : 1 })) { S.rig = kud() ? 0 : 1; render(); } break;
      case 'manual': var mi = document.getElementById('manualIp'); if (mi && mi.value.trim()) { S.manualIp = mi.value.trim(); if (window.KeystoneNative && KeystoneNative.connectTo) KeystoneNative.connectTo(S.manualIp); toast('TRYING'); } break;
      case 'prev': if (send({ t: 'songStep', d: -1 }) && S.songs.length) { S.current = Math.max(0, S.current - 1); render(true); } break;
      case 'next': if (send({ t: 'songStep', d: 1 }) && S.songs.length) { S.current = Math.min(S.songs.length - 1, S.current + 1); render(true); } break;
      case 'restartAudio': send({ t: 'restartAudio' }); break;
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
    if (S.screen === 'module') { go('tone'); return true; }
    if (S.screen !== 'play') { go('play'); return true; }
    return false;
  }

  loadOffline();
  setInterval(function () { if (connected()) strip(); }, 1000);
  render();
  return { onMessage: onMessage, onLink: onLink, back: back, state: S };
})();
