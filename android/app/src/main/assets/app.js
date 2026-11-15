/* Keystone phone controls.
   Talks to the Keystone app through window.KeystoneNative (the Android shell finds Keystone
   on the hotspot and carries the JSON lines). Every Tone page is built from the parameter
   list Keystone sends, so new controls appear here without a phone update. */
'use strict';

var KS = (function () {
  var S = {
    link: 'searching', linkDetail: '',
    schema: null, params: {}, modules: [], values: {},
    songs: [], current: 0, edited: false, maxSongs: 15,
    tuner: false, captures: [], capture: '', captureFile: '', namAvailable: true,
    footswitch: '', audio: '', keeperLive: false,
    meter: {}, screen: 'play', module: null, prevScreen: 'play', tunerAuto: false,
    editVoice: null, sel: null, openRow: -1, renameRow: -1, confirmDel: -1,
    noteFor: 0, noteUntil: 0, bloomDrag: null
  };

  // ---------------------------------------------------------------- transport
  function send(o) {
    var line = JSON.stringify(o);
    if (window.KeystoneNative) window.KeystoneNative.send(line);
    else if (window.KSMock) window.KSMock.send(line);
  }
  var pendingSet = {}, setTimer = null;
  function setParam(id, v) {          // throttled while dragging
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
        S.schema = m; S.params = {}; S.modules = m.modules;
        m.params.forEach(function (p) { S.params[p.id] = p; });
        render(); break;
      case 'state':
        S.values = m.values; S.songs = m.songs; S.current = m.current; S.edited = m.edited;
        S.maxSongs = m.maxSongs; S.captures = m.captures || []; S.capture = m.capture; S.captureFile = m.captureFile;
        S.namAvailable = m.namAvailable; S.footswitch = m.footswitch; S.audio = m.audio; S.keeperLive = m.keeperLive;
        tunerState(m.tuner);
        render(); break;
      case 'val':
        if (pendingSet[m.id] === undefined) S.values[m.id] = m.v;
        S.edited = m.edited;
        render(true); break;
      case 'meter':
        S.meter = m;
        if (m.tuner) tunerState(true); else if (S.tuner) tunerState(false);
        meters(); break;
      case 'toast':
        toast(m.text);
        if (m.text === 'KEPT') { S.noteUntil = Date.now() + 20000; nod(); render(true); }
        break;
    }
  }

  function onLink(status, detail) {
    S.link = status; S.linkDetail = detail || '';
    if (status === 'connected') send({ t: 'hello', client: 'android', v: 1 });
    render(true);
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
    if (/gate$/.test(id) && v <= -89) return 'Off';
    switch (p.unit) {
      case '%': return trim(v * 100, 0) + '%';
      case 'Hz': return v >= 1000 ? trim(v / 1000, 1) + 'k' : trim(v, 0);
      case 'ms': return trim(v, 0) + 'ms';
      case 's': return trim(v, v < 10 ? 1 : 0) + 's';
      case 'dB': return (v > 0.05 ? '+' : '') + trim(v, Math.abs(v) < 10 ? 1 : 0);
      case '/s': return trim(v, 0) + '/s';
    }
    return trim(v, Math.abs(v) < 10 ? 1 : 0);
  }
  // slider position 0..1 <-> value, log where the parameter says so
  function toPos(p, v) { return p.log ? Math.log(v / p.min) / Math.log(p.max / p.min) : (v - p.min) / (p.max - p.min); }
  function fromPos(p, x) { x = Math.max(0, Math.min(1, x)); return p.log ? p.min * Math.pow(p.max / p.min, x) : p.min + (p.max - p.min) * x; }
  function stepOf(p) {
    if (p.unit === '%') return 0.01;
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

  function pill() {
    var c = S.link === 'connected';
    return '<div class="pill"><span class="dot ' + (c ? '' : 'searching') + '"></span>' + (c ? 'Connected' : 'Searching') + '</div>';
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

  // ---------------------------------------------------------------- screens
  function playScreen() {
    var n = S.songs.length, i = S.current;
    var song = S.songs[i] || 'No songs yet';
    var voice = Math.round(val('drive.voice')), driveOn = on('drive.on');
    var layers = S.meter.drone || 0;
    var tiles = [
      { k: 'A', name: 'Drive', st: !driveOn ? 'OFF' : voice ? 'DRIVE' : 'EDGE', col: !driveOn ? '#8A847A' : voice ? '#F0997B' : '#5DCAA5', border: !driveOn ? '#262626' : voice ? '#F0997B' : '#5DCAA5' },
      { k: 'B', name: 'Bitrot', st: on('bitrot.on') ? 'ON' : 'OFF' },
      { k: 'C', name: 'Filter', st: on('filter.on') ? 'ON' : 'OFF' },
      { k: 'D', name: 'Drone', st: layers ? 'HOLDING' + (layers > 1 ? ' ' + layers : '') : 'READY' }
    ];
    var stomps = tiles.map(function (t) {
      var lit = t.st !== 'OFF' && t.st !== 'READY';
      var col = t.col || (lit ? '#5DCAA5' : '#8A847A'), border = t.border || (lit ? '#5DCAA5' : '#262626');
      return '<button type="button" class="stomp" data-stomp="' + t.k + '" style="border-color:' + border + '">' +
        '<span class="head">' + flip(t.k) + '<span class="nm">' + t.name + '</span></span>' +
        '<span class="st" style="color:' + col + '">' + t.st + '</span></button>';
    }).join('');
    var bl = S.bloomDrag != null ? S.bloomDrag : (S.meter.bloom || 0);
    var offline = S.link !== 'connected' ? '<div class="card"><span style="font-size:17px;font-weight:700">Looking for Keystone</span>' +
      '<span class="small">Turn on this phone\'s hotspot and switch on the laptop. Keystone joins the hotspot by itself and this screen connects within a few seconds. No internet or mobile data needed.</span>' +
      (S.linkDetail ? '<span class="small" style="color:#F2EDE4">' + esc(S.linkDetail) + '</span>' : '') +
      '<div class="note"><input id="manualIp" inputmode="decimal" placeholder="Laptop address, e.g. 10.42.0.17" aria-label="Laptop address" value="' + esc(S.manualIp || '') + '">' +
      '<button type="button" data-act="manual">Connect</button></div>' +
      '<span class="small">Type the laptop\'s address, not the phone\'s. Your hotspot settings list the laptop under Connected devices, and Keystone writes its address in Documents\\Keystone\\keystone.log.</span></div>' : '';
    var note = Date.now() < S.noteUntil ? '<div class="note"><input id="noteText" maxlength="200" placeholder="Add a note to that Keep (optional)" aria-label="Note for the last Keep"><button type="button" data-act="note">Save</button></div>' : '';
    return '<section class="screen">' +
      '<div class="top"><div class="brand">' + PEBBLE + '<span class="title">Keystone</span></div>' + pill() + '</div>' +
      offline +
      '<div class="songbar"><button type="button" class="arrow" data-act="prev" aria-label="Previous song">' + ICON.left + '</button>' +
      '<button type="button" class="song" data-go="setlist" aria-label="Open the setlist">' + flip(n ? two(i + 1) : '--', 'blue') +
      '<span style="display:flex;flex-direction:column;min-width:0"><span class="name">' + esc(song) + '</span><span class="pos">Song ' + (i + 1) + ' of ' + n +
      (S.edited ? ', <span class="edited">edited</span>' : '. Hold A or B to step.') + '</span></span></button>' +
      '<button type="button" class="arrow" data-act="next" aria-label="Next song">' + ICON.right + '</button></div>' +
      '<div class="card meter">' + meterSvg() + '<div class="label">Output</div></div>' +
      '<div class="stomps">' + stomps + '</div>' + note +
      '<div class="card bloom" id="bloom"><div class="label" style="display:flex;justify-content:space-between"><span>Bloom</span><span id="bloomPct">Expression ' + Math.round(bl * 100) + '%</span></div>' +
      '<div class="bar"><div id="bloomBar" style="width:' + (bl * 100) + '%"></div></div>' +
      '<div class="zones"><span>Drone</span><span>Grains</span><span>Wash</span></div></div>' +
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
      '<circle cx="155" cy="140" r="9" fill="#4B57FF"/></svg>';
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
      '<div class="tnote">' + flip(valid ? t.note.replace('#', '♯') : '-', 'big') + flip(valid ? t.oct : ' ', 'oct') + '</div>' +
      '<div class="card" style="align-items:center;gap:4px;padding:16px"><svg width="310" height="150" viewBox="0 0 310 150" role="img" aria-label="Tuning needle">' +
      '<path d="M35 140 A 120 120 0 0 1 275 140" fill="none" stroke="#3A3A40" stroke-width="2"/>' +
      '<path d="M135 22 A 120 120 0 0 1 175 22" fill="none" stroke="#5DCAA5" stroke-width="8"/><line x1="155" y1="20" x2="155" y2="4" stroke="#5DCAA5" stroke-width="3"/>' +
      '<line x1="35" y1="140" x2="23" y2="140" stroke="#F0997B" stroke-width="2"/><line x1="275" y1="140" x2="287" y2="140" stroke="#F0997B" stroke-width="2"/>' +
      '<line x1="70" y1="55" x2="62" y2="47" stroke="#8A847A" stroke-width="2"/><line x1="240" y1="55" x2="248" y2="47" stroke="#8A847A" stroke-width="2"/>' +
      '<text x="30" y="126" font-size="13" fill="#F0997B" text-anchor="middle">Flat</text><text x="280" y="126" font-size="13" fill="#F0997B" text-anchor="middle">Sharp</text>' +
      '<line id="tneedle" x1="155" y1="140" x2="' + p.x.toFixed(1) + '" y2="' + p.y.toFixed(1) + '" stroke="' + (inTune ? '#5DCAA5' : '#F2EDE4') + '" stroke-width="3" stroke-linecap="round"/>' +
      '<circle cx="155" cy="140" r="9" fill="#4B57FF"/></svg>' +
      '<div class="cents" style="color:' + (inTune ? '#5DCAA5' : '#F2EDE4') + '">' + (valid ? (cents > 0 ? '+' : '') + Math.round(cents) + ' cents' : '--') + '</div>' +
      '<div class="small">' + msg + '</div></div>' +
      '<div style="display:flex;flex-direction:column;gap:10px"><div class="label">Tuning</div><div class="seg" style="grid-template-columns:repeat(2,minmax(0,1fr))">' +
      '<button type="button" class="' + (drop ? '' : 'on') + '" data-set="drone.tuning" data-v="0" style="min-height:52px;font-size:17px">Standard E</button>' +
      '<button type="button" class="' + (drop ? 'on' : '') + '" data-set="drone.tuning" data-v="1" style="min-height:52px;font-size:17px">Drop D</button></div></div>' +
      '<label class="switch">Mute output while tuning<input type="checkbox" data-toggle="tuner.mute"' + (muted ? ' checked' : '') + '></label>' +
      '<div class="small">Reference A = ' + Math.round(val('tuner.ref')) + ' Hz. Hold C, or tap any footswitch, to leave the tuner.</div>' +
      nav('tuner') + '</section>';
  }

  function moduleState(m) {
    var id = m.id;
    if (id === 'drone') return { s: (S.meter.drone ? 'HOLDING' : on('drone.on') ? 'READY' : 'OFF') };
    if (id === 'drive') return { s: !on('drive.on') ? 'OFF' : Math.round(val('drive.voice')) ? 'DRIVE' : 'EDGE' };
    if (id === 'bloom') return { s: 'EXP' };
    if (id === 'footswitches') return { s: 'A to D' };
    if (id === 'output') return { s: on('output.limit') ? 'ON' : 'NO LIMIT' };
    if (id === 'split') return { s: 'ON' };
    var p = S.params[id + '.on'];
    return { s: p ? (on(id + '.on') ? 'ON' : 'OFF') : '' };
  }
  function summary(m) {
    switch (m.id) {
      case 'split': return 'Crossover ' + fmt('split.freq') + ' Hz, tight shelf';
      case 'lowcomp': return on('lowcomp.slap') ? 'Slap mode on' : 'Light, slap mode ready';
      case 'drive': return 'Edge and Drive voices, ' + (Math.round(val('drive.source')) === 0 && S.capture ? 'capture' : 'model');
      case 'dim': return 'Mode ' + fmt('dim.mode') + ', mono safe';
      case 'grains': return fmt('grains.pitch') + ', high-pass ' + fmt('grains.hp');
      case 'delay': return fmt('delay.time') + ', ' + fmt('delay.feedback') + ' repeats';
      case 'reverb': return fmt('reverb.decay') + ', shimmer ' + fmt('reverb.shimmer');
      case 'preamp': return 'Blend ' + Math.round(val('preamp.blend') * 100) + '% Cut, mid ' + fmt('preamp.midFreq');
    }
    return m.summary;
  }

  function toneScreen() {
    var groups = {}, order = [];
    S.modules.forEach(function (m) { if (!groups[m.group]) { groups[m.group] = []; order.push(m.group); } groups[m.group].push(m); });
    var html = order.map(function (g) {
      return '<section class="group"><h2 class="label">' + esc(g) + '</h2>' + groups[g].map(function (m) {
        var st = moduleState(m).s;
        var col = st === 'OFF' || st === 'NO LIMIT' ? '#8A847A' : '#5DCAA5';
        return '<button type="button" class="row" data-module="' + m.id + '"><span><span class="t">' + esc(m.title) + '</span><span class="s">' + esc(summary(m)) + '</span></span>' +
          '<span class="r"><span style="color:' + col + '">' + st + '</span>' + ICON.chev + '</span></button>';
      }).join('') + '</section>';
    }).join('');
    return '<section class="screen" style="gap:14px"><div class="top"><span class="title">Tone</span><span class="small">Song: ' + esc(S.songs[S.current] || '') + (S.edited ? ' <span class="edited">edited</span>' : '') + '</span></div>' +
      html + nav('tone') + '</section>';
  }

  // ---- generic module page, laid out like the canvas's Drive and Grains pages
  function paramsOf(mod) {
    var list = [];
    S.schema.params.forEach(function (p) { if (p.module === mod && !p.hidden) list.push(p); });
    return list;
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
          var cols = Math.min(4, p.choices.length), cur = Math.round(val(p.id));
          var showLabel = ps.length > 1 && p.label !== sec;
          body += (showLabel ? '<div class="small">' + esc(p.label) + '</div>' : '') +
            '<div class="seg" style="grid-template-columns:repeat(' + cols + ',minmax(0,1fr))">' + p.choices.map(function (c, i) {
              return '<button type="button" class="' + (i === cur ? 'on' : '') + '" data-set="' + p.id + '" data-v="' + i + '">' + esc(c) + '</button>';
            }).join('') + '</div>';
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
      return '<section class="card"><h2 class="label">' + esc(sec) + '</h2>' + body + '</section>';
    }).join('');
  }

  function moduleScreen() {
    var mod = S.module, m = S.modules.filter(function (x) { return x.id === mod; })[0] || { title: mod };
    var list = paramsOf(mod), extra = '', pre = '';
    var hasOn = S.params[mod + '.on'];
    var right = hasOn ? '<label class="onToggle">On<input type="checkbox" data-toggle="' + mod + '.on"' + (on(mod + '.on') ? ' checked' : '') + '></label>' : '<span style="width:44px"></span>';

    if (mod === 'drive') {
      // the voice being edited is the voice playing; its settings show below
      var voice = Math.round(val('drive.voice'));
      var pfx = voice ? 'drive.drive.' : 'drive.edge.';
      list = list.filter(function (p) { return !/^drive\.(edge|drive)\./.test(p.id) || p.id.indexOf(pfx) === 0; });
      var src = Math.round(val('drive.source'));
      var capName = S.capture ? S.capture : (S.namAvailable ? 'No capture loaded' : 'Captures not supported');
      var capSub = S.capture ? 'NAM, loaded from your captures' : 'Put .nam files in Documents\\Keystone\\Captures';
      // source section: the capture row sits above Capture / Model
      var srcHtml = '<section class="card"><h2 class="label">Source</h2><button type="button" class="source" data-act="captures"><span><span class="t">' + esc(capName) + '</span><span class="s">' + esc(capSub) + '</span></span><span style="font-size:14px;color:#5DCAA5">Change</span></button>' +
        (S.showCaptures ? '<div class="group">' + (S.captures.length ? S.captures.map(function (f) {
          return '<button type="button" class="row" data-capture="' + esc(f) + '" style="min-height:48px;background:#262626"><span class="t" style="font-size:16px">' + esc(f.replace(/\.nam$/i, '')) + '</span><span class="r" style="color:#5DCAA5">' + (f === S.captureFile ? 'LOADED' : '') + '</span></button>';
        }).join('') : '<span class="small">No .nam files found in Documents\\Keystone\\Captures on the laptop.</span>') + '</div>' : '') +
        '<div class="seg" style="grid-template-columns:repeat(2,minmax(0,1fr))"><button type="button" class="' + (src === 0 ? 'on' : '') + '" data-set="drive.source" data-v="0">Capture</button><button type="button" class="' + (src === 1 ? 'on' : '') + '" data-set="drive.source" data-v="1">Model</button></div></section>';
      var rest = list.filter(function (p) { return p.id !== 'drive.source'; });
      var html = renderParams(rest);
      // put Source after "Editing voice", as on the canvas
      var cut = html.indexOf('</section>') + '</section>'.length;
      pre = html.slice(0, cut) + srcHtml;
      list = null; extra = html.slice(cut);
    }
    if (mod === 'bloom') {
      var bl = S.meter.bloom || 0;
      pre = '<section class="card"><h2 class="label">Sweep now</h2><div class="bar"><div id="bloomBar" style="width:' + bl * 100 + '%"></div></div>' +
        '<div class="zones"><span>Drone</span><span>Grains</span><span>Wash</span></div><span class="small">Heel to a third: the drone swells in and its sustain stretches to infinite. A third to two thirds: grains join. Two thirds to toe: delay and reverb climb towards self-oscillation, held back by the feedback limiter. Grains reach is on the Grains page.</span></section>';
    }
    if (mod === 'drone') {
      pre = '<section class="card"><h2 class="label">Held notes</h2><div class="pair"><button type="button" class="wide" data-stomp="D">Capture</button><button type="button" class="wide" data-act="droneClear">Clear</button></div>' +
        '<span class="small">Tap D to grab a note, tap again to stack up to four. Double tap D to clear.</span></section>';
    }
    if (mod === 'delay') {
      pre = '<section class="card"><h2 class="label">Tap tempo</h2><button type="button" class="wide" data-act="tap">Tap</button></section>';
    }
    if (mod === 'footswitches') {
      var jobs = [['A', 'Edge / Drive', 'Previous song'], ['B', 'Bitrot', 'Next song'], ['C', 'Filter', 'Tuner'], ['D', 'Drone capture', 'Slap']];
      pre = '<section class="card"><h2 class="label">Tap and hold</h2>' + jobs.map(function (j, i) {
        return '<div style="display:flex;align-items:center;gap:12px">' + flip(j[0]).replace('class="flip "', 'class="flip" style="width:36px;height:42px;font-size:22px"') +
          '<span style="flex:1"><span style="display:block;font-size:17px">Tap: ' + j[1] + '</span><span class="small">Hold: ' + j[2] + '</span></span>' +
          '<button type="button" class="wide" style="height:44px;padding:0 14px;font-size:14px" data-learn="' + i + '">Learn</button></div>';
      }).join('') + '<div class="small">Hold C and D together for a second: Keep.</div></section>' +
        '<section class="card"><h2 class="label">Expression</h2><div class="small">Rock the pedal to heel and press Heel, then to toe and press Toe. This also fixes a pedal that reads backwards.</div>' +
        '<div class="pair"><button type="button" class="wide" data-cal="0">Heel</button><button type="button" class="wide" data-cal="1">Toe</button></div>' +
        '<div class="pair"><button type="button" class="wide" data-learn="4">Learn pedal</button><button type="button" class="wide" data-cal="2">Reset</button></div></section>' +
        '<section class="card"><h2 class="label">Status</h2><span class="small">' + esc(S.footswitch || 'Unknown') + '</span></section>';
    }
    if (mod === 'output') {
      var mt = S.meter;
      extra = '<section class="card"><h2 class="label">Keep</h2><label class="switch" style="background:#262626">Tag Keeps as Live<input type="checkbox" data-act="keeperLive"' + (S.keeperLive ? ' checked' : '') + '></label></section>' +
        '<section class="card"><h2 class="label">Keystone</h2><span class="small">Audio: ' + esc(S.audio || 'unknown') + '</span>' +
        '<span class="small">CPU ' + (mt.cpu != null ? mt.cpu : '-') + '%' + (mt.light ? ', effects in light mode' : '') + '. Round trip ' + (mt.lat || '-') + ' ms.</span>' +
        '<span class="small">Version ' + esc(S.schema.version) + '</span></section>';
    }
    var body = list ? renderParams(list) : '';
    return '<section class="screen" style="gap:14px">' + header(m.title, { id: 'tone', label: 'Tone' }, right) + pre + body + extra + nav('tone') + '</section>';
  }

  function setlistScreen() {
    var rows = S.songs.map(function (name, i) {
      var cur = i === S.current;
      var sub = cur ? (S.edited ? 'Playing now, <span class="edited">edited</span>' : 'Playing now') : 'Tap to load';
      var inner = '';
      if (S.renameRow === i) inner = '<div class="rename"><input id="renameText" maxlength="48" value="' + esc(name) + '" aria-label="Song name"><button type="button" data-act="renameSave" data-i="' + i + '">Save</button></div>';
      else if (S.confirmDel === i) inner = '<div class="acts" style="grid-template-columns:2fr 1fr 1fr"><span class="small" style="align-self:center">Delete ' + esc(name) + '?</span><button type="button" class="del" data-act="delYes" data-i="' + i + '">Delete</button><button type="button" data-act="delNo">Keep</button></div>';
      else if (S.openRow === i) inner = '<div class="acts"><button type="button" data-act="rename" data-i="' + i + '">Rename</button><button type="button" data-act="copy" data-i="' + i + '">Copy</button>' +
        '<button type="button" data-act="up" data-i="' + i + '" aria-label="Move up">Up</button><button type="button" data-act="down" data-i="' + i + '" aria-label="Move down">Down</button>' +
        '<button type="button" class="del" data-act="del" data-i="' + i + '">Delete</button></div>';
      return '<div class="srow' + (cur ? ' cur' : '') + '" data-row="' + i + '"><div class="main">' +
        '<button type="button" class="grip" data-grip="' + i + '" aria-label="Drag to reorder">' + ICON.grip + '</button>' +
        '<button type="button" class="sload" data-song="' + i + '">' + flip(two(i + 1), cur ? 'blue' : '') + '<span style="display:flex;flex-direction:column;min-width:0"><span class="n">' + esc(name) + '</span><span class="s">' + sub + '</span></span></button>' +
        '<button type="button" class="dots" data-menu="' + i + '" aria-label="Song options">' + ICON.dots + '</button></div>' + inner + '</div>';
    }).join('');
    return '<section class="screen" style="gap:14px">' + header('Setlist', { id: 'play', label: 'Play' }, '<span class="small">' + S.songs.length + ' of ' + S.maxSongs + '</span>') +
      '<div class="group" id="rows">' + rows + '</div>' +
      '<div class="pair"><button type="button" class="wide" data-act="add"' + (S.songs.length >= S.maxSongs ? ' disabled style="opacity:0.4"' : '') + '>Add song</button><button type="button" class="wide blue" data-act="save">Save edits</button></div>' +
      '<div class="small">Save writes your current changes into the loaded song. Changing song without saving puts it back as it was.</div>' +
      nav('play') + '</section>';
  }

  // ---------------------------------------------------------------- render
  var app = document.getElementById('app');
  var lastScreenKey = '';
  function render(soft) {
    if (!S.schema && S.screen !== 'play') S.screen = 'play';
    var active = document.activeElement;
    if (soft && active && active.tagName === 'INPUT' && active.type === 'text') return;   // don't wipe typing
    if (soft && S.dragging) return;
    var html;
    switch (S.screen) {
      case 'tuner': html = tunerScreen(); break;
      case 'tone': html = S.schema ? toneScreen() : playScreen(); break;
      case 'module': html = S.schema ? moduleScreen() : playScreen(); break;
      case 'setlist': html = setlistScreen(); break;
      default: html = playScreen();
    }
    var key = S.screen + (S.module || '');
    var y = window.scrollY;
    app.innerHTML = html;
    if (key === lastScreenKey) window.scrollTo(0, y); else window.scrollTo(0, 0);
    lastScreenKey = key;
    if (S.sel) sheet();
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
      if (S.bloomDrag == null) {
        var bar = document.getElementById('bloomBar'), pct = document.getElementById('bloomPct');
        if (bar) bar.style.width = (m.bloom * 100) + '%';
        if (pct) pct.textContent = 'Expression ' + Math.round(m.bloom * 100) + '%';
      }
      var droneTile = document.querySelector('[data-stomp="D"] .st');
      var want = m.drone ? 'HOLDING' + (m.drone > 1 ? ' ' + m.drone : '') : 'READY';
      if (droneTile && droneTile.textContent !== want) render(true);
    } else if (S.screen === 'tuner') {
      render(true);
    } else if (S.screen === 'module' && S.module === 'bloom') {
      var b2 = document.getElementById('bloomBar'); if (b2) b2.style.width = (m.bloom * 100) + '%';
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
    S.screen = screen; S.sel = null; sheetEl.hidden = true; S.openRow = -1; S.renameRow = -1; S.confirmDel = -1; S.showCaptures = false;
    render();
  }

  document.addEventListener('click', function (e) {
    var t = e.target.closest('button, input[type=checkbox]');
    if (!t) return;
    var d = t.dataset;
    if (d.go) return go(d.go);
    if (d.stomp) { send({ t: 'stomp', k: d.stomp }); return; }
    if (d.module) { S.module = d.module; return go('module'); }
    if (d.set) { setParam(d.set, +d.v); return render(true); }
    if (d.toggle) { setParam(d.toggle, t.checked ? 1 : 0); return render(true); }
    if (d.flip) { S.sel = S.sel === d.flip ? null : d.flip; render(true); if (!S.sel) sheetEl.hidden = true; return; }
    if (d.step) return step(+d.step);
    if (d.song) { send({ t: 'song', i: +d.song }); return; }
    if (d.menu) { var i = +d.menu; S.openRow = S.openRow === i ? -1 : i; S.renameRow = -1; S.confirmDel = -1; return render(); }
    if (d.capture) { send({ t: 'capture', file: d.capture }); S.showCaptures = false; return render(); }
    if (d.learn) { send({ t: 'learn', sw: +d.learn }); return; }
    if (d.cal) { send({ t: 'calibrate', step: +d.cal }); return; }
    var i2 = d.i != null ? +d.i : -1;
    switch (d.act) {
      case 'keep': send({ t: 'keep' }); break;
      case 'manual': var mi = document.getElementById('manualIp'); if (mi && mi.value.trim()) { S.manualIp = mi.value.trim(); if (window.KeystoneNative && KeystoneNative.connectTo) KeystoneNative.connectTo(S.manualIp); toast('TRYING'); } break;
      case 'prev': send({ t: 'songStep', d: -1 }); break;
      case 'next': send({ t: 'songStep', d: 1 }); break;
      case 'note': var nt = document.getElementById('noteText'); if (nt && nt.value.trim()) send({ t: 'keepNote', text: nt.value.trim() }); S.noteUntil = 0; render(); break;
      case 'sheetClose': S.sel = null; sheetEl.hidden = true; render(true); break;
      case 'captures': S.showCaptures = !S.showCaptures; render(); break;
      case 'droneClear': send({ t: 'drone', op: 'clear' }); break;
      case 'tap': send({ t: 'tap' }); break;
      case 'keeperLive': send({ t: 'keeperLive', on: t.checked }); break;
      case 'add': send({ t: 'setlist', op: 'add' }); break;
      case 'save': send({ t: 'setlist', op: 'save' }); break;
      case 'rename': S.renameRow = i2; S.openRow = -1; render(); var r = document.getElementById('renameText'); if (r) { r.focus(); r.select(); } break;
      case 'renameSave': var rt = document.getElementById('renameText'); if (rt && rt.value.trim()) send({ t: 'setlist', op: 'rename', i: i2, name: rt.value.trim() }); S.renameRow = -1; render(); break;
      case 'copy': send({ t: 'setlist', op: 'copy', i: i2 }); S.openRow = -1; break;
      case 'up': if (i2 > 0) { send({ t: 'setlist', op: 'move', i: i2, to: i2 - 1 }); S.openRow = i2 - 1; } break;
      case 'down': if (i2 < S.songs.length - 1) { send({ t: 'setlist', op: 'move', i: i2, to: i2 + 1 }); S.openRow = i2 + 1; } break;
      case 'del': S.confirmDel = i2; S.openRow = -1; render(); break;
      case 'delYes': send({ t: 'setlist', op: 'delete', i: i2 }); S.confirmDel = -1; break;
      case 'delNo': S.confirmDel = -1; render(); break;
    }
  });

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

  // Bloom bar: drag to sweep when there's no pedal
  function bloomAt(e) {
    var b = document.getElementById('bloom'); if (!b) return null;
    var bar = b.querySelector('.bar').getBoundingClientRect();
    var x = (e.touches ? e.touches[0].clientX : e.clientX);
    return Math.max(0, Math.min(1, (x - bar.left) / bar.width));
  }
  var bloomSendTimer = null;
  function bloomMove(e) {
    if (S.bloomDrag == null) return;
    var v = bloomAt(e); if (v == null) return;
    e.preventDefault();
    S.bloomDrag = v;
    var bar = document.getElementById('bloomBar'), pct = document.getElementById('bloomPct');
    if (bar) bar.style.width = (v * 100) + '%';
    if (pct) pct.textContent = 'Expression ' + Math.round(v * 100) + '%';
    if (!bloomSendTimer) bloomSendTimer = setTimeout(function () { bloomSendTimer = null; send({ t: 'bloom', v: S.bloomDrag }); }, 20);
  }
  function bloomDown(e) {
    if (!e.target.closest || !e.target.closest('#bloom')) return;
    S.bloomDrag = bloomAt(e); S.dragging = true; bloomMove(e);
  }
  function bloomUp() {
    if (S.bloomDrag == null) return;
    send({ t: 'bloom', v: S.bloomDrag });
    setTimeout(function () { S.bloomDrag = null; S.dragging = false; }, 150);
  }
  document.addEventListener('touchstart', bloomDown, { passive: false });
  document.addEventListener('touchmove', bloomMove, { passive: false });
  document.addEventListener('touchend', bloomUp);
  document.addEventListener('mousedown', bloomDown);
  document.addEventListener('mousemove', function (e) { if (e.buttons) bloomMove(e); });
  document.addEventListener('mouseup', bloomUp);

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

  render();
  return { onMessage: onMessage, onLink: onLink, back: back, state: S };
})();
