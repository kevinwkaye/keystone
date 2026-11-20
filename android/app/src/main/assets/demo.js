// Demo: a pretend Keystone inside the phone, so you can try every screen and show the app
// off with no laptop or Pi. It answers the same messages Keystone does: switches,
// settings, songs, banks, modes, the tuner and the meters move as if a bass were playing.
// Nothing is sent anywhere. Recordings, updates and captures need the real rig.
window.KSDemo = (function () {
  var st = null, timer = null, t0 = 0, tunerOn = false, droneOn = false, pedal = 0.5, taps = [];
  var NEEDS_RIG = { takes: 1, takeInfo: 1, listen: 1, listenList: 1, listenGet: 1, listenUse: 1, recStop: 1, fw: 1,
    capture: 1, space: 1, learn: 1, calibrate: 1, restartAudio: 1, bufferFind: 1, bufferStop: 1, asioPanel: 1, keep: 1, keepNote: 1, wifi: 1, 'default': 1 };

  function reply(o) { setTimeout(function () { if (window.KS) KS.onMessage(JSON.stringify(o)); }, 15); }
  function toast(text) { reply({ t: 'toast', text: text }); }
  function state() {
    var o = JSON.parse(JSON.stringify(st));
    o.t = 'state'; o.tuner = tunerOn; o.version = 'demo';
    return o;
  }
  function v(id) { return st.values[id] || 0; }
  function set(id, x) { st.values[id] = x; st.edited = true; reply({ t: 'val', id: id, v: x, edited: true }); }
  function flip(id) { set(id, v(id) > 0.5 ? 0 : 1); }

  function start() {
    var base = (window.KS_DEFAULT && KS_DEFAULT.state) || {};
    try { var cached = JSON.parse(localStorage.getItem('ks.state')); if (cached && cached.values) base = cached; } catch (e) {}
    st = JSON.parse(JSON.stringify(base));
    st.values = st.values || {}; st.songs = st.songs || ['Demo song']; st.parts = st.parts || [false];
    st.current = st.current || 0; st.bank = st.bank || 0; st.rig = st.rig || 0; st.footMode = st.footMode || 0; st.edited = false;
    st.audio = 'Demo: no audio interface'; st.footswitch = 'Demo: no footswitch';
    st.wifi = null;
    t0 = Date.now();
    if (!timer) timer = setInterval(tick, 60);
  }
  function stop() { if (timer) clearInterval(timer); timer = null; }

  // a bass playing a slow riff: notes every 0.6 s, decaying, a little busier in Grit
  function tick() {
    var t = (Date.now() - t0) / 1000, beat = t % 0.6, env = Math.exp(-beat * 3.2);
    var inDb = -14 - 30 * (1 - env) + Math.random() * 1.5;
    var outDb = inDb + 4 + (v('drive.on') > 0.5 ? 3 : 0);
    var m = { t: 'meter', out: Math.round(outDb * 10) / 10, rms: Math.round((outDb - 8) * 10) / 10, 'in': Math.round(inDb * 10) / 10,
      lim: 0, bloom: st.bank === 2 ? pedal : 0, bank: st.bank, job: st.bank === 0 ? (v('bitrot.on') > 0.5 && v('bitrot.pedal') > 0.5 ? 4 : 0) : st.bank === 1 ? (v('wah.on') > 0.5 ? 1 : 2) : 3,
      jobv: pedal, jobOn: true, wait: false, rig: st.rig, melt: pedal, sq: 0, pedal: pedal, drone: droneOn ? 1 : 0, swell: false,
      grains: v('grains.on') > 0.5 ? 0.5 * env : 0, clip: false, light: false, cpu: 9.5, buf: 128, sr: 48000, lat: 6.1, rec: { on: false } };
    if (tunerOn) {
      var cents = 6 * Math.sin(t * 0.9) + 2 * Math.sin(t * 2.3);
      m.tuner = { on: true, note: 'A', oct: 1, cents: Math.round(cents * 10) / 10, hz: Math.round(55 * Math.pow(2, cents / 1200) * 10) / 10, valid: true };
    }
    reply(m);
    if (Math.floor(t * 1000 / 60) % 16 === 0) reply(health());
  }
  function health() {
    return { t: 'health', version: 'demo', device: 'demo', uptime: Math.round((Date.now() - t0) / 1000),
      audio: { ok: true, text: 'Demo: pretend audio, 48 kHz, 128' }, 'in': { db: -20, ch: 1, dead: false },
      fs: { ok: true, text: 'Demo: use the tiles on Play', ago: -1, last: '' }, exp: { pos: pedal, ago: -1, cal: true, rev: false },
      out: { db: -8, lim: 0 }, laptop: { cpu: 9, dropouts: 0, recent: 0, light: false }, link: { phones: 1, laptop: '' } };
  }

  // the footswitch tiles, as Keystone's Stomp mode taps them
  function stomp(k) {
    if (k === 'D' && st.rig === 1) { flip('kuddle.tapeOn'); return; }
    if (k === 'D') { droneOn = !droneOn; toast(droneOn ? 'DRONE' : 'DRONE OFF'); return; }
    var i = 'ABC'.indexOf(k);
    if (i < 0) return;
    if (st.rig === 1) { flip(['kuddle.muteOn', 'kuddle.wahOn', 'kuddle.roomOn'][i]); return; }
    var map = [['drive.on', 'bitrot.on', 'preamp.flip'], ['filter.on', 'dim.on', 'wah.on'], ['grains.on', 'delay.on', 'reverb.on']];
    var id = map[st.bank][i];
    if (st.values[id] === undefined) { toast('DEMO'); return; }
    flip(id);
  }

  function handle(line) {
    var m; try { m = JSON.parse(line); } catch (e) { return; }
    switch (m.t) {
      case 'hello': reply(state()); reply(health()); break;
      case 'ping': reply({ t: 'pong', ts: m.ts }); break;
      case 'set': set(m.id, m.v); break;
      case 'song': st.current = Math.max(0, Math.min(st.songs.length - 1, m.i)); st.edited = false; reply(state()); break;
      case 'songStep': st.current = Math.max(0, Math.min(st.songs.length - 1, st.current + m.d)); st.edited = false; reply(state()); break;
      case 'bank': st.bank = Math.max(0, Math.min(2, m.i)); reply(state()); toast(['GRIT', 'GROOVE', 'GLOW'][st.bank]); break;
      case 'footMode': st.footMode = m.m ? 1 : 0; reply(state()); toast(st.footMode ? 'SONG MODE' : 'STOMP MODE'); break;
      case 'rig': st.rig = m.r ? 1 : 0; reply(state()); toast(st.rig ? 'KUDDLE' : 'KEYSTONE'); break;
      case 'stomp': stomp(m.k); break;
      case 'tuner': tunerOn = !!m.on; reply(state()); break;
      case 'drone': droneOn = false; break;
      case 'pedal': pedal = Math.max(0, Math.min(1, m.v)); break;
      case 'tap':
        var now = Date.now(); taps = taps.filter(function (x) { return now - x < 2000; }); taps.push(now);
        if (taps.length > 1) { var bpm = Math.round(600000 / ((now - taps[0]) / (taps.length - 1))) / 10; set('tempo.bpm', Math.max(40, Math.min(240, bpm))); }
        break;
      case 'setlist':
        if (m.op === 'save' || m.op === 'revert') { st.edited = false; reply(state()); toast(m.op === 'save' ? 'SAVED' : 'REVERTED'); }
        else if (m.op === 'rename' && st.songs[m.i] != null) { st.songs[m.i] = m.name; reply(state()); }
        else toast('DEMO: NEEDS KEYSTONE');
        break;
      default:
        if (NEEDS_RIG[m.t]) toast('DEMO: NEEDS KEYSTONE');
    }
  }
  return { start: start, stop: stop, send: handle, running: function () { return !!timer; } };
})();
