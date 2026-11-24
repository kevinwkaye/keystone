// Kaleidoscope: how each pedal's page is laid out, after its own plugin's panel.
//
// The plugin sends its controls by name; this says where each one goes.
//   top:      choices across the top of the face (a pedal's type or mode)
//   rows:     the face itself, a row of columns: { title, seg (a choice down the column), knobs }
//             "Name|Label" shows a control under a shorter label
//   steps:    stepped choices under the knobs
//   feet:     A to D. lit: the control that's on while its job is (inv: lit while it's off),
//             press: a control the phone holds down like the footswitch, text: a control
//             whose value shows under the tile
//   sections: everything else, folded away in the plugin's own groups
//   hide:     controls that only make sense to the plugin itself
//   rename / choices: kinder words on the phone (no Ableton here: the song's tempo)
// Anything not placed lands in "Everything else", so nothing is ever lost.
window.KS_PEDAL_LAYOUTS = (function () {
  var FEET = ['Footswitch A', 'Footswitch B', 'Footswitch C', 'Footswitch D'];
  var EXP = ['Expression Target', 'Expression Min', 'Expression Max', 'Expression Smoothing'];
  var EXP2 = ['Expression target', 'Expression heel', 'Expression toe'];
  var SONG = { 'Sync to Ableton': 'Sync to song tempo', 'Sync to Host': 'Sync to song tempo', 'Use Host Tempo': 'Use song tempo', 'Tempo Sync': 'Sync to song tempo' };
  return {
    murmur: {
      top: ['Mode'],
      rows: [[
        { title: 'WET', seg: 'Wet Mode', knobs: ['Time', 'Wet Modify|Modify'] },
        { title: 'CLOCK', seg: 'Routing', knobs: ['Clock', 'Mix'] },
        { title: 'LOOP', seg: 'Loop Mode', knobs: ['Length', 'Loop Modify|Modify'] }
      ]],
      feet: [{ lit: 'Wet On' }, { lit: 'Loop On' }, {}, { text: 'Preset' }],
      sections: [
        { name: 'Hidden options', sum: 'Width, Tone, Fade, Balance, Channel sync', params: ['Stereo Width', 'Tone', 'Fade', 'Direct Micro-Loop', 'Level Balance', 'Channel Sync', 'Half Loop Length', 'Spread Solo'] },
        { name: 'Switches', sum: 'Freeze, MISO, Spread, Dry Kill, Trails, Latch and more', params: ['Freeze', 'Loop Record', 'Overdub', 'MISO', 'Spread', 'Dry Kill', 'Trails', 'Latch', 'No Dub (Refresh)', 'Smooth Clock', 'Classic', 'Preset', 'Bypass'] },
        { name: 'Murk', sum: 'Soup, Relay or Flip, Glue, Cross and the EQ', params: ['Murk Wet Mode', 'Murk Loop Mode', 'Glue', 'Dry Glue', 'Cross', 'Cross From', 'EQ Tilt', 'EQ On'] },
        { name: 'Ramping and expression', sum: 'What the ramp moves, how fast and which shape', params: ['Ramp Time', 'Ramp Wet Modify', 'Ramp Clock', 'Ramp Loop Modify', 'Ramp Length', 'Bounce', 'Sweep', 'Ramp Speed', 'Ramp Waveform', 'Driven By', 'Expression Polarity', 'Expression Target'] },
        { name: 'Synth mode', sum: 'Drone, gate or envelope, and the ADSR', params: ['Synth Mode', 'Note Output', 'Attack', 'Decay', 'Sustain', 'Release', 'Glide', 'Original Pitch Note'] },
        { name: 'Tempo and levels', sum: 'Follow the song tempo, input and output', params: ['Tempo Sync', 'Tap Tempo', 'Input Gain', 'Output Gain', 'Max Buffer', 'Clock Steps'] }
      ],
      hide: ['Keeper', 'Expression'],
      choices: { 'Tempo Sync': { 'Ableton': 'Song' } },
      rename: { 'Tempo Sync': 'Tempo' }
    },
    afterglow: {
      top: ['Catch', 'Hold'],
      rows: [[
        { title: 'SHAPE', knobs: ['Rise', 'Decay'] },
        { title: 'TONE', knobs: ['Tone', 'Resonance'] },
        { title: 'LEVEL', knobs: ['Level', 'Input Gain|Input'] }
      ]],
      steps: ['Layers'],
      feet: [{ lit: 'Brass Pedal', press: 'Brass Pedal' }, { lit: 'Bypass', inv: true }, { lit: 'Kill Dry' }, { press: 'Next Preset' }],
      sections: [
        { name: 'FX loop', sum: 'Where it sits, which effect, how much', params: ['FX Loop', 'FX Type', 'FX Amount', 'FX Mix'] },
        { name: 'Aux and input', sum: 'The aux switch, input type, sustain pedal', params: ['Aux Mode', 'Aux On', 'Input', 'Sustain Pedal Input', 'Expression Target'] }
      ],
      hide: ['Keeper', 'Clear Layers', 'Next Preset', 'Brass Pedal']
    },
    oilslick: {
      top: ['Version'],
      rows: [[
        { title: 'PITCH', knobs: ['Pitch', 'Tracking'] },
        { title: 'VOICES', knobs: ['Primary', 'Secondary'] },
        { title: 'COLOUR', knobs: ['Tone', 'Magic'] }
      ]],
      feet: [{ lit: 'Bypass', inv: true, press: FEET[0] }, { lit: 'Magic Switch', press: FEET[1] }, { lit: 'Jump', press: FEET[2] }, { lit: 'Freeze', press: FEET[3] }],
      sections: [
        { name: 'Switches', sum: 'Trails, Runaway, Dive, Tame', params: ['Trails', 'Runaway', 'Dive', 'Tame', 'Magic Switch', 'Jump', 'Freeze', 'Bypass'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP }
      ],
      hide: ['Keeper'].concat(FEET)
    },
    changeling: {
      top: ['Synth Mode', 'Waveshape'],
      rows: [[
        { title: 'PITCH', knobs: ['Pitch', 'Modulation'] },
        { title: 'FILTER', knobs: ['Filter', 'Filter Env'] },
        { title: 'LEVEL', knobs: ['Mix', 'Sustain'] }
      ]],
      feet: [{ lit: 'Bypass', inv: true }, {}, {}, { lit: 'Sustain Latch' }],
      sections: [
        { name: 'The alt layer', sum: 'Glide, filter type, delay, ring mod', params: ['Glide', 'Filter Type', 'Delay Level', 'Delay Feedback', 'Ring Mod', 'Bandwidth'] },
        { name: 'Envelope and tempo', sum: 'Trigger or follow, the song tempo, trails', params: ['Envelope Type', 'Sync to Host', 'Tempo', 'Trails', 'Sustain Latch'] },
        { name: 'Second oscillator', sum: 'Interval, wave, level, detune', params: ['Osc 2 Interval', 'Osc 2 Wave', 'Osc 2 Level', 'Osc 2 Detune'] },
        { name: 'Tracking', sum: 'Input, sensitivity, lowest note, character', params: ['Input', 'Source', 'Character', 'Lowest Note', 'Sensitivity', 'Note Hold', 'Poly Speed', 'Octave Guard', 'Tuner Reference'] },
        { name: 'Arpeggiator', sum: 'Pattern, step, octaves, gate', params: ['Arp Pattern', 'Arp Step', 'Arp Octaves', 'Arp Gate'] },
        { name: 'MIDI and expression', sum: 'Ring carrier, velocity, bend', params: ['Ring Carrier', 'Velocity', 'Pitch Bend', 'MIDI Mono', 'Expression Target'] }
      ],
      hide: ['Keeper'],
      rename: SONG
    },
    backtrack: {
      top: ['Mode'],
      rows: [[
        { title: 'DIRECTION', knobs: ['DIR 1|Dir 1', 'DIR 2|Dir 2', 'DIR 3|Dir 3'] },
        { title: 'REPEATS', knobs: ['LEN S|Length', 'FBK|Feedback', 'RAND|Random'] },
        { title: 'LEVEL', knobs: ['Mix', 'LEN B|Bars'] }
      ]],
      steps: ['Heads', 'Quantise'],
      feet: [{ lit: 'On' }, {}, { lit: 'Reverse' }, {}],
      sections: [
        { name: 'Switches', sum: 'Transcend, Grit, Boost, Spread, Trails, Sync', params: ['Transcend', 'Grit', 'Grit amount', 'Boost', 'Spread', 'Trails', 'Latch', 'Sync', 'Lock', 'Low keep', 'Low keep frequency', 'Reverse', 'Firmware'] },
        { name: 'The Q layer', sum: 'Feedback filter, LFO, slices, steps, heads', params: ['FBK filter', 'LFO rate', 'LFO depth', 'Trigger sensitivity', 'Jump', 'Slice fade', 'Step rate', 'Step variance', 'Step heads', 'Head 1 level', 'Head 2 level', 'Head 3 level', 'Dub fade'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP2 }
      ],
      hide: ['Expression', 'Expression active', 'On'],
      rename: { 'Sync': 'Sync to song tempo', 'Firmware': 'Firmware (Nightly unlocks the rest)' }
    },
    speckle: {
      top: ['Effect', 'Variation'],
      rows: [[
        { title: 'SHAPE', knobs: ['Activity', 'Repeats'] },
        { title: 'TONE', knobs: ['Shape', 'Filter'] },
        { title: 'SPACE', knobs: ['Mix', 'Space'] }
      ]],
      steps: ['Subdivision'],
      feet: [{ lit: 'Bypass', inv: true }, { lit: 'Hold' }, { press: 'Loop Rec / Dub' }, { press: 'Loop Stop / Play' }],
      sections: [
        { name: 'Motion', sum: 'Mod depth and rate, resonance, reverse, reverb', params: ['Mod Depth', 'Mod Rate', 'Resonance', 'Reverse', 'Reverb'] },
        { name: 'Looper', sum: 'Level, speed, fades, where it sits', params: ['Loop Level', 'Loop Speed', 'Fade Time', 'Volume', 'Loop Fade', 'Loop Before Effects', 'Quantise Loop', 'Reverse Loop', 'Burst', 'Looper Only', 'Looper Order'] },
        { name: 'Tempo and switches', sum: 'Song tempo, time mode, trails, input', params: ['Sync to Ableton', 'Time Mode', 'Tempo', 'Input', 'Trails', 'Hold', 'Expression Target'] }
      ],
      hide: ['Keeper', 'Next Effect', 'Loop Rec / Dub', 'Loop Stop / Play'],
      rename: SONG
    },
    jetstream: {
      top: ['Voice'],
      rows: [[
        { title: 'ENVELOPE', knobs: ['Env Shape|Shape', 'Env Time|Time'] },
        { title: 'MOTION', knobs: ['LFO Depth|Depth', 'LFO Rate|Rate'] },
        { title: 'BLEND', knobs: ['Dimension', 'Dry'] }
      ]],
      steps: ['Trigger Mode'],
      feet: [{}, {}, { lit: 'Hold Memory' }, {}],
      sections: [
        { name: 'Motion', sum: 'LFO shape, dimension source, song tempo', params: ['LFO Shape', 'Dimension Source', 'Tempo Sync'] },
        { name: 'Output', sum: 'Drive, tone, width, air, level', params: ['Drive', 'Tone', 'Master', 'Auto Level', 'Width', 'Air', 'Tails', 'Bypass'] },
        { name: 'Input', sum: 'Input type, sensitivity, footswitch mode', params: ['Input', 'Sensitivity', 'Footswitch Mode', 'Hold Memory'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP2 }
      ],
      hide: ['Expression', 'Expression active'],
      rename: SONG
    },
    prismpatch: {
      rows: [
        [
          { title: 'CHARACTER', seg: 'Character Effect', knobs: ['Character Amount|Amount'] },
          { title: 'MOVEMENT', seg: 'Movement Effect', knobs: ['Movement Amount|Amount'] },
          { title: 'DIFFUSION', seg: 'Diffusion Effect', knobs: ['Diffusion Amount|Amount'] },
          { title: 'TEXTURE', seg: 'Texture Effect', knobs: ['Texture Amount|Amount'] }
        ],
        [
          { title: 'TILT', knobs: ['Tilt|Tilt'] },
          { title: 'RATE', knobs: ['Rate|Rate'] },
          { title: 'TIME', knobs: ['Time|Time'] },
          { title: 'MIX', knobs: ['Mix|Mix'] }
        ]
      ],
      feet: [{ lit: 'Character On' }, { lit: 'Movement On' }, { lit: 'Diffusion On' }, { lit: 'Texture On' }],
      sections: [
        { name: 'Modules', sum: 'Each module\'s level and bypass group', params: ['Character On', 'Character Effect Vol', 'Character In Bypass Group', 'Movement On', 'Movement Effect Vol', 'Movement In Bypass Group', 'Diffusion On', 'Diffusion Effect Vol', 'Diffusion In Bypass Group', 'Texture On', 'Texture Effect Vol', 'Texture In Bypass Group'] },
        { name: 'Drift, order and spare slot', sum: 'Movement and diffusion drift, module order, the spare', params: ['Movement Drift', 'Diffusion Drift', 'Module Order', 'Spare Slot', 'Spare Amount', 'Spare Tilt/Rate/Time'] },
        { name: 'Tempo', sum: 'Song tempo for Rate and Time', params: ['Sync to Ableton', 'Tempo', 'Rate Follows Tempo', 'Time Follows Tempo'] },
        { name: 'Shared', sum: 'Output, sensitivity, input, bypass, trails', params: ['Output Level', 'Sensitivity', 'Calibration', 'Filter Style', 'Input', 'Bypass', 'Trails', 'True Dry at Mix Zero'] },
        { name: 'Capture and gesture', sum: 'Routing, overdub, quantise, speed', params: ['Capture Routing', 'Capture Overdub', 'Capture Quantise', 'Gesture Quantise', 'Gesture Speed', 'Expression Target'] }
      ],
      hide: ['Keeper'],
      rename: SONG
    },
    bitrot: {
      rows: [[
        { title: 'VOICES', knobs: ['Square', 'Subharmonic|Sub', 'Oscillator|Osc'] },
        { title: 'MASTER', knobs: ['Dry', 'Level', 'Corrupt'] },
        { title: 'MOTION', seg: 'Glide or Vibrato', knobs: ['Rate'] }
      ]],
      steps: ['Subharmonic Interval', 'Oscillator Interval'],
      feet: [{ lit: 'Bypass', inv: true }, { text: 'Subharmonic Interval' }, { lit: 'Freeze' }, { lit: 'Octave Up' }],
      sections: [
        { name: 'Roots and floor', sum: 'Where the sub and oscillator sit', params: ['Subharmonic Root', 'Sub Floor', 'Oscillator Root', 'Oscillator Sub', 'Floor'] },
        { name: 'Tracking', sum: 'Input, compressor, gate, vibrato depth, bend', params: ['Input', 'Tracking Compressor', 'Gate Sensitivity', 'Vibrato Depth', 'Bend'] },
        { name: 'Switches and expression', sum: 'Freeze, octave up, what the pedal moves', params: ['Freeze', 'Octave Up', 'Bypass', 'Expression Target'] }
      ],
      hide: ['Keeper']
    },
    monolith: {
      top: ['Algorithm'],
      rows: [[
        { title: 'SPACE', knobs: ['Space Decay|Decay', 'Predelay'] },
        { title: 'TONE', knobs: ['Lo Frequency|Lo Freq', 'Hi Frequency|Hi Freq'] },
        { title: 'MOTION', knobs: ['Modulate', 'Mix'] }
      ]],
      steps: ['Pitch Vector'],
      feet: [{ lit: 'Bypass', inv: true }, { lit: 'Auto Swell' }, { lit: 'Infinite' }, {}],
      sections: [
        { name: 'The alt layer', sum: 'Mod speed, pitch vector mix, density, attack, vibrato', params: ['Mod Speed', 'Pitch Vector Mix', 'Density', 'Attack Time', 'Vibrato Depth'] },
        { name: 'Globals', sum: 'Trails, Kill Dry, Bass Guard, levels', params: ['Trails', 'Kill Dry', 'Bass Guard', 'Predelay Follows Tempo', 'Input Trim', 'Output Level'] },
        { name: 'Shimmer and expression', sum: 'What D shimmers to, what the pedal moves', params: ['Shimmer Interval', 'Expression Target', 'Auto Swell', 'Infinite', 'Bypass'] }
      ],
      hide: ['Keeper'],
      rename: { 'Predelay Follows Tempo': 'Predelay follows the song tempo' }
    },
    seasick: {
      rows: [[
        { title: 'WOBBLE', knobs: ['Rate', 'Depth'] },
        { title: 'TONE', knobs: ['LPG', 'Damp'] },
        { title: 'LEVEL', knobs: ['Mix', 'Volume'] }
      ]],
      feet: [{ lit: 'Bypass', inv: true, press: FEET[0] }, { lit: 'Seasick', press: FEET[1] }, { lit: 'Freeze', press: FEET[2] }, { press: FEET[3] }],
      sections: [
        { name: 'Switches', sum: 'Punch, Clean, Freeze mode, Swell, Calm', params: ['Punch', 'Clean', 'Freeze Mode', 'Swell', 'Calm', 'Seasick', 'Freeze', 'Bypass'] },
        { name: 'Bass and input', sum: 'Input type, bass split, the gate floor', params: ['Input', 'Bass Split', 'LPG Floor', 'Wobble Ceiling'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP }
      ],
      hide: ['Keeper', 'Pluck', 'Re-roll'].concat(FEET)
    },
    lathe: {
      top: ['Voice'],
      rows: [
        [
          { title: 'TONE', knobs: ['Tone', 'Lag'] },
          { title: 'MOTION', knobs: ['Rate', 'Depth'] },
          { title: 'WARP', knobs: ['Warp', 'Ramp'] }
        ],
        [
          { title: 'MIX', knobs: ['Mix'] },
          { title: 'VOLUME', knobs: ['Volume'] }
        ]
      ],
      feet: [{ lit: 'Bypass', inv: true, press: FEET[0] }, { press: FEET[1] }, { press: FEET[2] }, { text: 'Preset', press: FEET[3] }],
      sections: [
        { name: 'The ramp', sum: 'What it moves and which way, its wave and division', params: ['Ramp Moves Volume', 'Ramp Moves Lag', 'Ramp Moves Mix', 'Ramp Moves Rate', 'Ramp Moves Depth', 'Ramp Moves Warp', 'Ramp Moves Tone', 'Ramp Rises Volume', 'Ramp Rises Lag', 'Ramp Rises Mix', 'Ramp Rises Rate', 'Ramp Rises Depth', 'Ramp Rises Warp', 'Ramp Rises Tone', 'Bounce', 'Sweep Top', 'First Half', 'Second Half', 'Division', '3 6 8 Divisions'] },
        { name: 'Tempo', sum: 'Song tempo, tap, what hold C does', params: ['Use Host Tempo', 'Sync To Tempo', 'Tempo', 'Tap Sets', 'Hold C'] },
        { name: 'Output and bass', sum: 'Mono or stereo, width, noise, lo-fi, keep lows dry', params: ['Output', 'Width', 'Noise', 'Lo-Fi', 'Bass: Keep Lows Dry', 'Bass Below', 'Momentary Bypass', 'Preset', 'Bypass'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP }
      ],
      hide: ['Keeper', 'Brake', 'Hold', 'Fire Ramp'].concat(FEET),
      rename: SONG
    },
    grotto: {
      top: ['Drag Mode'],
      rows: [[
        { title: 'SPACE', knobs: ['Length', 'Diffuse'] },
        { title: 'TONE', knobs: ['Dampen', 'Reflect'] },
        { title: 'DRAG', knobs: ['Drag', 'Mix'] }
      ]],
      feet: [{ lit: 'Bypass', inv: true, press: FEET[0] }, { lit: 'Runaway', press: FEET[1] }, { lit: 'Freeze', press: FEET[2] }, { lit: 'Dry Kill', press: FEET[3] }],
      sections: [
        { name: 'Switches', sum: 'Trails, low cut, bass, mono, tame', params: ['Trails', 'Low Cut', 'Bass', 'Mono', 'Tame', 'Runaway', 'Freeze', 'Dry Kill', 'Bypass'] },
        { name: 'Expression', sum: 'What the pedal moves, heel and toe', params: EXP }
      ],
      hide: ['Keeper'].concat(FEET)
    },
    divebomb: {
      top: ['Section', 'Model'],
      rows: [[
        { title: 'SETTING', knobs: ['Setting'] },
        { title: 'TREADLE', knobs: ['Treadle'] },
        { title: 'LEVEL', knobs: ['Level', 'Glitch'] }
      ]],
      steps: ['Treadle algorithm'],
      feet: [{ lit: 'Bypass', inv: true }, {}, { text: 'Setting' }, { lit: 'Shift layer on' }],
      sections: [
        { name: 'Stomp section', sum: 'Its algorithm, footswitch B, the pitches and sequences', params: ['Stomp algorithm', 'Footswitch B', 'Pitch 1', 'Pitch 2', 'Pitch 2 second', 'Pitch 2 mode', 'Sequence 2 step 1', 'Sequence 2 step 2', 'Sequence 3 step 1', 'Sequence 3 step 2', 'Sequence 3 step 3', 'Sequence 5 step 1', 'Sequence 5 step 2', 'Sequence 5 step 3', 'Sequence 5 step 4', 'Sequence 5 step 5', 'Sequence retrigger'] },
        { name: 'Momentary and shift', sum: 'Rise and fall, the shift layer, drop', params: ['Momentary rise', 'Momentary fall', 'Shift layer on', 'Shift layer type', 'Shift layer interval', 'Shift layer glide', 'Drop', 'Drop footswitch', 'Drop latched on'] },
        { name: 'Glide and trill', sum: 'Glide interval and timing, trill speed', params: ['Glide interval', 'Glide direction', 'Glide shift', 'Glide return', 'Glide footswitch', 'Glide latched on', 'Trill', 'Trill sync', 'Trill division', 'Trill rate'] },
        { name: 'Tone and input', sum: 'Dry+, low anchor, input, latency, trails', params: ['Dry+', 'Low Anchor', 'Low Anchor crossover', 'Input', 'Original bandwidth', 'Latency', 'Trails', 'Bypass', 'Expression moves'] }
      ],
      hide: ['Keeper'],
      rename: { 'Trill sync': 'Trill follows the song tempo' }
    }
  };
})();
