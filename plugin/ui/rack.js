// Sonicology Rack: one view of every Sonicology plug-in in the project (Voxology, Honey Tune, Mixology, each Mixology
// Link, Masterology): which modules run on each track, what they are set to, and how the plug-ins work together.
//
// THIS FILE, rack.css AND plugin/ui/rack/ ARE THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).
// Change them in Mixology (the master copy), copy them to the other two.
//
// The plug-ins do the processing; the Rack shows it all (stage 1 of docs/rack/RACK-PLAN.md in Mixology's repo) and, in
// stage 2, turns knobs and switches modules through the plug-in that owns them (request / answer below).
// A plug-in opens it over its own page:
//   SonicologyRack.open({ host: "voxology" | "mixology" | "masterology", getRack: (track) => Promise<data>, back: () => {},
//                         request: ({ unit, gen, change: "value" | "state", module, control, value, gesture }) =>
//                           Promise<{ ticket, why }>, answer: (unit, gen, ticket) => Promise<{ outcome, why }>,
//                         art: "rack/" (where the rack_*.webp files are, from the page),
//                         dataFile: true (the plug-in also serves getRack's data as the file rack-data/<track>.json,
//                           "_" = every track's: read first, because a native function's long text costs the DAW's
//                           window thread dearly in JUCE; getRack is the fallback),
//                         theme: "dark" | "light" (the plug-in's saved one), onTheme: (theme) => {} (save it) })
// Clicking the cube switches between the dark and the light view.
// getRack() returns what every plug-in publishes into the shared block (RackLink), as JSON:
//   { host, hostHash, units: [ { id, gen, lock, product, version, track, hash, role, on, inDb, outDb, modules: [ { key,
//     state, switchable, summary, by, meterKind, meter, values: [ { name, text, norm, bi, key, steps } ],
//     bands: [ { type, hz, db, q } ], layers } ],
//     together: [ { level, text } ] } ], together: [ { hash, level, text } ], mix: { lufsI, lufsS, tp, style },
//     master: { goal, lufs, ceiling } }
// product: "voxology" | "honeytune" | "link" | "mixology" | "masterology"; role: "vocal" | "beat" | "track" |
// "fullmix" | "master"; state: "on" | "off" | "idle" | "rest"; level: "ok" | "info" | "warn".
// No framework, no modules (a plain script), no network; every name starts with sr- in the page.
(function () {
  "use strict";

  // ---- the plug-ins ----------------------------------------------------------------------------------------------
  const PRODUCTS = {
    honeytune: { name: "Honey Tune", badge: "HT", tone: "honey", order: 0 },
    voxology: { name: "Voxology", badge: "V", tone: "gold", order: 1 },
    link: { name: "Mixology Link", badge: "L", tone: "blue", order: 2 },
    mixology: { name: "Mixology", badge: "M", tone: "blue", order: 3 },
    masterology: { name: "Masterology", badge: "MA", tone: "red", order: 4 },
  };
  const HOSTS = { voxology: "Voxology", mixology: "Mixology", masterology: "Masterology", link: "Link" };
  const SUITE_WORD = { voxology: "VOXOLOGY", honeytune: "HONEY TUNE", link: "LINKS", mixology: "MIXOLOGY", masterology: "MASTEROLOGY" };

  const CATS = [
    { key: "all", name: "All Modules", chip: "ALL", icon: "grid" },
    { key: "vocal", name: "Vocal Tools", chip: "VOCAL", icon: "mic" },
    { key: "mixing", name: "Mixing Tools", chip: "MIXING", icon: "sliders" },
    { key: "mastering", name: "Mastering Tools", chip: "MASTERING", icon: "maxbars" },
    { key: "dynamics", name: "Dynamics", chip: "DYNAMICS", icon: "dyneq" },
    { key: "space", name: "Space / Reverb", chip: "SPACE", icon: "cube" },
    { key: "creative", name: "Creative FX", chip: "CREATIVE", icon: "spark" },
    { key: "utility", name: "Utility", chip: "UTILITY", icon: "gear" },
  ];

  // ---- icons (24 x 24, drawn with the current colour) -------------------------------------------------------------
  const ICONS = {
    lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
    mic: '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5.5 11a6.5 6.5 0 0 0 13 0M12 17.5V21M8.5 21h7"/>',
    wave: '<path d="M2 12h3l2-6 3 12 3-9 2 6 2-3h5"/>',
    sliders: '<path d="M6 3v18M12 3v18M18 3v18"/><rect x="3.5" y="13" width="5" height="3" rx="1"/><rect x="9.5" y="6" width="5" height="3" rx="1"/><rect x="15.5" y="15" width="5" height="3" rx="1"/>',
    dyneq: '<path d="M2 9c3 0 4 0 6 2s2 6 4 6 2-4 4-6 3-2 6-2"/><path d="M12 4v3M12 20v-1"/>',
    s: '<circle cx="12" cy="12" r="9"/><path d="M15 9.2c-.6-1-1.7-1.6-3-1.6-1.7 0-3 1-3 2.3 0 3 6 1.6 6 4.4 0 1.4-1.4 2.4-3.1 2.4-1.4 0-2.6-.6-3.2-1.7"/>',
    bars: '<path d="M4 20v-6M8 20V8M12 20v-9M16 20V5M20 20v-7"/>',
    gauge: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M13.5 10.5 17 7"/>',
    tube: '<rect x="8" y="3" width="8" height="14" rx="4"/><path d="M10 21h4M11 17v4M13 17v4M10.5 7v6M13.5 7v6"/>',
    voices: '<circle cx="8" cy="8" r="3"/><circle cx="16" cy="8" r="3"/><path d="M2.5 20c0-3 2.5-5 5.5-5s5.5 2 5.5 5M10.5 20c0-3 2.5-5 5.5-5s5.5 2 5.5 5"/>',
    rings: '<circle cx="9" cy="12" r="6"/><circle cx="15" cy="12" r="6"/>',
    rings3: '<circle cx="12" cy="8" r="5"/><circle cx="8" cy="15" r="5"/><circle cx="16" cy="15" r="5"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    arcs: '<path d="M4 18a8 8 0 0 1 16 0M7 18a5 5 0 0 1 10 0M10 18a2 2 0 0 1 4 0"/><path d="M2 18h20"/>',
    out: '<path d="M4 20v-4M8 20v-7M12 20V9M16 20V6M20 20V3"/>',
    gate: '<path d="M2 18h5V7h10v11h5"/><path d="M7 12h10"/>',
    multi: '<rect x="3" y="10" width="4" height="10" rx="1"/><rect x="10" y="5" width="4" height="15" rx="1"/><rect x="17" y="12" width="4" height="8" rx="1"/>',
    spike: '<path d="M2 17h5l2-13 2 13 2-6 2 6h7"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/>',
    speaker: '<path d="M4 9h4l5-4v14l-5-4H4z"/><path d="M16.5 9a4 4 0 0 1 0 6M19 6.5a7.5 7.5 0 0 1 0 11"/>',
    sine: '<path d="M2 12c2.5-7 5-7 7.5 0s5 7 7.5 0 3-3 5-3"/>',
    reel: '<circle cx="7" cy="12" r="4"/><circle cx="17" cy="12" r="4"/><path d="M7 16h10M3 20h18"/>',
    clip: '<path d="M2 12c1.5-4 3-6 4.5-6h11c1.5 0 3 2 4.5 6"/><path d="M2 12c1.5 4 3 6 4.5 6"/>',
    star: '<path d="m12 3 2.6 5.6 6 .7-4.5 4.1 1.2 6L12 16.4 6.7 19.4l1.2-6-4.5-4.1 6-.7z"/>',
    cube: '<path d="M12 2 21 7v10l-9 5-9-5V7z"/><path d="M3 7l9 5 9-5M12 12v10"/>',
    ceiling: '<path d="M3 5h18"/><path d="M3 19c3 0 3-11 6-11s3 11 6 11 3-8 6-8"/>',
    arrowup: '<path d="M3 20h18"/><path d="M6 16V9M12 16V5M18 16v-6"/><path d="m4.5 10.5 1.5-2 1.5 2M10.5 6.5 12 4.5l1.5 2M16.5 11.5l1.5-2 1.5 2"/>',
    layers: '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 13 9 5 9-5"/><path d="m3 17 9 5 9-5" opacity=".6"/>',
    balance: '<path d="M12 3v18M5 21h14M4 9h16"/><path d="m4 9-2.5 5h5zM20 9l-2.5 5h5z"/>',
    notch: '<path d="M2 8c4 0 6 0 8 2l2 7 2-7c2-2 4-2 8-2"/>',
    lowwave: '<path d="M2 12c3-8 7-8 10 0s7 8 10 0"/><path d="M2 20h20" opacity=".5"/>',
    link: '<path d="M9.5 14.5 14.5 9.5"/><path d="M11 6.5 13 4.5a4 4 0 0 1 6.5 6.5l-2 2M13 17.5l-2 2A4 4 0 0 1 4.5 13l2-2"/>',
    maxbars: '<path d="M3 4h18" stroke-dasharray="2 2"/><path d="M5 20v-6M9 20V9M13 20V6M17 20V8M21 20v-5"/>',
    eqdip: '<path d="M2 18c2 0 3-9 6-9s3 2 5 5 3 1 4-1 3-5 5-5"/>',
    honey: '<path d="m12 2.5 8.2 4.75v9.5L12 21.5l-8.2-4.75v-9.5z"/><path d="M10 15.5V8.5l5-1.3v7"/><circle cx="8.6" cy="15.6" r="1.6"/><circle cx="13.6" cy="14.4" r="1.6"/>',
    note: '<path d="M9 18V5l11-2v13"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="17.5" cy="16" r="2.5"/>',
    grid: '<path d="M4 4h4v4H4zM10 4h4v4h-4zM16 4h4v4h-4zM4 10h4v4H4zM10 10h4v4h-4zM16 10h4v4h-4zM4 16h4v4H4zM10 16h4v4h-4zM16 16h4v4h-4z"/>',
    spark: '<path d="M12 2v6M12 16v6M2 12h6M16 12h6M5 5l4 4M15 15l4 4M5 19l4-4M15 9l4-4"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M12 2.5v3M12 18.5v3M21.5 12h-3M5.5 12h-3M18.7 5.3l-2.1 2.1M7.4 16.6l-2.1 2.1M18.7 18.7l-2.1-2.1M7.4 7.4 5.3 5.3"/>',
    chain: '<path d="M4 4h16M4 10h16M4 16h16" opacity=".5"/><path d="m3 20 9-4 9 4"/>',
    coach: '<path d="M2 9 12 4l10 5-10 5z"/><path d="M6 11v5c0 1.5 3 3 6 3s6-1.5 6-3v-5"/><path d="M22 9v6"/>',
    check: '<circle cx="12" cy="12" r="9"/><path d="m7.5 12 3 3 6-6"/>',
    warn: '<circle cx="12" cy="12" r="9"/><path d="M12 7v6M12 16.5v.5"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.5"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m16.5 16.5 4.5 4.5"/>',
    back: '<path d="M10 6 4 12l6 6"/><path d="M4 12h12a4 4 0 0 1 4 4v2"/>',
    left: '<path d="m15 5-7 7 7 7"/>',
    right: '<path d="m9 5 7 7-7 7"/>',
    down: '<path d="m5 9 7 7 7-7"/>',
    bulb: '<path d="M9 18h6M10 21h4M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.3 1 2.1h5c0-.8.4-1.6 1-2.1A6 6 0 0 0 12 3z"/>',
    loud: '<path d="M3 12h2l2-5 3 10 3-14 3 16 2-7h3"/>',
    pulse: '<path d="M2 12h4l2-5 3 10 2-5h9"/>',
    undo: '<path d="M9 7 4 12l5 5"/><path d="M4 12h11a5 5 0 0 1 0 10h-2"/>',
    redo: '<path d="m15 7 5 5-5 5"/><path d="M20 12H9a5 5 0 0 0 0 10h2"/>',
    power: '<path d="M12 3v8"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/>',
    save: '<path d="M5 4h11l3 3v13H5z"/><path d="M8 4v5h7V4"/><path d="M8 20v-6h8v6"/>',
    grip: '<path d="M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01" stroke-width="3"/>',
    close: '<path d="M6 6l12 12M18 6 6 18"/>',
  };
  // where the art is, from the page (the plug-ins serve it flat by file name; the preview page passes its own path)
  let ART = "rack/";
  const icon = (name, cls = "") => `<svg class="sr-ic ${cls}" viewBox="0 0 24 24" aria-hidden="true">${ICONS[name] || ICONS.wave}</svg>`;

  // ---- the modules (the same words the plug-ins' manuals use) -----------------------------------------------------
  // graph: "eq" (a curve from bands), "gr" (gain reduction over time) or "level". art: the browser card's picture.
  const M = (plug, name, hex, cats, ic, graph, art, desc, uses, controls) => ({ plug, name, hex, cats, icon: ic, graph, art, desc, uses, controls });
  const CATALOG = {
    "ht.tune": M("honeytune", "Honey Tune", "HONEY TUNE", ["vocal"], "honey", "level", "notes",
      "Note-by-note tuning on the clip, before every insert: move notes, steady wobbly ones, keep the drift and vibrato you like.",
      ["Fixing single notes by hand", "Steadying wobbly notes (Note Drift and Note Vibrato at 0)", "Tuning a part exactly"],
      ["Key", "Scale", "Keep Drift", "Keep Vibrato"]),

    "vox.pitch": M("voxology", "Pitch", "PITCH", ["vocal"], "mic", "level", "wave",
      "Auto-tune: hears the note you sing and pulls it onto your key, from natural tuning to the hard robot effect. Your voice's tone stays the same.",
      ["In-tune singing that still sounds like you (Natural)", "The familiar auto-tune glide (Classic)", "The hard, stepped trap effect (Robot)"],
      ["Mode", "Amount", "Retune", "Vibrato", "Humanize", "Formant"]),
    "vox.cleanup": M("voxology", "Cleanup", "CLEANUP", ["vocal", "utility"], "wave", "level", "noise",
      "Cleans the recording first: steady noise and room echo turned down, rumble cut, pops, mouth clicks and breaths tamed, the gaps quieted. LISTEN in Voxology plays only what it takes out.",
      ["Home recordings with hiss, fan or AC noise", "A roomy vocal that should sound closer", "Pops, clicks and loud breaths"],
      ["Noise", "Room", "Low Cut", "Pops", "Clicks", "Breaths"]),
    "vox.eq": M("voxology", "Tone EQ", "TONE EQ", ["vocal", "mixing"], "sliders", "eq", "curve",
      "Shapes the vocal's tone in five bands: body, mud, nasal, presence and air.",
      ["Clearing mud (200 - 500 Hz)", "Presence so the words cut through", "Air on top for a modern sheen"],
      ["Body", "Mud", "Nasal", "Presence", "Air"]),
    "vox.dyneq": M("voxology", "Dynamic EQ", "DYN EQ", ["vocal", "dynamics"], "dyneq", "eq", "dip",
      "Cuts boom, mud, honk and harshness only when they jump out; Smooth turns down narrow ringing resonances while they ring. LISTEN in Voxology plays only what it cuts.",
      ["Harsh notes that only bite when you push", "Boomy low notes", "Whistly or nasal resonances (Smooth)"],
      ["Max Cut", "Frequency", "Sensitivity", "Smooth"]),
    "vox.deess": M("voxology", "De-Esser", "DE-ESS", ["vocal", "dynamics"], "s", "gr", "sss",
      "Tames sharp s, t, sh and ch sounds without dulling the voice. LISTEN in Voxology plays only the s sounds it takes out.",
      ["Bright vocals with piercing s sounds", "After a presence boost", "Before reverb and delay, so they don't splash"],
      ["Amount", "Sensitivity", "Frequency"]),
    "vox.rider": M("voxology", "Rider", "RIDER", ["vocal", "dynamics"], "bars", "gr", "bars",
      "Rides the level word by word, like a hand on the fader, so quiet words don't get lost.",
      ["Words that drop under the beat", "Verses with loud and soft lines", "Less work for the compressor"],
      ["Target", "Range", "Speed"]),
    "vox.comp": M("voxology", "Compressor", "COMP", ["vocal", "dynamics"], "gauge", "gr", "gauge",
      "Evens the performance: Peak catches the spikes, the leveler holds the rest steady, Mix blends it in parallel. Style: Clean, FET (fast, upfront) or Opto (smooth, slow to let go).",
      ["A steady, upfront vocal (FET for rap)", "Smooth sung lines (Opto)", "Parallel punch with Mix"],
      ["Style", "Peak", "Threshold", "Ratio", "Makeup", "Mix"]),
    "vox.sat": M("voxology", "Saturation", "SAT", ["vocal", "creative"], "tube", "level", "tube",
      "Adds harmonics for warmth, density and presence: Tape, Tube or Clip. Air adds sparkle on top (harmonics above 8 kHz).",
      ["A thin vocal that needs body", "Cutting through a dense beat", "An open, expensive top (Air)"],
      ["Mode", "Drive", "Mix", "Air"]),
    "vox.voices": M("voxology", "Voices", "VOICES", ["vocal", "creative"], "voices", "level", "stack",
      "Doubles, extra voices and harmonies in key, up to 8 voices, or the chords you play in over MIDI.",
      ["A wider, bigger hook", "Harmonies without singing them", "Stacks for a choir feel"],
      ["Double", "Width", "Voice 1", "Voice 2", "Stack", "Level"]),
    "vox.dimension": M("voxology", "Dimension", "DIMENSION", ["vocal", "creative"], "rings3", "level", "swirl",
      "Movement and width on the finished vocal: chorus, flanger or phaser.",
      ["A lush, wide hook", "Movement on long notes", "Effect sections"],
      ["Mode", "Amount", "Speed", "Width"]),
    "vox.delay": M("voxology", "Delay", "DELAY", ["vocal", "space"], "clock", "level", "echo",
      "Echoes locked to the song's tempo; they duck while you sing, and Throw puts an echo on each line's last word. Character: Clean, Tape (wavering, warm) or Analog (dark, soft).",
      ["Depth without washing out the words", "Echo throws at the end of lines", "Lo-fi tape echoes"],
      ["Character", "Time", "Feedback", "Mix", "Tone", "Duck", "Throw"]),
    "vox.reverb": M("voxology", "Reverb", "REVERB", ["vocal", "space"], "arcs", "level", "arcs",
      "A space for the voice that blooms in the gaps: Plate, Room, Hall or Chamber; Throw adds a wash after each line.",
      ["Placing the vocal in a polished space", "A close room for rap, a hall for sung lines", "A wash after each line (Throw)"],
      ["Type", "Decay", "Pre-delay", "Mix", "Tone", "Duck", "Throw"]),
    "vox.output": M("voxology", "Output", "OUTPUT", ["vocal", "utility"], "out", "level", "meter",
      "The vocal's final level into your mix, and PEAKS: Clip rounds off, or Limit turns down, whatever reaches past the Ceiling.",
      ["Matching the level to before Voxology", "Gain staging into the mixer", "Keeping loud words from clipping"],
      ["Output", "Peaks", "Ceiling"]),
    "vox.beat": M("voxology", "Beat Listener", "BEAT", ["utility"], "note", "level", "notes",
      "Voxology in Beat mode on the beat: it listens for the song's key and shares it with your vocals and Honey Tune; its Unmask dips the beat where the vocal sits.",
      ["Every vocal tuned to the beat's key", "Honey Tune's Auto key"],
      ["Key", "Scale"]),

    "mix.assist": M("mixology", "Assist EQ", "ASSIST EQ", ["mixing", "utility"], "eqdip", "eq", "dip",
      "The Link's low cut and two dips that Mixology's Clean Up and Vocal Pocket set: rumble, mud (200 - 600 Hz), and a pocket (1 - 6 kHz) where the vocal's words sit.",
      ["Rumble under a track", "Mud that builds up across tracks", "Room for the vocal's words"],
      ["Low Cut", "Mud", "Pocket"]),
    "mix.gate": M("mixology", "Gate", "GATE", ["mixing", "dynamics"], "gate", "gr", "gate",
      "Turns the track down between notes: Gate closes by the full range, Expander turns quiet parts down gently.",
      ["Noise between drum hits", "Bleed between phrases"],
      ["Threshold", "Range", "Attack", "Release"]),
    "mix.eq": M("mixology", "EQ", "EQ", ["mixing"], "sliders", "eq", "curve",
      "Six bands: bells, shelves, low and high cut, in Precision, Smooth or Natural style.",
      ["Cutting rumble and mud", "A boxy kick or snare", "A dull or harsh top"],
      ["Frequency", "Gain", "Q", "Shape"]),
    "mix.comp": M("mixology", "Compressor", "COMP", ["mixing", "dynamics"], "gauge", "gr", "gauge",
      "Holds the level steady: threshold, ratio, attack, release, makeup and knee.",
      ["Bass, keys or guitars that swing in level", "Punch on drums", "Control without squashing"],
      ["Threshold", "Ratio", "Attack", "Release", "Makeup", "Knee"]),
    "mix.multi": M("mixology", "Multiband", "MULTIBAND", ["mixing", "dynamics"], "multi", "gr", "bars",
      "Lows, mids and highs, each held together by its own compressor.",
      ["An 808 whose low end jumps from note to note", "Taming one range without the rest"],
      ["Low", "Mid", "High", "Crossovers"]),
    "mix.trans": M("mixology", "Transient", "TRANSIENT", ["mixing", "dynamics"], "spike", "level", "spike",
      "Shapes the front (attack) and the tail (sustain) of each hit.",
      ["A squashed kick or snare", "Spiky hits that poke out", "More or less room sound"],
      ["Attack", "Sustain", "Speed", "Mix"]),
    "mix.tone": M("mixology", "Tone Balance", "TONE", ["mixing"], "sun", "level", "curve",
      "Gentle Warmth, Presence and Air, with a little Glue and Punch.",
      ["A broad tilt of the tone", "A dull or sharp top"],
      ["Warmth", "Presence", "Air", "Glue", "Punch"]),
    "mix.loud": M("mixology", "Loudness", "LOUDNESS", ["mixing"], "speaker", "level", "meter",
      "Trim, Density (a blended-in compressor for a fuller sound) and Low End / Top End shelves.",
      ["A fuller, denser track", "Quick low and top shelves"],
      ["Trim", "Density", "Low End", "Top End"]),
    "mix.harm": M("mixology", "Harmonics", "HARMONICS", ["mixing", "creative"], "sine", "level", "wave",
      "Adds harmonics: warm (even) or edgy (odd), brighter or darker.",
      ["Bass you can hear on small speakers", "Warmth or edge"],
      ["Drive", "Even / Odd", "Tone", "Mix"]),
    "mix.analog": M("mixology", "Analog Touch", "ANALOG", ["mixing", "creative"], "reel", "level", "tube",
      "Tape (soft saturation, a low bump, a softer top), Console (a subtle edge) and tape Noise.",
      ["A warmer, glued sound", "Vintage character"],
      ["Tape", "Console", "Noise", "Mix"]),
    "mix.clip": M("mixology", "Clip Control", "CLIP", ["mixing", "dynamics"], "clip", "gr", "clip",
      "Rounds off peaks over the threshold, smoothly or harder.",
      ["Drum peaks that poke out", "More level without a limiter"],
      ["Threshold", "Softness", "Oversampling", "Mix"]),
    "mix.stereo": M("mixology", "Width", "WIDTH", ["mixing"], "rings", "level", "rings",
      "Stereo width overall and for the lows, mids and highs; 100 % is as it is.",
      ["Lows in the middle", "A narrow keys or synth a little wider", "A very wide track that falls apart in mono"],
      ["Width", "Low Width", "Mid Width", "High Width"]),
    "mix.final": M("mixology", "Final Touch", "FINAL", ["mixing", "utility"], "star", "level", "meter",
      "Width, Air, Glue and Output; on the Full Mix its Output is what Make Room turns down for Masterology.",
      ["Room for mastering (on the Full Mix)", "A last touch of air or glue"],
      ["Width", "Air", "Glue", "Output"]),
    "mix.space": M("mixology", "Space", "SPACE", ["space"], "cube", "level", "arcs",
      "Up to three effects at once (Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX), each with its own Mix, ducking under the tracks you pick.",
      ["A small room on a snare", "A hall on keys that ducks under the vocal", "Delay throws"],
      ["Effects", "Mix", "Ducking", "Effect EQ"]),
    "mix.limit": M("mixology", "Limiter", "LIMITER", ["mixing", "dynamics"], "ceiling", "gr", "maxbars",
      "Nothing passes the ceiling. On the Full Mix, leave the final limiting to Masterology.",
      ["Catching peaks on a track", "Safety on a bus"],
      ["Ceiling", "Release"]),
    // Space's effects: cards in the browser (they run inside Space).
    "mix.fx.reverb": M("mixology", "Reverb", "REVERB", ["space"], "arcs", "level", "arcs", "A lush, flexible reverb inside Space.", ["Keys, guitars and pads", "A send-style wash on any track"], ["Decay", "Pre-delay", "Mix"]),
    "mix.fx.delay": M("mixology", "Delay", "DELAY", ["space", "creative"], "clock", "level", "echo", "Tempo-synced or free delay inside Space.", ["Echoes on leads and ad-libs", "Width with ping-pong"], ["Time", "Feedback", "Mix"]),
    "mix.fx.chorus": M("mixology", "Chorus", "CHORUS", ["creative"], "rings3", "level", "swirl", "Three modulated voices per side: rich, moving width.", ["Wider keys and guitars", "Movement on pads"], ["Rate", "Depth", "Mix"]),
    "mix.fx.phaser": M("mixology", "Phaser", "PHASER", ["creative"], "sine", "level", "swirl", "Six sweeping stages with feedback: classic phasing.", ["Movement on keys or synths", "Effect sections"], ["Rate", "Depth", "Feedback", "Mix"]),
    "mix.fx.hall": M("mixology", "Hall", "HALL", ["space"], "arcs", "level", "arcs", "A large, open hall.", ["Big ballads", "Pads and strings"], ["Decay", "Mix"]),
    "mix.fx.room": M("mixology", "Room", "ROOM", ["space"], "cube", "level", "arcs", "A natural room: ambience and early reflections.", ["Drums that sound played in a room", "A little life on dry tracks"], ["Size", "Mix"]),
    "mix.fx.special": M("mixology", "Special FX", "SPECIAL FX", ["space", "creative"], "spark", "level", "swirl", "Shimmer, Drift, Swell and Freeze: pitched echoes and spaces for effect moments.", ["Intros and breakdowns", "Ear candy on a hook"], ["Shift", "Feedback", "Motion", "Tone", "Mix"]),

    "mas.unlimiter": M("masterology", "Unlimiter", "UNLIMITER", ["mastering", "dynamics"], "arrowup", "level", "spike",
      "Puts back the punch a limiter or clipper squashed out of a beat or mix: it lifts the start of each hit.",
      ["A beat that was already limited", "Bringing back contrast between hits"],
      ["Amount", "Length", "Focus"]),
    "mas.tonaleq": M("masterology", "Tonal EQ", "TONAL EQ", ["mastering"], "sliders", "eq", "curve",
      "Shapes the master's overall tone in seven bands, up to 9 dB each; 1 - 3 dB goes a long way.",
      ["The overall tone against the style", "A little more low end or air"],
      ["Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air"]),
    "mas.stemeq": M("masterology", "Stem EQ", "STEM EQ", ["mastering"], "layers", "level", "stack",
      "Turns the Vocals, Drums, Bass or Other up or down inside the finished mix, like moving one fader in the session.",
      ["A vocal a little buried in the master", "An 808 that's too loud"],
      ["Vocals", "Drums", "Bass", "Other"]),
    "mas.steady": M("masterology", "Tone Steady", "STEADY", ["mastering"], "balance", "level", "wave",
      "Keeps the tone consistent through the song: a section that drifts muddier, harsher or thinner is nudged back.",
      ["A chorus that turns harsh", "Sections that change tone"],
      ["Amount", "Range", "Speed", "Target"]),
    "mas.resonance": M("masterology", "Resonance Tamer", "RESONANCE", ["mastering"], "notch", "level", "dip",
      "Finds ringing tones that stick out (boxiness, whistly vocal tones, harsh hats) and cuts them only while they ring.",
      ["Boxiness", "Harsh hats", "Whistly vocal tones"],
      ["Amount", "Sensitivity", "Release", "Focus"]),
    "mas.bass": M("masterology", "Bass Control", "BASS", ["mastering"], "lowwave", "level", "lowwave",
      "Keeps the low end solid on every system: lows in mono, the sub tamed, punch kept.",
      ["Lows in mono", "An 808 that holds up on small speakers"],
      ["Mono Below", "Sub Tame", "Punch"]),
    "mas.glue": M("masterology", "Glue", "GLUE", ["mastering", "dynamics"], "link", "gr", "gauge",
      "One compressor for the whole mix, like a bus compressor on a console, so the beat sits as one.",
      ["Drums, 808 and vocal moving together", "A cohesive master"],
      ["Character", "Threshold", "Ratio", "Attack", "Release", "Mix"]),
    "mas.dynamics": M("masterology", "Dynamics", "DYNAMICS", ["mastering", "dynamics"], "multi", "gr", "bars",
      "Four-band compression, each band with its own gain reduction.",
      ["Taming one range of the master", "Control without squashing the whole mix"],
      ["Low", "Lo-Mid", "Hi-Mid", "High"]),
    "mas.imager": M("masterology", "Stereo Imager", "IMAGER", ["mastering"], "rings", "level", "rings",
      "Sets the width of the lows, mids and highs; the centre (vocal, kick, 808) keeps its level and place.",
      ["Wider highs", "Lows kept in the middle"],
      ["Lows", "Mids", "Highs"]),
    "mas.sat": M("masterology", "Saturation", "SAT", ["mastering", "creative"], "tube", "level", "tube",
      "Adds harmonics so bass translates on small speakers and the master feels denser; quiet parts pass untouched.",
      ["808s on phone speakers", "A denser master"],
      ["Mode", "Focus", "Drive", "Mix"]),
    "mas.clipper": M("masterology", "Soft Clipper", "CLIPPER", ["mastering", "dynamics"], "clip", "gr", "clip",
      "Shaves the tallest drum peaks before the Maximizer, so the limiter works less.",
      ["Loud masters that stay punchy"],
      ["Clip Level", "Softness", "Oversampling"]),
    "mas.max": M("masterology", "Maximizer", "MAXIMIZER", ["mastering", "dynamics"], "maxbars", "gr", "maxbars",
      "The final true-peak limiter: makes the master loud and keeps every peak under the ceiling, measured the way streaming services measure.",
      ["Hitting the loudness target", "True peaks under the ceiling"],
      ["Drive", "Input Trim", "Ceiling", "Release", "Bass First"]),
  };
  const SPACE_FX = { reverb: "mix.fx.reverb", delay: "mix.fx.delay", chorus: "mix.fx.chorus", phaser: "mix.fx.phaser", hall: "mix.fx.hall", room: "mix.fx.room", special: "mix.fx.special" };
  const cat = (key) => CATALOG[key] || { plug: "mixology", name: key, hex: key.toUpperCase(), cats: [], icon: "wave", graph: "level", art: "wave", desc: "", uses: [], controls: [] };

  // ---- small helpers -----------------------------------------------------------------------------------------------
  const MINUS = "−";
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const num = (v, d = 1) => (v < 0 ? MINUS : "") + Math.abs(v).toFixed(d);
  // A reading with its unit; a dash while nothing is known (no audio yet, or no plug-in measuring it).
  const dbText = (v, unit, d = 1) => (v == null || !isFinite(v) || v <= -99 ? "\u2014" : num(v, d) + " " + unit);
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const live = (m) => m.state === "on" || m.state === "rest" || m.state === "idle";
  const STATE_WORD = { on: "ON", off: "OFF", idle: "IDLE", rest: "RESTS" };
  const ROLE_ORDER = { vocal: 0, beat: 1, track: 2, fullmix: 8, master: 9 };

  // ---- the view's state ---------------------------------------------------------------------------------------------
  const S = {
    root: null, stage: null, opts: null, timer: 0, data: null, sig: "",
    parts: null, html: {}, detail: {}, down: false, pending: false, eqKey: "",
    page: "rack", track: null, sel: null, unfold: {}, hist: {},
    filter: "all", search: "", card: null, inProject: null,
    theme: "dark",
    held: {}, drag: null, chain: Promise.resolve(), toastTimer: 0, scale: 1,   // stage 2: knobs you turn
    wheel: null, sent: {}, snapTold: {}, toastAct: null,
    undo: [], redo: [], ab: {}, bypass: {}, replaying: false,   // stage 2 step 4: UNDO / REDO, A/B, BYPASS
    order: {},   // Beta 25: what the levels say about the insert order, per track
    coachText: {}, coachObj: {}, coachWhy: null,   // Beta 26: the Coach's reports (parsed once per change)
  };

  // The light / dark view: the plug-in's saved choice, else the last one in this browser (if it keeps any)
  const THEME_KEY = "sonicologyRackTheme";
  function savedTheme() {
    try { const t = window.localStorage.getItem(THEME_KEY); return t === "light" || t === "dark" ? t : null; } catch (e) { return null; }
  }
  function setTheme(t) {
    S.theme = t === "light" ? "light" : "dark";
    if (S.root) S.root.classList.toggle("sr-light", S.theme === "light");
    try { window.localStorage.setItem(THEME_KEY, S.theme); } catch (e) { /* no storage: fine */ }
  }

  // The project's tracks, from the units: { key, name, role, units (in signal order) }.
  function tracks(data) {
    const map = new Map();
    for (const u of data.units || []) {
      const key = u.role === "fullmix" ? "fullmix" : u.role === "master" ? "master" : "t" + (u.hash || u.track);
      if (!map.has(key)) map.set(key, { key, name: u.role === "fullmix" ? "Full Mix" : u.role === "master" ? "Master" : u.track || "(no name)", role: u.role, hash: u.hash || 0, units: [] });
      const t = map.get(key);
      t.units.push(u);
      if (u.role === "vocal" || u.role === "beat") t.role = u.role;
    }
    const list = [...map.values()];
    for (const t of list) t.units.sort((a, b) => PRODUCTS[a.product].order - PRODUCTS[b.product].order);
    list.sort((a, b) => (ROLE_ORDER[a.role] ?? 5) - (ROLE_ORDER[b.role] ?? 5) || a.name.localeCompare(b.name));
    return list;
  }

  function trackOf(data, key) {
    const list = tracks(data);
    return list.find((t) => t.key === key) || null;
  }

  function hostTrackKey(data) {
    const host = S.opts.host;
    const list = tracks(data);
    if (host === "mixology" && list.some((t) => t.key === "fullmix")) return "fullmix";
    if (host === "masterology" && list.some((t) => t.key === "master")) return "master";
    const mine = list.find((t) => t.hash && t.hash === data.hostHash);
    return (mine || list[0] || {}).key || null;
  }

  // Every module of a track in signal order: [{ unit, mod, index, of }].
  function chainOf(t) {
    const out = [];
    for (const u of t.units) (u.modules || []).forEach((m, i) => out.push({ unit: u, mod: m, index: i + 1, of: (u.modules || []).length }));
    return out;
  }

  function findSel(t) {
    if (!t) return null;
    const all = chainOf(t);
    if (S.sel) {
      const hit = all.find((c) => c.unit.id === S.sel.unit && c.mod.key === S.sel.key);
      if (hit) return hit;
    }
    const first = all.find((c) => c.mod.state === "on") || all[0] || null;
    if (first) S.sel = { unit: first.unit.id, key: first.mod.key };
    return first;
  }

  // ---- graphs -------------------------------------------------------------------------------------------------------
  const FS = 48000;
  function bandDb(b, f) {
    // RBJ biquads, evaluated at f (for drawing only).
    const w0 = (2 * Math.PI * clamp(b.hz, 10, 22000)) / FS;
    const A = Math.pow(10, (b.db || 0) / 40);
    const q = b.q || 0.71;
    const alpha = Math.sin(w0) / (2 * q);
    const cw = Math.cos(w0);
    let b0, b1, b2, a0, a1, a2;
    if (b.type === "lowShelf" || b.type === "highShelf") {
      const s = b.type === "lowShelf" ? 1 : -1;
      const sq = 2 * Math.sqrt(A) * alpha;
      b0 = A * ((A + 1) - s * (A - 1) * cw + sq);
      b1 = 2 * s * A * ((A - 1) - s * (A + 1) * cw);
      b2 = A * ((A + 1) - s * (A - 1) * cw - sq);
      a0 = (A + 1) + s * (A - 1) * cw + sq;
      a1 = -2 * s * ((A - 1) + s * (A + 1) * cw);
      a2 = (A + 1) + s * (A - 1) * cw - sq;
    } else if (b.type === "lowCut" || b.type === "highCut") {
      const hp = b.type === "lowCut";
      b0 = hp ? (1 + cw) / 2 : (1 - cw) / 2;
      b1 = hp ? -(1 + cw) : 1 - cw;
      b2 = b0;
      a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else {
      b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
      a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    }
    const w = (2 * Math.PI * f) / FS;
    const c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
    const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    const mag = Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    const order = b.slope && (b.type === "lowCut" || b.type === "highCut") ? Math.max(1, Math.round(b.slope / 12)) : 1;
    return 20 * Math.log10(Math.max(mag, 1e-9)) * order;
  }

  // EQ bands as the concept draws them: gold where a band lifts, red where it cuts hard (with a red line down from its
  // point and the spectrum tinted under it), blue for gentle cuts and low / high cuts.
  const isCut = (b) => b.type === "lowCut" || b.type === "highCut";
  const bandTone = (b) => (isCut(b) ? "blue" : (b.db || 0) >= 0.5 ? "gold" : (b.db || 0) <= -2 ? "red" : "blue");
  const BAND_WORD = { lowCut: "Low Cut", highCut: "High Cut", lowShelf: "Low Shelf", highShelf: "High Shelf" };
  const bandName = (b, i) => b.name || BAND_WORD[b.type] || "Band " + (i + 1);
  const TONE_RGB = { gold: "242, 196, 84", blue: "58, 162, 255", red: "255, 46, 69" };

  function eqGraph(mod, w, h) {
    const bands = (mod.bands || []).filter((b) => b.on !== false);
    const fx = (f) => (Math.log10(clamp(f, 20, 20000) / 20) / 3) * w;
    const bells = bands.filter((b) => !isCut(b));
    const top = Math.max(12, Math.ceil(Math.max(0, ...bells.map((b) => b.db || 0)) / 6) * 6);
    const bot = -Math.max(18, Math.ceil(Math.max(0, ...bells.map((b) => -(b.db || 0))) / 6) * 6);
    const gy = (db) => 6 + ((top - clamp(db, bot, top)) / (top - bot)) * (h - 12);
    const y0 = gy(0);

    let defs = "";
    for (const t of Object.keys(TONE_RGB)) {
      defs += `<linearGradient id="sr-up-${t}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgb(${TONE_RGB[t]})" stop-opacity=".62"/><stop offset="1" stop-color="rgb(${TONE_RGB[t]})" stop-opacity=".10"/></linearGradient>`;
      defs += `<linearGradient id="sr-dn-${t}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="rgb(${TONE_RGB[t]})" stop-opacity=".10"/><stop offset="1" stop-color="rgb(${TONE_RGB[t]})" stop-opacity=".66"/></linearGradient>`;
    }
    defs += `<linearGradient id="sr-spec" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3fa8ff" stop-opacity=".85"/><stop offset=".55" stop-color="#1c64d8" stop-opacity=".45"/><stop offset="1" stop-color="#0b2a80" stop-opacity=".25"/></linearGradient>`;
    defs += `<linearGradient id="sr-spec-red" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff5a6a" stop-opacity=".95"/><stop offset="1" stop-color="#a0001a" stop-opacity=".45"/></linearGradient>`;
    defs += `<linearGradient id="sr-redcol" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff2e45" stop-opacity=".22"/><stop offset="1" stop-color="#ff2e45" stop-opacity="0"/></linearGradient>`;
    defs += `<linearGradient id="sr-redfade" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#fff" stop-opacity="0"/><stop offset=".5" stop-color="#fff" stop-opacity="1"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>`;

    let grid = "";
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) grid += `<line x1="${fx(f)}" y1="0" x2="${fx(f)}" y2="${h}" class="sr-gl"/>`;
    for (let d = top; d >= bot; d -= 6) grid += `<line x1="0" y1="${gy(d)}" x2="${w}" y2="${gy(d)}" class="sr-gl${d === 0 ? " sr-g0" : ""}"/>`;

    // the spectrum (spiky, glowing); tinted red under hard cuts
    const reds = bands.filter((b) => bandTone(b) === "red");
    let spec = "", clips = "";
    if (Array.isArray(mod.spectrum) && mod.spectrum.length > 4) {
      // dense glowing needles under a soft envelope (the concept's spectrum), in dB on the graph's own scale
      const n = mod.spectrum.length;
      const sy = (v) => gy(clamp(v, bot, top)).toFixed(1);
      let d = `M0 ${h}`, edge = "", needles = "", pen = false;
      mod.spectrum.forEach((v, i) => {
        const x = ((i / (n - 1)) * w).toFixed(1);
        d += ` L${x} ${sy(v)}`;
        const shows = v > bot + 1;   // no line along the floor where there is nothing
        if (shows) { edge += `${pen ? "L" : "M"}${x} ${sy(v)}`; needles += `M${x} ${h}V${sy(v)}`; }
        pen = shows;
      });
      d += ` L${w} ${h} Z`;
      const body = `<path d="${d}" fill="url(#sr-spec)" class="sr-specarea"/><path d="${needles}" class="sr-needles"/><path d="${edge}" class="sr-specedge"/>`;
      spec = `<g class="sr-spec">${body}</g>`;
      reds.forEach((b, i) => {
        const span = Math.pow(2, 0.9 / Math.max(0.4, b.q || 1));
        const x0 = fx(b.hz / span), x1 = fx(b.hz * span);
        clips += `<mask id="sr-rm-${i}" maskUnits="userSpaceOnUse"><rect x="${x0}" y="0" width="${x1 - x0}" height="${h}" fill="url(#sr-redfade)"/></mask>`;
        spec += `<g class="sr-spec sr-spec-red" mask="url(#sr-rm-${i})"><path d="${d}" fill="url(#sr-spec-red)" class="sr-specarea"/><path d="${needles}" class="sr-needles"/><path d="${edge}" class="sr-specedge"/></g>`;
      });
    }
    let cols = "";
    reds.forEach((b, i) => {
      const span = Math.pow(2, 0.9 / Math.max(0.4, b.q || 1));
      cols += `<rect x="${fx(b.hz / span)}" y="${y0}" width="${fx(b.hz * span) - fx(b.hz / span)}" height="${h - y0}" fill="url(#sr-redcol)" mask="url(#sr-rm-${i})"/>`;
    });

    // the response, and the fill between it and 0 dB in the colour of the band that shapes each part
    let curve = "", fills = "";
    if (bands.length) {
      const N = 200;
      const pts = [];
      for (let i = 0; i <= N; i++) {
        const f = 20 * Math.pow(1000, i / N);
        let db = 0, lead = null, most = 0;
        for (const b of bands) {
          const v = bandDb(b, f);
          db += v;
          if (Math.abs(v) > most) { most = Math.abs(v); lead = b; }
        }
        pts.push({ x: fx(f), y: gy(db), db, tone: lead && most > 0.15 ? bandTone(lead) : null });
      }
      curve = `<path d="${pts.map((p, i) => (i ? "L" : "M") + p.x.toFixed(1) + " " + p.y.toFixed(1)).join(" ")}" class="sr-curve"/>`;
      let i = 0;
      while (i < pts.length) {
        let j = i;
        const up = pts[i].db >= 0;
        while (j + 1 < pts.length && pts[j + 1].tone === pts[i].tone && (pts[j + 1].db >= 0) === up) j++;
        if (pts[i].tone && j > i) {
          const seg = pts.slice(i, j + 2 <= pts.length ? j + 2 : j + 1);
          const d = `M${seg[0].x.toFixed(1)} ${y0} ` + seg.map((p) => `L${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(" ") + ` L${seg[seg.length - 1].x.toFixed(1)} ${y0} Z`;
          fills += `<path d="${d}" fill="url(#sr-${up ? "up" : "dn"}-${pts[i].tone})"/>`;
        }
        i = j + 1;
      }
    }

    // each band's point (round, glossy: HTML on top of the stretched drawing), a red line down from a hard cut
    let lines = "", dots = "";
    bands.forEach((b) => {
      const x = fx(b.hz);
      const y = isCut(b) ? gy(-3) : gy(b.db || 0);
      const tone = bandTone(b);
      if (tone === "red") lines += `<line x1="${x}" y1="${y}" x2="${x}" y2="${h}" class="sr-redline"/>`;
      dots += `<i class="sr-eqdot sr-eqdot-${tone}" style="left:${((x / w) * 100).toFixed(2)}%;top:${((y / h) * 100).toFixed(2)}%"></i>`;
    });

    const labels = [20, 50, 100, 200, 500, "1k", "2k", "5k", "10k", "20k"].map((t) => {
      const f = typeof t === "number" ? t : parseFloat(t) * 1000;
      return `<span style="left:${(fx(f) / w) * 100}%">${t}</span>`;
    }).join("");
    let dbl = "";
    for (let d = top; d >= bot; d -= 6) dbl += `<span style="top:${(gy(d) / h) * 100}%">${d > 0 ? "+" + d : d < 0 ? MINUS + -d : 0}</span>`;
    return `<div class="sr-graph sr-graph-eq"><div class="sr-ylab">${dbl}</div><svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><defs>${defs}${clips}</defs>${grid}${spec}${cols}${fills}${lines}${curve}</svg>${dots}<div class="sr-xlab">${labels}</div></div>`;
  }

  function bandChips(mod) {
    const bands = (mod.bands || []).filter((b) => b.on !== false);
    if (!bands.length) return "";
    return `<div class="sr-bands"><b>BANDS</b>${bands.map((b, i) => `<span class="sr-band sr-band-${bandTone(b)}">${i + 1}&nbsp; ${esc(bandName(b, i))}</span>`).join("")}</div>`;
  }

  function historyGraph(key, kind, w, h) {
    const hist = S.hist[key] || [];
    const n = 120;
    const pts = hist.slice(-n);
    const gr = kind === "gr";
    const yOf = (v) => (gr ? clamp(-v / 12, 0, 1) * (h - 10) + 5 : h - clamp((v + 60) / 60, 0, 1) * (h - 10) - 5);
    let d = "";
    pts.forEach((v, i) => { d += (i ? " L" : "M") + ((i + n - pts.length) / (n - 1) * w).toFixed(1) + " " + yOf(v).toFixed(1); });
    const base = gr ? 5 : h - 5;
    const area = d ? `<path d="${d} L${w} ${base} L${((n - pts.length) / (n - 1) * w).toFixed(1)} ${base} Z" class="${gr ? "sr-grfill" : "sr-lvfill"}"/>` : "";
    let grid = "";
    for (let i = 1; i < 4; i++) grid += `<line x1="0" y1="${(h / 4) * i}" x2="${w}" y2="${(h / 4) * i}" class="sr-gl"/>`;
    const lab = gr ? ["0", MINUS + "3", MINUS + "6", MINUS + "9", MINUS + "12"] : ["0", MINUS + "15", MINUS + "30", MINUS + "45", MINUS + "60"];
    const yl = lab.map((t, i) => `<span style="top:${(i / 4) * 100}%">${t}</span>`).join("");
    return `<div class="sr-graph sr-graph-hist"><div class="sr-ylab">${yl}</div><svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">${grid}${area}<path d="${d}" class="${gr ? "sr-grline" : "sr-lvline"}"/></svg><div class="sr-xlab sr-xlab-time"><span style="left:0%">${MINUS}12 s</span><span style="left:50%">${MINUS}6 s</span><span style="left:100%">now</span></div></div>`;
  }

  // ---- knobs --------------------------------------------------------------------------------------------------------
  function knobArc(norm, bi) {
    const ang = -135 + norm * 270;
    const r = 41, c = 50;
    const arc = (a0, a1) => {
      const p = (a) => [c + r * Math.sin((a * Math.PI) / 180), c - r * Math.cos((a * Math.PI) / 180)];
      const [x0, y0] = p(a0), [x1, y1] = p(a1);
      return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
    };
    const from = bi ? 0 : -135;
    const lit = Math.abs(ang - from) < 0.5 ? "" : `<path d="${arc(Math.min(from, ang), Math.max(from, ang))}" class="sr-karc ${bi && ang < 0 ? "sr-karc-neg" : ""}"/>`;
    return `<path d="${arc(-135, 135)}" class="sr-kdim"/>${lit}`;
  }

  // A knob; turnable (unit + module given) when its plug-in can take a request for it: drag it up / down (Shift: fine),
  // click one with a few options to step through them.
  function knob(v, size, unit, mod) {
    const turn = !!(unit && mod && v.key && canTurn(unit));
    const k = turn ? heldKey(unit, mod.key, v.key) : "";
    const held = turn ? S.held[k] : null;
    const norm = clamp(held ? held.norm : v.norm == null ? 0.5 : v.norm, 0, 1);
    const ang = -135 + norm * 270;
    const attrs = turn ? ` data-knob="${esc(k)}" data-unit="${esc(unit.id)}" data-module="${esc(mod.key)}" data-control="${esc(v.key)}" data-steps="${v.steps || 0}" data-bi="${v.bi ? 1 : 0}" data-norm="${norm}"
      title="${v.steps >= 2 ? "Click for the next option, or drag · mouse wheel: step" : "Drag up / down (Shift: fine) · mouse wheel: nudge · double-click: back to default"}"` : "";
    return `<div class="sr-knob${turn ? " sr-turn" : ""}" style="--sz:${size}px"${attrs}>
      <img class="sr-kring" src="${ART}rack_knob_ring.webp" alt="" draggable="false">
      <svg class="sr-kval" viewBox="0 0 100 100">${knobArc(norm, v.bi)}</svg>
      <img class="sr-kcap" src="${ART}rack_knob_cap.webp" alt="" draggable="false" style="transform:rotate(${ang.toFixed(1)}deg)">
    </div>`;
  }
  const canTurn = (unit) => !!(S.opts && S.opts.request && unit && unit.gen != null && !unit.lock);
  const heldKey = (unit, module, control) => unit.id + "|" + module + "|" + control;

  // ---- pieces --------------------------------------------------------------------------------------------------------
  const pill = (state) => `<span class="sr-pill sr-pill-${state}">${STATE_WORD[state] || state}</span>`;
  const lineIcon = (level) => icon(level === "warn" ? "warn" : level === "info" ? "info" : "check", "sr-li-" + (level || "ok"));

  function header(data) {
    const t = trackOf(data, S.track);
    const list = tracks(data);
    const idx = list.findIndex((x) => x.key === S.track);
    const counts = {};
    for (const u of data.units || []) counts[u.product] = (counts[u.product] || 0) + 1;
    const chips = ["voxology", "honeytune", "link", "mixology", "masterology"].filter((p) => counts[p]).map((p) =>
      `<span class="sr-suite sr-tone-${PRODUCTS[p].tone}" title="${esc(PRODUCTS[p].name)}"><i></i>${esc(SUITE_WORD[p])}${counts[p] > 1 ? ` <b>${counts[p]}</b>` : ""}</span>`).join("");
    const missing = ["voxology", "mixology", "masterology"].filter((p) => !counts[p] && !(p === "mixology" && counts.link));
    const miss = missing.length ? `<span class="sr-suite sr-suite-missing">NOT IN THIS PROJECT: ${missing.map((p) => PRODUCTS[p].name.toUpperCase()).join(", ")}</span>` : "";
    // (a narrow Rack, Beta 33 fix: the bar ran out of room and BACK TO ... was cut off at the right)
    const narrow = (S.W || 1280) < 1440;
    const backTo = "BACK TO " + (HOSTS[S.opts.host] || "PLUG-IN").toUpperCase();
    return `<header class="sr-head ${narrow ? "sr-head-n" : ""}">
      <i class="sr-liquid sr-liquid-l"></i><i class="sr-liquid sr-liquid-r"></i>
      <div class="sr-emblem"><img src="${ART}rack_emblem.webp" alt="Light / dark view" title="Click: ${S.theme === "light" ? "dark" : "light"} view" draggable="false" data-sr="theme"></div>
      <div class="sr-title">
        <img class="sr-lettering" src="${ART}rack_lettering.webp" alt="Sonicology Rack" draggable="false">
        <div class="sr-sub"><span>YOUR WHOLE SUITE</span> IN ONE VIEW</div>
      </div>
      <div class="sr-picker" data-sr="picker">
        <button class="sr-pk-arrow" data-sr="track-prev" ${idx <= 0 ? "disabled" : ""}>${icon("left")}</button>
        <button class="sr-pk-name" data-sr="track-menu"><span>${esc(t ? t.name : "No tracks")}</span>${icon("down")}</button>
        <button class="sr-pk-arrow" data-sr="track-next" ${idx < 0 || idx >= list.length - 1 ? "disabled" : ""}>${icon("right")}</button>
      </div>
      <div class="sr-bar">
        <div class="sr-seg">
          <button class="${S.page === "rack" ? "sr-on" : ""}" data-sr="page" data-page="rack">${icon("chain")}RACK</button>
          <button class="${S.page === "modules" ? "sr-on" : ""}" data-sr="page" data-page="modules">${icon("grid")}MODULES</button>
          <button class="${S.page === "session" ? "sr-on" : ""}" data-sr="page" data-page="session" title="Every track and the modules that play on it, in one list">${icon("layers")}SESSION</button>
          <button class="${S.page === "coach" ? "sr-on" : ""}" data-sr="page" data-page="coach" title="The Coach: ANALYZE &amp; BUILD a vocal's chain (Voxology listens, plans, measures; you apply)">${icon("coach")}COACH</button>
        </div>
        <div class="sr-sep"></div>
        ${tools(data, t)}
        <div class="sr-suites">${chips}${miss}</div>
        <button class="sr-back" data-sr="back" title="${esc(backTo)}">${icon("back")}${esc(narrow ? "BACK" : backTo)}</button>
      </div>
    </header>`;
  }

  // The top bar's tools (RACK page, a track with something the Rack can change): A / B, BYPASS, UNDO / REDO.
  function tools(data, t) {
    if (S.page !== "rack" || !t || !S.opts.request || !t.units.some((u) => canTurn(u))) return "";
    const ab = S.ab[t.key] || { cur: "a" };
    const by = !!S.bypass[t.key];
    return `<div class="sr-tools">
      <div class="sr-seg sr-ab" title="A / B: two versions of this track's settings (what the Rack can change). Click B to start one from A, change things, then flip between them.">
        <button class="${ab.cur === "a" ? "sr-on" : ""}" data-sr="ab" data-ab="a">A</button><button class="${ab.cur === "b" ? "sr-on" : ""}" data-sr="ab" data-ab="b">B</button></div>
      <button class="sr-tool ${by ? "sr-on" : ""}" data-sr="bypass" title="BYPASS: this track's Sonicology plug-ins off (each one's own bypass), to hear before / after. Click again (or BACK) to bring them back. Honey Tune keeps playing what it tuned.">${icon("power")}BYPASS</button>
      <button class="sr-tool sr-tool-ic" data-sr="undo" ${S.undo.length ? "" : "disabled"} title="Undo the Rack's last change${S.undo.length ? ": " + esc(S.undo[S.undo.length - 1].label) : ""}">${icon("undo")}</button>
      <button class="sr-tool sr-tool-ic" data-sr="redo" ${S.redo.length ? "" : "disabled"} title="Redo${S.redo.length ? ": " + esc(S.redo[S.redo.length - 1].label) : ""}">${icon("redo")}</button>
    </div><div class="sr-sep"></div>`;
  }

  function trackMenu(data) {
    const list = tracks(data);
    return `<div class="sr-menu" data-sr="menu">${list.map((t) => `<button class="${t.key === S.track ? "sr-on" : ""}" data-sr="track" data-track="${esc(t.key)}">
      <span class="sr-mt-name">${esc(t.name)}</span><span class="sr-badges">${t.units.map((u) => `<b class="sr-badge sr-tone-${PRODUCTS[u.product].tone}">${PRODUCTS[u.product].badge}</b>`).join("")}</span></button>`).join("")}</div>`;
  }

  function footer(data, unit) {
    const mix = data.mix || {};
    const seg = (db, id) => {
      const lit = clamp((db + 60) / 60, 0, 1);
      return `<div class="sr-meter" data-meter="${id}"><i style="width:${(lit * 100).toFixed(1)}%"></i></div>`;
    };
    const inDb = unit ? unit.inDb : null, outDb = unit ? unit.outDb : null;
    const who = unit ? (unit.role === "fullmix" ? "Full Mix" : unit.role === "master" ? "Master" : unit.track) + " · " + PRODUCTS[unit.product].name : "";
    return `<footer class="sr-foot">
      <div class="sr-io"><b>IN</b>${seg(inDb ?? -100, "in")}<span data-val="in">${dbText(inDb, "dB")}</span></div>
      <div class="sr-loud">
        <div class="sr-loud-t">${icon("loud")}LOUDNESS<small>${esc(mix.source || "the mix")}</small></div>
        <div class="sr-lbox"><b data-val="lufsI">${dbText(mix.lufsI, "LUFS")}</b><small>Integrated</small></div>
        <div class="sr-lbox"><b data-val="lufsS">${dbText(mix.lufsS, "LUFS")}</b><small>Short-Term</small></div>
        <div class="sr-lbox"><b data-val="tp">${dbText(mix.tp, "dBTP")}</b><small>True Peak</small></div>
      </div>
      <div class="sr-io sr-io-out"><b>OUT</b>${seg(outDb ?? -100, "out")}<span data-val="out">${dbText(outDb, "dB")}</span></div>
      <div class="sr-who">${esc(who)}</div>
    </footer>`;
  }

  // ---- page: RACK ----------------------------------------------------------------------------------------------------
  // SIGNAL CHAIN rows (the RACK and COACH pages' left panel): grouped by plug-in, modules that are off folded away.
  function chainRows(t, sel) {
    let rows = "";
    for (const u of t.units) {
      const P = PRODUCTS[u.product];
      const mods = u.modules || [];
      const shown = mods.filter((m) => m.state !== "off" || S.unfold[u.id]);
      const off = mods.length - mods.filter((m) => m.state !== "off").length;
      const save = u.product === "voxology" && canTurn(u)
        ? `<button class="sr-gsave" data-sr="savechain" data-unit="${esc(u.id)}" title="SAVE CHAIN: keep this vocal's whole Voxology setup in Voxology's MY CHAINS (named after the track and the time), to use on your next song">${icon("save")}SAVE</button>` : "";
      // Beta 33: modules the plug-in lets you move (drag the row); RESET ORDER when one is out of the plug-in's own order
      const movable = canTurn(u) && mods.some((m) => m.movable);
      const reset = movable && mods.some((m) => m.moved)
        ? `<button class="sr-fold sr-oreset" data-sr="order-reset" data-unit="${esc(u.id)}" title="${esc(P.name)}'s own order again">${icon("undo")}RESET ORDER</button>` : "";
      rows += `<div class="sr-group sr-tone-${P.tone}"><b class="sr-badge sr-tone-${P.tone}">${P.badge}</b><span>${esc(P.name.toUpperCase())}</span>${u.on === false ? '<em class="sr-bypassed">BYPASSED</em>' : ""}${save}<small>v${esc(u.version || "")}</small></div>`;
      for (const m of shown) {
        const c = cat(m.key);
        const picked = sel && sel.unit.id === u.id && sel.mod.key === m.key;
        const mv = movable && m.movable;
        rows += `<button class="sr-row ${picked ? "sr-picked" : ""} sr-st-${m.state} ${mv ? "sr-movable" : ""} ${m.moved ? "sr-moved" : ""}" data-sr="mod" data-unit="${esc(u.id)}" data-key="${esc(m.key)}"${mv ? ` data-move="1" title="Drag up or down to move ${esc(c.name)} in ${esc(P.name)}'s chain"` : ""}>
          ${icon(c.icon)}<span class="sr-rname">${esc(c.name)}</span>${mv ? icon("grip", "sr-grip") : ""}${pill(m.state)}</button>`;
      }
      if (!mods.length) rows += `<div class="sr-row sr-row-note">Nothing to show yet</div>`;
      if (off > 0 || reset) rows += `<div class="sr-folds">${off > 0 ? `<button class="sr-fold" data-sr="unfold" data-unit="${esc(u.id)}">${S.unfold[u.id] ? "hide the " + off + " off" : "+ " + off + " off"}</button>` : ""}${reset}</div>`;
    }
    return rows;
  }

  function rackPage(data) {
    const t = trackOf(data, S.track);
    if (!t) return `<main class="sr-main sr-empty"><div class="sr-panel sr-wide"><h3>No Sonicology plug-ins are playing</h3><p>Press play in your DAW: plug-ins take part while the song plays, and only in the project that plays.</p></div></main>`;
    const sel = findSel(t);
    const chain = chainOf(t);

    // left: SIGNAL CHAIN
    const rows = chainRows(t, sel);

    // CHAIN FLOW: the modules that play, then where the sound goes next
    const flowMods = chain.filter((c) => live(c.mod));
    let flow = flowMods.map((c, i) => {
      const k = cat(c.mod.key);
      const picked = sel && sel.unit.id === c.unit.id && sel.mod.key === c.mod.key;
      return `${i ? '<i class="sr-arrow"></i>' : ""}<button class="sr-hex ${picked ? "sr-picked" : ""} sr-st-${c.mod.state} sr-tone-${PRODUCTS[c.unit.product].tone}" data-sr="mod" data-unit="${esc(c.unit.id)}" data-key="${esc(c.mod.key)}" title="${esc(k.name)}">
        ${icon(k.icon)}<span>${esc(k.hex)}</span>${c.mod.state === "rest" ? "<em>RESTS</em>" : ""}</button>`;
    }).join("");
    const next = [];
    if (t.role !== "fullmix" && t.role !== "master") next.push(["FADER", "sliders", null]);
    if (t.role !== "fullmix" && t.role !== "master" && trackOf(data, "fullmix")) next.push(["FULL MIX", "star", "fullmix"]);
    if (t.role !== "master" && trackOf(data, "master")) next.push(["MASTER", "maxbars", "master"]);
    flow += next.map(([label, ic, key]) => `<i class="sr-arrow sr-arrow-out"></i><button class="sr-hex sr-hex-next" ${key ? `data-sr="track" data-track="${key}"` : "disabled"}>${icon(ic)}<span>${label}</span></button>`).join("");
    if (!flowMods.length) flow = `<div class="sr-flow-empty">Nothing on this track is processing right now.</div>` + flow;

    // centre: the module
    let detail = "";
    if (sel) {
      const c = cat(sel.mod.key);
      const m = sel.mod;
      const where = `${PRODUCTS[sel.unit.product].name.toUpperCase()} · ${esc((t.name || "").toUpperCase())} · ${sel.index} OF ${sel.of}`;
      const graph = c.graph === "eq" && (m.bands || []).length ? eqGraph(m, 640, 210)
        : historyGraph(sel.unit.id + "|" + m.key, m.meterKind === "gr" || c.graph === "gr" ? "gr" : "level", 640, 210);
      const vals = m.values || [];
      // up to 8 knobs, as big as the centre's width allows (92 px at most); more values as chips
      const nk = Math.min(vals.length, 8);
      const centre = (S.W || 1280) - 684 - 56;
      const ks = Math.round(clamp(centre / Math.max(nk, 1) - 18, 54, 92));
      const knobs = vals.slice(0, nk).map((v, i) => `<div class="sr-kcell" data-vi="${i}"><b>${esc(v.name.toUpperCase())}</b>${knob(v, ks, sel.unit, m)}<span class="sr-kval-t">${esc(v.text)}</span></div>`).join("");
      const more = vals.slice(nk).map((v, i) => `<span class="sr-chip" data-vi="${nk + i}"><b>${esc(v.name)}</b><i>${esc(v.text)}</i></span>`).join("");
      const meterNow = m.meterKind === "gr" ? `<div class="sr-big"><b data-val="meter">${num(m.meter || 0)}</b><small>dB gain reduction</small></div>` : "";
      detail = `<section class="sr-panel sr-mod">
        <div class="sr-mod-head">
          <div class="sr-mod-ic sr-tone-${PRODUCTS[sel.unit.product].tone}">${icon(c.icon)}</div>
          <div class="sr-mod-t"><h2>${esc(c.name.toUpperCase())}</h2><p>${where}</p></div>
          <div class="sr-mod-sum"><span>${esc(m.summary || "")}</span>${m.by ? `<small>set by ${esc(m.by)}</small>` : ""}</div>
          ${m.switchable && canTurn(sel.unit)
            ? `<button class="sr-pill sr-pill-${m.state} sr-switch" data-sr="switch" data-unit="${esc(sel.unit.id)}" data-key="${esc(m.key)}" title="Click: turn ${esc(c.name)} ${m.state === "off" ? "on" : "off"}">${STATE_WORD[m.state] || m.state}</button>`
            : pill(m.state)}
        </div>
        <div class="sr-graphwrap">${graph}${meterNow}</div>
        ${c.graph === "eq" ? bandChips(m) : ""}
        <div class="sr-knobs ${vals.length ? "" : "sr-knobs-none"}">
          ${vals.length ? `<div class="sr-krow">${knobs}</div>` : `<p class="sr-nokn">${m.state === "off" ? "Off: it passes the sound through untouched." : "No settings to show."}</p>`}
          ${more ? `<div class="sr-more">${more}</div>` : ""}
          <div class="sr-readonly ${sel.unit.lock ? "sr-locked" : ""}">${readonlyLine(sel.unit, m)}</div>
        </div>
      </section>`;
    }

    // right: RACK COACH (how they work together, what's playing, about this module)
    const lines = [];
    for (const l of data.together || []) if (!l.hash || l.hash === t.hash || (t.role === "master" && l.hash === "master") || (t.role === "fullmix" && l.hash === "fullmix")) lines.push(l);
    for (const u of t.units) for (const l of u.together || []) lines.push(l);
    const order = orderLine(t);
    if (order) lines.push(order);
    if ((data.otherLayouts || []).length)
      lines.push({ level: "warn", text: "A Sonicology plug-in from another beta is in this project, and the Rack can't see it. Install this beta, so all three plug-ins are the same version." });
    lines.sort((a, b) => (a.level === "warn" ? 0 : a.level === "info" ? 2 : 1) - (b.level === "warn" ? 0 : b.level === "info" ? 2 : 1));
    const together = lines.length ? lines.map((l) => `<li class="sr-tl sr-tl-${l.level || "ok"}">${lineIcon(l.level)}<span>${esc(l.text)}${fixButtons(l)}</span></li>`).join("")
      : `<li class="sr-tl">${lineIcon("info")}<span>Nothing else in the suite touches this track.</span></li>`;
    const overview = flowMods.map((c) => {
      const isSel = sel && c.unit.id === sel.unit.id && c.mod.key === sel.mod.key;   // the picked module, in red (as the concept)
      return `<li class="${isSel ? "sr-ov-sel" : ""}">${icon("check", isSel ? "sr-li-sel" : "sr-li-ok")}<span><b>${esc(cat(c.mod.key).name)}</b> ${MINUS} ${esc(c.mod.summary || STATE_WORD[c.mod.state])}</span></li>`;
    }).join("");
    const about = sel ? (() => {
      const c = cat(sel.mod.key);
      return `<h4>${icon("bulb")}ABOUT ${esc(c.name.toUpperCase())}</h4><p>${esc(c.desc)}</p><ul class="sr-uses">${c.uses.map((u) => `<li>${esc(u)}</li>`).join("")}</ul>`;
    })() : "";

    return `<main class="sr-main sr-main-rack">
      <section class="sr-panel sr-left">
        <h3 class="sr-ph">${icon("layers")}SIGNAL CHAIN<small>${esc(t.name)}</small></h3>
        <div class="sr-honey sr-honey-chain"><div class="sr-rows" data-scroll="rows">${rows}</div></div>
      </section>
      <div class="sr-centre">
        <section class="sr-panel sr-flow"><div class="sr-flow-t">CHAIN<br>FLOW</div><div class="sr-flow-hexes" data-scroll="flow">${flow}</div></section>
        ${detail}
      </div>
      <section class="sr-panel sr-right">
        <h3 class="sr-ph">${icon("coach")}RACK COACH</h3>
        <div class="sr-coach" data-scroll="coach">
          <div class="sr-banner">${icon("pulse")}HOW THEY WORK TOGETHER</div>
          <ul class="sr-together">${together}</ul>
          ${gainLane(t)}
          ${overview ? `<h4>CHAIN OVERVIEW</h4><ul class="sr-overview">${overview}</ul>` : ""}
          ${about}
        </div>
      </section>
    </main>`;
  }

  // GAIN STAGING (the Coach): each plug-in on the track, what goes in and comes out (peaks lately), and what to watch:
  // a plug-in that makes the sound much louder (louder sounds "better": compare at the same level) or peaks near 0 dBFS.
  // The numbers move with the meters (updateLive); the list itself only when the track's plug-ins change.
  function gainLane(t) {
    const units = t.units.filter((u) => u.product !== "honeytune");
    if (!units.length) return "";
    // The track's own fader (Mixology moves it through its DAW bridge): a knob like the others (drag, wheel, double-click
    // for 0 dB, UNDO).
    const f = trackFader(S.data, t);
    const fader = f ? `<li class="sr-gain-fader" title="This track's fader in your DAW, through Mixology's bridge. Drag · mouse wheel · double-click: 0 dB">
      <b>Track fader</b>${knob(f.v, 30, f.unit, f.mod)}<span class="sr-kval-t" data-g="fader">${esc(f.v.text)}</span></li>` : "";
    return `<h4>GAIN STAGING</h4><ul class="sr-gain">${units.map((u) =>
      `<li data-gain="${esc(u.id)}"><b>${esc(PRODUCTS[u.product].name)}</b><span data-g="io">${gainIo(u)}</span><em data-g="d">${gainDelta(u)}</em></li>`).join("")}
      ${fader}<li class="sr-gain-note" data-gain-note>${esc(gainNote(units))}</li></ul>`;
  }
  const known = (db) => db != null && isFinite(db) && db > -99;
  const gainIo = (u) => (known(u.inDb) && known(u.outDb) ? `${num(u.inDb)} \u2192 ${num(u.outDb)}` : "\u2014");
  const gainDelta = (u) => (known(u.inDb) && known(u.outDb) ? (u.outDb - u.inDb >= 0 ? "+" : MINUS) + Math.abs(u.outDb - u.inDb).toFixed(1) : "");
  function gainNote(units) {
    const hot = units.find((u) => known(u.outDb) && u.outDb > -1.0);
    if (hot) return hot.product === "masterology"
      ? `Masterology peaks at ${num(hot.outDb)} dBFS: a Ceiling of ${MINUS}1 dB (on its Maximizer) suits most masters.`
      : `${PRODUCTS[hot.product].name} peaks at ${num(hot.outDb)} dBFS, close to clipping: turn its output down a little.`;
    // Masterology is there to make the mix louder; a big jump anywhere else colours what you hear.
    const loud = units.find((u) => u.product !== "masterology" && known(u.inDb) && known(u.outDb) && u.outDb - u.inDb > 6.0);
    if (loud) return `${PRODUCTS[loud.product].name} makes it ${(loud.outDb - loud.inDb).toFixed(1)} dB louder. Louder sounds better by itself, so compare at the same level` +
      (loud.product === "voxology" ? " (Voxology's MATCH, or its Output)." : ".");
    return units.some((u) => known(u.inDb)) ? "Levels through the chain look healthy." : "Press play to see the levels.";
  }
  function updateGain(t) {
    for (const u of t.units) {
      const row = S.stage.querySelector(`[data-gain="${u.id}"]`);
      if (!row) continue;
      const io = row.querySelector('[data-g="io"]'), d = row.querySelector('[data-g="d"]');
      const a = gainIo(u), b = gainDelta(u);
      if (io.textContent !== a) io.textContent = a;
      if (d.textContent !== b) d.textContent = b;
      row.classList.toggle("sr-gain-warn", (known(u.outDb) && u.outDb > -1.0) || (known(u.inDb) && known(u.outDb) && u.outDb - u.inDb > 6.0));
    }
    const note = S.stage.querySelector("[data-gain-note]");
    const text = gainNote(t.units.filter((u) => u.product !== "honeytune"));
    if (note && note.textContent !== text) note.textContent = text;
  }

  // ---- page: SESSION (every track, the modules that play on it, its warnings) ------------------------------------------
  // ---- page: COACH (stage 3: ANALYZE & BUILD) ----------------------------------------------------------------------
  // For a vocal: Voxology's Auto-Edit listens and works out a plan without changing anything (Change "coach", "plan");
  // the plug-in sends it back as its report (rack::Unit::report): what it heard, what stands out against its aim, every
  // module it would set and why, and the take measured again through the plan. APPLY sets it (one UNDO in Voxology).
  // The Coach for a track: Voxology's on a vocal it processes; else (Beta 28) Mixology's on a track with Mixology Link (the
  // Mixology on the master builds it: its report rides on its own unit, {"track": <hash>, ...}). On the master (Beta 29):
  // Masterology's Auto-Master, the same way (BUILD NOW once it has heard 12 s; the whole song is best).
  function coachOf(t) {
    if (!t) return null;
    const parse = (u) => {
      if (u.report && S.coachText[u.id] !== u.report) {
        S.coachText[u.id] = u.report;
        try { S.coachObj[u.id] = JSON.parse(u.report); } catch (e) { S.coachObj[u.id] = null; }
      }
      return u.report ? S.coachObj[u.id] : null;
    };
    const u = t.units.find((x) => x.product === "voxology" && x.role !== "beat");
    if (u) return { unit: u, r: parse(u) || { state: "idle" }, kind: "vox" };
    if (t.role === "master") {
      const m = t.units.find((x) => x.product === "masterology");
      return m ? { unit: m, r: parse(m) || { state: "idle" }, kind: "master" } : null;
    }
    if (!t.hash || t.role === "fullmix" || t.role === "master" || !t.units.some((x) => x.product === "link")) return null;
    const mix = mixOwner(S.data);
    const r = mix ? parse(mix) : null;
    if (!mix || !r || r.track !== t.hash) return { unit: mix || null, r: { state: "none" }, kind: "mix", missing: !mix };
    return { unit: mix, r, kind: "mix" };
  }
  // The Mixology that owns the mix (Beta 32: with two Mixologys in a project, the one on the master: it moves the faders
  // and sends the Coach's and WHAT TO FIX FIRST's reports).
  function mixOwner(data) {
    const all = ((data && data.units) || []).filter((u) => u.product === "mixology" && u.role === "fullmix");
    return all.find((u) => u.faders) || all.find((u) => u.report) || all[0] || null;
  }
  // A Coach request: Voxology's take the step; Mixology's the step and the track ("plan.<hash>").
  const coachControl = (c, t, step) => (c.kind === "mix" ? step + "." + t.hash : step);
  // STOP while it listens: Mixology drops the Coach's listen; Voxology and Masterology stop the Coach's own listen only.
  const coachStopStep = (c) => (c.kind === "mix" ? "discard" : "cancel");
  // A Coach step goes even while the plug-in is locked (its own listen locks it): the plug-in decides.
  const canStep = (c) => !!(S.opts && S.opts.request && c && c.unit && c.unit.gen != null) && (c.r.state !== "none" || coachAsks(c));
  // Mixology reports the Coach on one track at a time (another Rack may show another): any track can still ask it.
  const coachAsks = (c) => !!c && c.kind === "mix" && !c.missing && !!c.unit;
  const secsText = (v) => { const n = Math.max(0, Math.floor(+v || 0)); return n >= 60 ? `${Math.floor(n / 60)}:${String(n % 60).padStart(2, "0")}` : n + " s"; };
  const COACH_BUSY = { listening: 1, working: 1, busy: 1 };

  // The take's tone (dB per band vs its 500 Hz - 2 kHz average) as a graph: lines [{ values, cls, fill }], issues marked.
  // The plot stretches to its box (lines keep their width); the labels and tags are HTML, so nothing gets squashed.
  function toneGraph(bands, lines, issues) {
    const hz = bands.hz || [];
    if (hz.length < 2) return "";
    const all = lines.flatMap((l) => l.values || []).filter((v) => Number.isFinite(v));
    if (!all.length) return "";
    const top = Math.ceil((Math.max(...all) + 3) / 6) * 6, bottom = Math.floor((Math.min(...all) - 3) / 6) * 6;
    const lo = Math.log(hz[0] / 1.12), hi = Math.log(hz[hz.length - 1] * 1.12);
    const X = (f) => ((Math.log(f) - lo) / (hi - lo)) * 1000;
    const Y = (db) => ((top - db) / Math.max(top - bottom, 6)) * 1000;
    let svg = `<svg class="sr-cg" viewBox="0 0 1000 1000" preserveAspectRatio="none">`, labels = "";
    const gstep = top - bottom > 30 ? 12 : 6;   // a wide range (a whole track's roll-off): fewer labels
    for (let db = top - Math.floor((top - bottom) / gstep) * gstep; db <= top; db += gstep) {
      svg += `<line class="sr-cg-grid" x1="0" x2="1000" y1="${Y(db).toFixed(1)}" y2="${Y(db).toFixed(1)}"/>`;
      labels += `<span class="sr-cg-y" style="top:${(Y(db) / 10).toFixed(1)}%">${db > 0 ? "+" + db : db < 0 ? MINUS + -db : 0}</span>`;
    }
    for (const f of [100, 200, 500, 1000, 2000, 5000, 10000]) labels += `<span class="sr-cg-x" style="left:${(X(f) / 10).toFixed(1)}%">${f >= 1000 ? f / 1000 + "k" : f}</span>`;
    for (const i of issues || []) {
      if (!(i.from > 0 && i.to > 0)) continue;
      const x0 = X(i.from / 1.1), x1 = X(i.to * 1.1);
      svg += `<rect class="sr-cg-zone ${i.warn ? "sr-cg-warn" : "sr-cg-info"}" x="${x0.toFixed(1)}" y="0" width="${Math.max(4, x1 - x0).toFixed(1)}" height="1000"/>`;
    }
    for (const l of lines) {
      const v = l.values || [];
      if (v.length !== hz.length) continue;
      const pts = v.map((d, k) => `${X(hz[k]).toFixed(1)},${Y(d).toFixed(1)}`);
      if (l.fill) svg += `<path class="sr-cg-area ${l.cls}" d="M${X(hz[0]).toFixed(1)},1000 L${pts.join(" L")} L${X(hz[hz.length - 1]).toFixed(1)},1000 Z"/>`;
      svg += `<polyline class="sr-cg-line ${l.cls}" points="${pts.join(" ")}"/>`;
    }
    svg += "</svg>";
    // the issues' tags over their zones; one that would sit on its neighbour steps down a row
    const gap = (S.W || 1280) < 1440 ? 21 : 15;   // % of the plot a tag takes
    let lastX = -100, lastRow = 1;
    const tags = (issues || []).filter((i) => i.from > 0 && i.to > 0).map((i) => ({ i, x: X(Math.sqrt(i.from * i.to)) / 10 })).sort((a, b) => a.x - b.x).map(({ i, x }) => {
      const row = x - lastX < gap && lastRow === 0 ? 1 : 0;
      lastX = x; lastRow = row;
      return `<span class="sr-cg-tag ${i.warn ? "sr-cg-warn" : "sr-cg-info"}" style="left:${clamp(x, 7, 93).toFixed(1)}%;top:${row * 30}px">${esc(i.name)}<small>${esc(hzRange(i.from, i.to))}</small></span>`;
    }).join("");
    return `<div class="sr-cgwrap"><div class="sr-cg-plot">${svg}${labels}${tags}</div></div>`;
  }
  const hzText = (f) => (f >= 1000 ? (Math.round(f / 100) / 10).toString().replace(/\.0$/, "") + "k" : Math.round(f));
  const hzRange = (a, b) => `${hzText(a)}${MINUS}${hzText(b)} Hz`;
  const dbSigned = (v) => (v > 0.05 ? "+" : v < -0.05 ? MINUS : "") + Math.abs(v).toFixed(1);

  function coachPage(data) {
    const t = trackOf(data, S.track);
    if (!t) return rackPage(data);
    const c = coachOf(t);
    const left = guideOn() ? guidePanel(data, true) : `<section class="sr-panel sr-left">
        <h3 class="sr-ph">${icon("layers")}SIGNAL CHAIN<small>${esc(t.name)}</small></h3>
        <div class="sr-honey sr-honey-chain"><div class="sr-rows" data-scroll="rows">${chainRows(t, null)}</div></div>
      </section>`;
    if (!c) {
      const vocals = tracks(data).filter((x) => x.units.some((u) => u.product === "voxology" && u.role !== "beat"));
      return `<main class="sr-main sr-main-coach">${left}
        <section class="sr-panel sr-coach-none"><h3 class="sr-ph">${icon("coach")}AUTO RACK COACH</h3>
          <div class="sr-coach-msg"><p>The Coach builds a track's chain: a vocal's through Voxology (not in BEAT mode), any other track's
            through its Mixology Link (with Mixology on your master channel), the master's through Masterology.</p>
          ${vocals.length ? `<p>Pick a vocal:</p><div class="sr-coach-pick">${vocals.map((v) => `<button class="sr-tool" data-sr="track" data-track="${esc(v.key)}">${icon("mic")}${esc(v.name)}</button>`).join("")}</div>`
            : "<p>No vocal with Voxology is playing in this project.</p>"}</div>
        </section></main>`;
    }
    const mixk = c.kind === "mix", mk = c.kind === "master";
    const r = c.r, plan = r.plan && r.plan.ok ? r.plan : null;
    const engine = mk ? "Auto-Master" : mixk ? "Mixology" : "Auto-Edit";
    const failed = r.plan && !r.plan.ok ? r.plan
      : r.state === "failed" ? { tipTitle: `${engine} couldn't build a plan`, summary: "Something went wrong while it worked.", tip: "Play the song and press ANALYZE AGAIN." } : null;
    const busy = !!COACH_BUSY[r.state];
    const styles = r.styles || [], ints = r.intensities || [];
    const can = !!c.unit && canTurn(c.unit) && !busy && (r.state !== "none" || coachAsks(c));
    const finish = mk && r.state === "listening" && !!r.canFinish;   // BUILD NOW: it has heard enough
    const narrow = (S.W || 1280) < 1440;

    // AUTO RACK SETUP
    const big = finish ? `${icon("star")}BUILD NOW<small data-coach-heard>${secsText(r.heardSeconds)} heard</small>`
      : r.state === "listening" ? `${icon("pulse")}LISTENING<small data-coach-pct>${r.progress || 0} %</small>`
      : r.state === "working" ? `${icon("pulse")}${mk ? "BUILDING YOUR MASTER" : "BUILDING YOUR RACK"}${String.fromCharCode(8230)}`
      : r.state === "busy" ? `${icon("pulse")}${mk ? "AUTO-MASTER IS RUNNING IN MASTEROLOGY" : mixk ? "MIXOLOGY IS LISTENING" : "AUTO-EDIT IS RUNNING IN VOXOLOGY"}`
      : `${icon("star")}${plan || failed ? "ANALYZE AGAIN" : "ANALYZE &amp; BUILD"}`;
    const pv = r.preview === "plan" || r.preview === "yours";
    const hint = pv ? `PREVIEW: you hear ${r.preview === "plan" ? "the plan" : "your settings"}. Flip PLAN / YOURS while the song plays; KEEP PLAN applies it, BACK TO YOURS puts yours back.`
      : mk
      ? (r.state === "listening" ? (finish ? "It has heard enough: press BUILD NOW, or let the song play to the end (the whole song is best: the Coach builds then)." 
          : r.hearing ? "Hearing your mix: BUILD NOW comes after 12 seconds; the whole song is best." : "Press play: Masterology listens to the song (at least 12 seconds; the whole song is best).")
        : r.state === "working" ? "Working out every module, then playing the loudest part of your mix through the plan to measure it."
        : r.state === "busy" ? "Auto-Master is running in Masterology's own window: press ANALYZE & BUILD when it has finished."
        : plan ? "" : "Masterology listens while the song plays, then builds the plan here. Nothing changes until you apply it.")
      : mixk
      ? (r.state === "none" ? (c.missing ? "The Coach for this track needs Mixology on your master channel (with its DAW bridge), version 0.36 or newer." : "Press ANALYZE & BUILD: Mixology works out a plan for this track. Nothing changes until you apply it.")
        : r.state === "listening" ? (r.hearing ? `Hearing ${t.name}: keep the song playing where it plays.` : `Press play at a part where ${t.name} plays: the Coach listens to ${r.need || 8} seconds of it.`)
        : r.state === "busy" ? "Analyze Mix (or AUTO on a page) is listening: press ANALYZE & BUILD when it has finished."
        : plan ? "" : r.heardEnough ? "Mixology works from what Analyze Mix heard. Nothing changes until you apply it."
        : "Mixology hasn't heard this track play yet: the Coach listens to it first while the song plays. Nothing changes until you apply it.")
      : r.state === "listening" ? (r.hearing ? "Hearing the vocal: keep the song playing." : "Press play: Voxology listens to about 12 seconds of singing.")
      : r.state === "working" ? "Working out every module, then playing your take through the plan to measure it."
      : plan ? "" : "Voxology listens while the song plays, then builds the plan here. Nothing changes until you apply it.";
    const setup = `<section class="sr-panel sr-csetup">
      <h3 class="sr-ph sr-ph-s">${icon("sliders")}AUTO RACK SETUP</h3>
      <div class="sr-csetup-row">${mk ? `
        <div class="sr-cfield"><b>GENRE</b><span class="sr-cval sr-cstep">
          <button data-sr="coach-genre" data-dir="-1" ${can ? "" : "disabled"} title="The genre Auto-Master aims for">${icon("left")}</button>
          <i title="${esc((r.genres || [])[r.genre] || "")}">${esc((r.genres || [])[r.genre] || "")}</i>
          <button data-sr="coach-genre" data-dir="1" ${can ? "" : "disabled"} title="The genre Auto-Master aims for">${icon("right")}</button></span></div>
        <div class="sr-cfield"><b>INTENSITY</b><span class="sr-seg sr-cseg">${ints.map((n, i) =>
          `<button class="${i === r.intensity ? "sr-on" : ""}" data-sr="coach-int" data-i="${i}" ${can ? "" : "disabled"}>${esc(n.toUpperCase())}</button>`).join("")}</span></div>
        <div class="sr-cfield"><b>TARGET</b><span class="sr-cval sr-cstep">
          <button data-sr="coach-target" data-dir="-1" ${can ? "" : "disabled"} title="How loud the master ends up">${icon("left")}</button>
          <i title="${esc((r.targets || [])[r.target] || "")}">${esc((r.targets || [])[r.target] || "")}</i>
          <button data-sr="coach-target" data-dir="1" ${can ? "" : "disabled"} title="How loud the master ends up">${icon("right")}</button></span></div>
        ${narrow ? "" : `<div class="sr-cfield"><b>REFERENCE</b><span class="sr-cval sr-dimv" title="${r.reference ? esc(r.reference) : "Load one in Masterology (Match ref)"}">${r.reference ? icon("wave") + esc(r.reference) : "None (in Masterology)"}</span></div>`}
      </div>` : mixk ? `
        ${narrow ? "" : `<div class="sr-cfield"><b>SOURCE</b><span class="sr-cval">${icon("sliders")}${esc(t.name)}</span></div>`}
        <div class="sr-cfield"><b>ROLE</b><span class="sr-cval ${r.role ? "" : "sr-dimv"}" title="Set a track's role on Mixology's Tracks page">${esc(r.role || "Not heard yet")}</span></div>
        <div class="sr-cfield"><b>STYLE</b><span class="sr-cval" title="The mix style: choose it in Mixology (or Masterology)">${esc(r.style || "")}</span></div>
        <div class="sr-cfield"><b>HEARD</b><span class="sr-cval ${r.heardEnough ? "" : "sr-dimv"}">${r.heardEnough ? `${icon("check")}${r.heardSeconds != null ? r.heardSeconds + " s" : "Yes"}` : "Not yet: it listens first"}</span></div>
      </div>` : `
        ${narrow ? "" : `<div class="sr-cfield"><b>SOURCE</b><span class="sr-cval">${icon("mic")}${esc(t.name)} ${MINUS} vocal</span></div>`}
        <div class="sr-cfield"><b>STYLE</b><span class="sr-cval sr-cstep">
          <button data-sr="coach-style" data-dir="-1" ${can ? "" : "disabled"} title="The style Auto-Edit aims for">${icon("left")}</button>
          <i>${esc(styles[r.style] || "")}</i>
          <button data-sr="coach-style" data-dir="1" ${can ? "" : "disabled"} title="The style Auto-Edit aims for">${icon("right")}</button></span></div>
        <div class="sr-cfield"><b>INTENSITY</b><span class="sr-seg sr-cseg">${ints.map((n, i) =>
          `<button class="${i === r.intensity ? "sr-on" : ""}" data-sr="coach-int" data-i="${i}" ${can ? "" : "disabled"}>${esc(n.toUpperCase())}</button>`).join("")}</span></div>
        <div class="sr-cfield"><b>REFERENCE</b><span class="sr-cval sr-dimv" title="${r.reference ? esc(r.reference) : "Choose one in Voxology (REFERENCE)"}">${r.reference ? icon("wave") + esc(r.reference) : narrow ? "None" : "None (in Voxology)"}</span></div>
      </div>`}
      <div class="sr-cgo">
        <i class="sr-chev"></i>
        ${finish ? `<button class="sr-cbig" data-sr="coach-finish" ${canStep(c) ? "" : "disabled"} title="Build the plan from what it has heard">${big}</button>`
          : `<button class="sr-cbig ${busy ? "sr-busy" : ""}" data-sr="coach-plan" ${can ? "" : "disabled"}>${big}</button>`}
        <i class="sr-chev sr-chev-r"></i>
      </div>
      ${r.state === "listening" ? `<div class="sr-cprog"><i data-coach-bar style="width:${clamp(r.progress || 0, 0, 100)}%"></i></div>` : ""}
      ${hint || r.state === "listening" ? `<p class="sr-chint">${esc(hint)}${r.state === "listening" ? ` <button class="sr-cstop" data-sr="coach-stop" ${canStep(c) ? "" : "disabled"} title="Stop listening: nothing changes">STOP</button>` : ""}</p>` : ""}
    </section>`;

    // ANALYSIS RESULTS: the take against the aim, what stands out
    const issues = plan ? plan.issues || [] : [];
    const warns = issues.filter((i) => i.warn).length;
    const analysis = `<section class="sr-panel sr-cana">
      <h3 class="sr-ph sr-ph-s">${icon("pulse")}ANALYSIS RESULTS${plan ? `<span class="sr-cbadges"><em class="sr-ok">${icon("check")}Done</em><em class="${warns ? "sr-bad" : ""}">${issues.length} stand${issues.length === 1 ? "s" : ""} out</em>${
        plan.upToDate === true ? `<em class="sr-ok">Already set</em>` : plan.upToDate === false ? `<em>${plan.changes} to change</em>` : `<em>${plan.modulesOn} modules</em>`}</span>` : ""}</h3>
      ${plan ? `<div class="sr-cana-body">
          <div class="sr-cana-graph">${toneGraph(plan.bands || {}, [{ values: (plan.bands || {}).before, cls: "sr-cg-take", fill: true }].concat((plan.bands || {}).target ? [{ values: plan.bands.target, cls: "sr-cg-aim" }] : []), issues)}
            <div class="sr-cg-key"><span class="sr-cg-k-take">${mk ? "your mix" : mixk ? "your track (against its mids)" : plan.beforeNow ? "your take, with your settings now" : "your take"}</span>${(plan.bands || {}).target ? `<span class="sr-cg-k-aim">the aim: ${esc(plan.aim || "")}</span>` : ""}</div></div>
          <div class="sr-cissues"><h4>WHAT STANDS OUT</h4><ul>${issues.length ? issues.map((i) =>
            `<li class="${i.warn ? "sr-warn" : "sr-info"}" title="${esc(i.text)}">${icon(i.warn ? "warn" : "info", i.warn ? "sr-li-warn" : "sr-li-info")}<span>${esc(i.name)}${i.from > 0 ? ` <small>${esc(hzRange(i.from, i.to))}</small>` : ""}</span></li>`).join("")
            : `<li>${icon("check", "sr-li-ok")}<span>${mixk ? "Nothing stands out: it already fits." : mk ? "Nothing stands out: your mix is already close to the aim." : "Nothing stands out: your take is already close to the aim."}</span></li>`}</ul></div>
        </div>`
        : failed ? `<div class="sr-cempty sr-cfail"><b>${esc(failed.tipTitle || engine + " couldn't build a plan")}</b><p>${esc(failed.summary || "")}</p><p>${esc(failed.tip || "")}</p></div>`
        : `<div class="sr-cempty">${busy ? "Listening and building: the results land here." : mk ? "Press ANALYZE &amp; BUILD and play the song: what stands out in your mix shows here, measured against your genre's masters (or your reference)." : mixk ? "Press ANALYZE &amp; BUILD: what stands out in this track shows here, with what Mixology would do (the same rules as its AUTO buttons, every page at once)." : "Press ANALYZE &amp; BUILD and play the song: what stands out in your take shows here, measured against what Auto-Edit aims for."}</div>`}
    </section>`;

    // RECOMMENDED SIGNAL CHAIN
    const chainOn = plan ? (plan.chain || []).filter((m) => m.on) : [];
    const recommended = `<section class="sr-panel sr-flow sr-crec" title="${plan ? `Recommended for ${esc(t.name)}: ${esc(mk ? plan.genre : plan.style)}, ${esc(plan.intensity)}${mk ? ", " + esc(plan.target) : ""}` : ""}">
      <div class="sr-flow-t">RECOMMENDED<br>CHAIN</div>
      <div class="sr-crec-row" data-scroll="crec">${chainOn.length ? chainOn.map((m, i) => {
        const k = cat(m.key);
        return `${i ? '<i class="sr-arrow"></i>' : ""}<div class="sr-chex ${m.same ? "sr-same" : ""}"><button class="sr-hex ${i === chainOn.length - 1 ? "sr-picked" : ""}" data-sr="coach-why" data-key="${esc(m.key)}" title="${esc(k.name)}${m.same ? ": already set as planned" : ""}">${icon(k.icon)}<span>${esc(k.hex)}</span></button><small>${m.same ? "Already set" : esc(m.caption)}</small></div>`;
      }).join("") : `<div class="sr-flow-empty">${plan && mixk ? "No module needed: it already fits." : `The modules ${engine} would use show here.`}</div>`}</div>
    </section>`;

    // BEFORE vs PROPOSED, RESULTS (measured)
    const results = plan ? plan.results || [] : [];
    const scale = Math.max(3, ...results.filter((x) => !mk || x.unit === "dB").map((x) => Math.max(Math.abs(x.before), Math.abs(x.after))));
    const lower = `<div class="sr-clower">
      <section class="sr-panel sr-cba"><h3 class="sr-ph sr-ph-s"><span class="sr-ab-chip">A/B</span>BEFORE vs PROPOSED</h3>
        ${plan && (plan.bands || {}).after ? `${toneGraph(plan.bands, [{ values: plan.bands.before, cls: "sr-cg-before", fill: true }, { values: plan.bands.after, cls: "sr-cg-after" }], [])}
          <div class="sr-cg-key"><span class="sr-cg-k-before">${mk ? "your mix" : mixk ? "now" : plan.beforeNow ? "now (your settings)" : "before (your take)"}</span><span class="sr-cg-k-after">${mk ? "the master (measured)" : mixk ? "with the plan's EQ and Tone" : "through the plan"}</span></div>`
          : `<div class="sr-cempty">${mk ? "Your mix and the master, measured through the plan." : mixk ? "The track's tone now and with the plan." : "Your take before and through the plan."}</div>`}
      </section>
      <section class="sr-panel sr-cres"><h3 class="sr-ph sr-ph-s">${icon("maxbars")}RESULTS<small>${mk ? "measured on your mix" : mixk ? "the plan on your track" : "measured on your take"}</small></h3>
        ${results.length && mk ? `<ul>${results.map((x) => {
          // the master's rows have units of their own: each bar on its own scale (louder is longer)
          const w = (v) => (x.unit === "LUFS" || x.unit === "dBTP" ? clamp((v + 40) / 40, 0, 1) : x.unit === "%" ? clamp(v / 100, 0, 1) : Math.abs(v) / scale) * 100;
          const f = (v) => (x.unit === "%" ? String(Math.round(v)) : /aim/.test(x.name) ? dbSigned(v) : (v < 0 ? MINUS : "") + Math.abs(v).toFixed(1));
          return `<li class="${x.better ? "sr-better" : ""}" title="${esc(x.name)}: ${f(x.before)} ${String.fromCharCode(8594)} ${f(x.after)} ${esc(x.unit)}">
            <b>${esc(x.name)}</b><span class="sr-cbar"><i class="sr-cbar-b" style="width:${w(x.before).toFixed(0)}%"></i><i class="sr-cbar-a" style="width:${w(x.after).toFixed(0)}%"></i></span>
            <em>${f(x.before)} ${String.fromCharCode(8594)} ${f(x.after)}<small> ${esc(x.unit)}</small></em></li>`;
        }).join("")}</ul>`
        : results.length ? `<ul>${results.map((x) => {
          const wb = (Math.abs(x.before) / scale) * 100, wa = (Math.abs(x.after) / scale) * 100;
          const unit = x.unit === "dB apart" ? " dB apart" : " dB";
          return `<li class="${x.better ? "sr-better" : ""}" title="${esc(x.name)}: ${dbSigned(x.before)} ${String.fromCharCode(8594)} ${dbSigned(x.after)}${unit} (${esc(x.unit)})">
            <b>${esc(x.name)}</b><span class="sr-cbar"><i class="sr-cbar-b" style="width:${wb.toFixed(0)}%"></i><i class="sr-cbar-a" style="width:${wa.toFixed(0)}%"></i></span>
            <em>${x.unit === "dB apart" ? x.before.toFixed(1) : dbSigned(x.before)} ${String.fromCharCode(8594)} ${x.unit === "dB apart" ? x.after.toFixed(1) : dbSigned(x.after)}</em></li>`;
        }).join("")}${(plan && plan.moves || []).map((mv) => `<li class="sr-cmove"><b>${esc(mv.module)}</b><em>${esc(mv.from || "")} ${String.fromCharCode(8594)} ${esc(mv.to || "")}</em></li>`).join("")}</ul>`
          : plan && (plan.moves || []).length ? `<ul>${plan.moves.map((mv) => `<li class="sr-cmove"><b>${esc(mv.module)}</b><em>${esc(mv.from || "")} ${String.fromCharCode(8594)} ${esc(mv.to || "")}</em></li>`).join("")}</ul>`
          : `<div class="sr-cempty">Before ${String.fromCharCode(8594)} after, ${mixk ? "from the plan's own curves" : "measured"}: no guesses.</div>`}
      </section>
    </div>`;

    // right: WHAT I HEARD, WHY I BUILT THIS RACK, ADJUST FIRST
    const why = plan ? (plan.chain || []).filter((m) => m.on && (m.set || []).length).map((m) => {
      const s0 = m.set[0];
      const what = (x) => (x.from ? x.from + " " + String.fromCharCode(8594) + " " : "") + [x.control, x.value].filter(Boolean).join(" ");
      return `<li data-why="${esc(m.key)}" class="${S.coachWhy === m.key ? "sr-picked" : ""}">${icon("check", "sr-li-ok")}<span><b>${esc(cat(m.key).name)}</b>${m.same ? ` <em class="sr-csame">already set</em>` : ""} ${MINUS} ${esc(what(s0))}${s0.why ? ": " + esc(s0.why) : ""}${
        m.set.length > 1 ? `<small>${m.set.slice(1).map((x) => esc(what(x))).join(" · ")}</small>` : ""}</span></li>`;
    }).join("") : "";
    const right = `<section class="sr-panel sr-right">
      <h3 class="sr-ph">${icon("coach")}RACK COACH</h3>
      <div class="sr-coach" data-scroll="ccoach">
        <div class="sr-banner">${icon("pulse")}WHAT I HEARD</div>
        ${plan ? `<p class="sr-cheard">${esc(plan.summary || "")}</p><ul class="sr-cheard-l">${(plan.heard || []).map((h) => `<li>${esc(h)}</li>`).join("")}</ul>`
          : `<p class="sr-cheard sr-dimv">${busy ? (mk ? "Listening to your mix" : mixk ? `Listening to ${esc(t.name)}` : "Listening to your vocal") + String.fromCharCode(8230) : "Nothing yet: press ANALYZE &amp; BUILD."}</p>`}
        ${why ? `<h4 class="sr-cban">${icon("layers")}WHY I BUILT THIS RACK</h4><ul class="sr-cwhy">${why}</ul>` : ""}
        ${plan && (plan.first || []).length ? `<h4 class="sr-cban">${icon("bulb")}${mixk || mk ? "GOOD TO KNOW" : "ADJUST FIRST"}</h4><ol class="sr-cfirst">${plan.first.map((f) => `<li>${esc(f)}</li>`).join("")}</ol>` : ""}
      </div>
    </section>`;

    return `<main class="sr-main sr-main-coach ${narrow ? "sr-cnarrow" : ""}">${left}
      <div class="sr-centre sr-ccentre">${setup}${analysis}${recommended}${lower}</div>
      ${right}</main>`;
  }

  // The COACH page's bottom bar (in place of the meters): APPLY TO TRACK, UNDO, SAVE CHAIN, TWEAK MANUALLY.
  function coachBar(data) {
    const t = trackOf(data, S.track);
    const c = coachOf(t);
    const r = c ? c.r : {};
    const ready = r.state === "ready", applied = r.state === "applied";
    const upToDate = ready && r.plan && r.plan.ok && r.plan.upToDate === true;   // the settings now are the plan already
    const ok = c && c.unit && canTurn(c.unit) && r.state !== "none";
    const mk = !!c && c.kind === "master", where = mk ? "MASTER" : "TRACK";
    // Beta 32, PREVIEW: hear the plan before it is applied, flip with yours at the same loudness, keep it or go back.
    if (ok && (r.preview === "plan" || r.preview === "yours")) {
      const onPlan = r.preview === "plan";
      return `<footer class="sr-foot sr-cfoot sr-cprev ${(S.W || 1280) < 1440 ? "sr-cfoot-n" : ""}">
      <button class="sr-capply" data-sr="coach-keep" title="Apply the plan (one UNDO step)">${icon("check")}KEEP PLAN</button>
      <span class="sr-csep"></span>
      <span class="sr-seg sr-cflip" title="Flip while the song plays: the louder one is turned down to match">
        <button class="${onPlan ? "sr-on" : ""}" data-sr="coach-flip" data-to="plan">${icon("star")}PLAN</button>
        <button class="${onPlan ? "" : "sr-on"}" data-sr="coach-flip" data-to="yours">${icon("sliders")}YOURS</button></span>
      <span class="sr-csep"></span>
      <button class="sr-cact" data-sr="coach-back" title="Stop the preview: your settings play again, nothing changes">${icon("undo")}BACK TO YOURS</button>
      <span class="sr-cmatch">${esc(matchText(r, c.kind))}</span>
    </footer>`;
    }
    const canPreview = ok && ready && !upToDate && r.plan && r.plan.ok && r.plan.changes !== 0;
    return `<footer class="sr-foot sr-cfoot ${(S.W || 1280) < 1440 ? "sr-cfoot-n" : ""}">
      <button class="sr-capply ${applied || upToDate ? "sr-done" : ""}" data-sr="coach-apply" ${ok && ready && !upToDate ? "" : "disabled"} title="${upToDate ? "Your settings already match the plan" : "Set every module as the plan says (one UNDO step)"}">${icon(applied || upToDate ? "check" : "layers")}${applied ? "APPLIED TO " + where : upToDate ? "NOTHING TO CHANGE" : "APPLY TO " + where}</button>
      ${canPreview ? `<button class="sr-cact sr-cpv" data-sr="coach-preview" title="Hear the plan before you apply it: flip with yours at the same loudness">${icon("speaker")}PREVIEW</button>` : ""}
      <span class="sr-csep"></span>
      <button class="sr-cact" data-sr="coach-undo" ${ok && r.canUndo ? "" : "disabled"} title="${c && c.kind === "mix" ? "Put back the settings from before the plan (one step in Mixology's History)" : mk ? "Put back the settings from before the plan (one step in Masterology's UNDO)" : "Put back the settings from before the plan (or Voxology's last Auto-Edit)"}">${icon("undo")}UNDO</button>${c && c.kind !== "vox" ? "" : `
      <span class="sr-csep"></span>
      <button class="sr-cact" data-sr="savechain" data-unit="${c ? esc(c.unit.id) : ""}" ${ok ? "" : "disabled"} title="Keep this setup in Voxology's MY CHAINS">${icon("save")}SAVE CHAIN</button>`}
      <span class="sr-csep"></span>
      <button class="sr-cact" data-sr="page" data-page="rack" title="Open the RACK page to change any knob">${icon("sliders")}TWEAK MANUALLY</button>
    </footer>`;
  }


  // ---- Beta 35: GUIDED MIX ------------------------------------------------------------------------------------------
  // For a first mix: one step at a time through the whole song. Check the setup, let Mixology hear the mix, see what to
  // fix first, the Coach on each track (vocals first; PREVIEW, then KEEP or BACK), the master, done. It only starts when
  // you press GUIDED MIX (SESSION page); while it runs its steps take the left column (SESSION: beside WHAT TO FIX FIRST;
  // COACH: in place of the signal chain). Your progress is kept for this project while the Rack stays in this plug-in.
  const GUIDE_KEY = "sonicologyRackGuide";
  const guideProject = (data) => tracks(data).map((t) => t.key).sort().join("|");
  function guideSave() {
    try { if (S.guide) localStorage.setItem(GUIDE_KEY, JSON.stringify(S.guide)); else localStorage.removeItem(GUIDE_KEY); } catch (e) { /* none */ }
  }
  function guideSaved(data) {
    try {
      const g = JSON.parse(localStorage.getItem(GUIDE_KEY) || "null");
      return g && g.project === guideProject(data) ? g : null;
    } catch (e) { return null; }
  }
  // The tracks the Coach works on, in a mixing order: the lead vocal, the other vocals, bass, drums, the beat, the rest.
  function guideTracks(data) {
    const rank = (t) => {
      const n = (t.name || "").toLowerCase();
      const vocal = t.units.some((u) => u.product === "voxology" && u.role !== "beat") || /vox|vocal|voice|adlib|ad-lib|harmon|backing|double/.test(n);
      if (vocal) return /lead|main/.test(n) ? 0 : 1;
      if (/bass|808|sub/.test(n)) return 2;
      if (/kick/.test(n)) return 3;
      if (/snare|clap|rim/.test(n)) return 3.1;
      if (/hat|perc|drum|tom|cymbal|shaker/.test(n)) return 3.2;
      if (/beat|instrumental|loop/.test(n)) return 4;
      return 5;
    };
    return tracks(data).filter((t) => t.role !== "fullmix" && t.role !== "master" && coachOf(t))
      .map((t, i) => ({ t, i, r: rank(t) })).sort((a, b) => a.r - b.r || a.i - b.i).map((x) => x.t);
  }
  function guideSteps(data) {
    const steps = [
      { id: "setup", title: "Check the setup" },
      { id: "listen", title: "Let Mixology hear the mix" },
      { id: "first", title: "What to fix first" },
    ];
    for (const t of guideTracks(data)) steps.push({ id: "track:" + t.key, title: "Coach: " + t.name, track: t });
    const master = tracks(data).find((t) => t.role === "master");
    steps.push({ id: "master", title: "The master", track: master || null });
    steps.push({ id: "done", title: "Finished" });
    return steps;
  }
  const guideOn = () => !!(S.guide && S.guide.on);
  function guideStart(data, again) {
    const saved = again ? null : guideSaved(data);
    S.guide = saved ? Object.assign(saved, { on: true }) : { on: true, project: guideProject(data), at: "setup", done: {} };
    guideSave();
    guideGo(S.guide.at, data);
  }
  function guideGo(id, data) {
    const g = S.guide;
    if (!g) return;
    const steps = guideSteps(data || S.data);
    const st = steps.find((x) => x.id === id) || steps[0];
    g.at = st.id;
    if (st.track) { S.track = st.track.key; S.sel = null; S.page = "coach"; }
    else if (st.id === "master") S.page = "session";   // (no Master track in this project: the step says so)
    else S.page = "session";
    guideSave();
  }
  function guideMove(dir, data) {
    const steps = guideSteps(data || S.data);
    const i = Math.max(0, steps.findIndex((x) => x.id === S.guide.at));
    const j = clamp(i + dir, 0, steps.length - 1);
    guideGo(steps[j].id, data);
  }
  // What a track's (or the master's) Coach says now: "kept", "same" (already set), "busy", "ready", "preview", "idle".
  function guideCoachState(t) {
    const c = t && coachOf(t);
    if (!c || !c.r) return "idle";
    const r = c.r, p = r.plan || {};
    if (r.preview === "plan" || r.preview === "yours") return "preview";
    if (r.state === "applied") return "kept";
    if (r.state === "ready" && p.ok && p.upToDate === true) return "same";
    if (r.state === "ready" && p.ok) return "ready";
    if (COACH_BUSY[r.state]) return "busy";
    return "idle";
  }
  // While the guide shows a track, what its Coach did is noted (KEEP / APPLY: kept; NOTHING TO CHANGE: already set).
  function guideObserve(data) {
    const g = S.guide;
    if (!guideOn() || !data) return;
    const st = guideSteps(data).find((x) => x.id === g.at);
    if (!st || !st.track || S.track !== st.track.key) return;
    const now = guideCoachState(st.track);
    if ((now === "kept" || now === "same") && g.done[st.id] !== now) { g.done[st.id] = now; guideSave(); }
  }
  function guideStatus(st, data) {
    const g = S.guide, d = g.done[st.id];
    if (st.id === "setup") {
      const n = fixItems(data).filter((x) => x.level === "warn" && x.from !== "mix").length;
      return n ? { cls: "warn", text: n + " to fix" } : { cls: "ok", text: "looks right" };
    }
    if (st.id === "listen") {
      const { mix, r } = mixFixReport(data);
      if (!mix) return { cls: "warn", text: "needs Mixology" };
      if (r && r.busy) return { cls: "busy", text: "listening" };
      return r && r.analyzed ? { cls: "ok", text: "heard " + secsText(r.seconds) } : { cls: "todo", text: "not yet" };
    }
    if (st.id === "first") {
      const n = fixItems(data).filter((x) => x.level === "warn" && x.from === "mix").length;
      const { r } = mixFixReport(data);
      return { cls: g.done.first ? "ok" : "todo", text: r && r.analyzed ? (n ? n + " points" : "nothing big") : "" };
    }
    if (st.id === "done") return { cls: "todo", text: "" };
    if (st.id === "master" && !st.track) return { cls: "warn", text: "no Masterology" };
    if (d === "kept") return { cls: "ok", text: "kept" };
    if (d === "same") return { cls: "ok", text: "already set" };
    if (d === "skipped") return { cls: "skip", text: "skipped" };
    return { cls: "todo", text: "" };
  }
  // The current step: what to do, its buttons.
  function guideNow(st, data, narrow) {
    const g = S.guide;
    const steps = guideSteps(data);
    const i = steps.findIndex((x) => x.id === st.id);
    const next = steps[i + 1];
    const nextLabel = !next ? "" : next.track && st.track && !narrow ? "NEXT TRACK" : "NEXT";
    let say = "", acts = "";
    if (st.id === "setup") {
      const n = fixItems(data).filter((x) => x.level === "warn" && x.from !== "mix").length;
      say = n ? `Fix the red points in WHAT TO FIX FIRST${narrow ? "" : " (on the right)"}: click a one-click fix, or do what it says in your DAW. These are things no mixing move fixes (a plug-in missing or in the wrong place, two reverbs on one vocal...).`
        : "Every Sonicology plug-in is where it should be. Next, Mixology listens to your whole mix.";
    } else if (st.id === "listen") {
      const { mix, r } = mixFixReport(data);
      if (!mix) say = "Put Mixology on your master channel (the Stereo Out, before Masterology): the guide needs it to hear the whole mix. You can skip this step, but the Coach works best after Mixology has listened.";
      else if (r && r.busy) {
        const enough = r.enough || 20, heard = Math.min(r.hearing || 0, 60);
        say = r.playing === false ? "Press play: Mixology listens while the song plays (the busiest part, a chorus, is best)." : `Listening to your mix: keep the song playing. ${heard >= enough ? "It has heard enough: FINISH, or let it hear up to a minute." : `It needs about ${enough} seconds.`}`;
        acts = `<div class="sr-cprog sr-gprog"><i data-guide-bar style="width:${clamp((heard / enough) * 100, 0, 100).toFixed(0)}%"></i></div>`
          + (heard >= enough ? `<button class="sr-gbtn sr-gbtn-go" data-sr="guide-finish">${icon("check")}FINISH</button>` : "")
          + `<button class="sr-gbtn" data-sr="guide-cancel">STOP</button>`;
      } else if (r && r.analyzed) {
        say = `Mixology heard ${secsText(r.seconds)} of your mix${r.style ? " (" + r.style + ")" : ""}. Changed a lot since? Listen again; else NEXT.`;
        acts = `<button class="sr-gbtn" data-sr="guide-analyze">${icon("pulse")}LISTEN AGAIN</button>`;
      } else {
        say = "Press ANALYZE MIX, then play the busiest part of your song (a chorus) for about 20 seconds. Mixology hears every track at once and finds what to fix first. Nothing changes yet.";
        acts = `<button class="sr-gbtn sr-gbtn-go" data-sr="guide-analyze">${icon("pulse")}ANALYZE MIX</button>`;
      }
    } else if (st.id === "first") {
      const n = fixItems(data).filter((x) => x.level === "warn" && x.from === "mix").length;
      say = n ? `WHAT TO FIX FIRST${narrow ? "" : " (on the right)"} lists your mix's biggest problems, worst first. You don't need to fix them by hand: the next steps run the Coach on each track, and it fixes most of them.`
        : "Nothing big stands out in your mix. The next steps still let the Coach shape each track.";
    } else if (st.id === "master") {
      say = !st.track ? "No Masterology on your master channel: put Masterology last on the Stereo Out (after Mixology), or skip this step."
        : guideCoachText(st.track, true);
    } else if (st.id === "done") {
      const kept = Object.values(g.done).filter((x) => x === "kept").length;
      const same = Object.values(g.done).filter((x) => x === "same").length;
      const skipped = Object.values(g.done).filter((x) => x === "skipped").length;
      say = `Your first mix is through. The Coach set ${kept} track${kept === 1 ? "" : "s"}${same ? `, ${same} already fit` : ""}${skipped ? `, you skipped ${skipped}` : ""}. Play the whole song: something off? Open that track's COACH and press UNDO. Then save your project.`;
      acts = `<button class="sr-gbtn" data-sr="guide-restart">${icon("undo")}START OVER</button><button class="sr-gbtn sr-gbtn-go" data-sr="guide-exit">${icon("check")}CLOSE THE GUIDE</button>`;
    } else {
      say = guideCoachText(st.track, false);
    }
    const nav = st.id === "done" ? "" : `<div class="sr-gnav">
        <button class="sr-gbtn" data-sr="guide-back" ${i <= 0 ? "disabled" : ""} title="The step before">${icon("left")}</button>
        ${st.track || st.id === "master" ? `<button class="sr-gbtn" data-sr="guide-skip" title="Leave this one as it is">SKIP</button>` : ""}
        <button class="sr-gbtn sr-gbtn-next" data-sr="guide-next">${nextLabel}${icon("right")}</button>
      </div>`;
    return `<div class="sr-gnow"><p>${esc(say)}</p>${acts ? `<div class="sr-gacts">${acts}</div>` : ""}${nav}</div>`;
  }
  function guideCoachText(t, master) {
    const where = master ? "the master" : t.name;
    switch (guideCoachState(t)) {
      case "busy": return master ? "Masterology is listening: keep the song playing. BUILD NOW comes after 12 seconds (the whole song is best)." : `Listening to ${where}: keep the song playing where it plays.`;
      case "ready": return "The plan is ready. Press PREVIEW (bottom bar) and flip PLAN / YOURS while the song plays: both play at the same loudness. Sounds better? KEEP PLAN. Not? BACK TO YOURS.";
      case "preview": return "Flip PLAN / YOURS while it plays. KEEP PLAN if it sounds better, BACK TO YOURS if not. Then NEXT.";
      case "kept": return `Done: the plan is on ${where} (UNDO on the Coach page takes it back). NEXT.`;
      case "same": return `${master ? "Your master" : where} already fits the plan: nothing to change. NEXT.`;
      default: return master ? "Pick the GENRE and how loud (TARGET), press ANALYZE & BUILD and play the song. Masterology plans the whole master; nothing changes until you keep it."
        : `Press ANALYZE & BUILD and play a part where ${where} plays. The Coach plans the whole track; nothing changes until you keep it.`;
    }
  }
  function guidePanel(data, narrow) {
    const g = S.guide;
    const steps = guideSteps(data);
    const at = steps.find((x) => x.id === g.at) || steps[0];
    const i = steps.indexOf(at);
    const list = steps.map((st, n) => {
      const s2 = guideStatus(st, data);
      const now = st === at;
      const ok = s2.cls === "ok";
      return `<li class="sr-gstep ${now ? "sr-gstep-now" : ""} sr-gs-${s2.cls}">
        <button data-sr="guide-go" data-step="${esc(st.id)}" title="${esc(st.title)}"><span class="sr-gn">${ok ? icon("check") : n + 1}</span><b>${esc(st.title)}</b>${s2.text ? `<small>${esc(s2.text)}</small>` : ""}</button>
        ${now ? guideNow(st, data, narrow) : ""}</li>`;
    }).join("");
    return `<section class="sr-panel ${narrow ? "sr-left" : "sr-wide sr-session"} sr-guide">
      <h3 class="sr-ph">${icon("star")}GUIDED MIX<small>${narrow ? `${i + 1} / ${steps.length}` : `step ${i + 1} of ${steps.length}`}</small><button class="sr-gx" data-sr="guide-exit" title="Close the guide (your progress is kept: GUIDED MIX goes on from here)">${icon("close")}</button></h3>
      <div class="sr-gbody" data-scroll="guide"><ol class="sr-gsteps">${list}</ol></div>
    </section>`;
  }

  // How PREVIEW matches the two: the louder one plays turned down (the plug-in reports the plan minus yours).
  function matchText(r, kind) {
    const gap = +r.planGapDb;
    const est = kind === "mix" ? " (estimated)" : "";
    if (!Number.isFinite(gap)) return "Measuring the loudness" + String.fromCharCode(8230);
    if (Math.abs(gap) < 0.3) return "Same loudness already: no level change";
    const db = Math.abs(gap).toFixed(1) + " dB";
    return gap > 0 ? `Same loudness: the plan plays ${db} down${est}` : `Same loudness: yours plays ${db} down${est}`;
  }

  // ---- WHAT TO FIX FIRST (Beta 31: the SESSION page's right panel) ---------------------------------------------------
  // One list for the whole project, worst first: Mixology's setup check (things no mixing move fixes), what the plug-ins
  // do together that works against you (two reverbs on a vocal...), Mixology's Mix Assistant points (from Analyze Mix:
  // the mix's own problems, with their tracks), a Link in the wrong insert slot; then the "have a look" points. Mixology
  // sends its part as its report while a Rack shows the Full Mix (the SESSION page asks for it).
  function mixFixReport(data) {
    const mix = mixOwner(data);
    if (!mix) return { mix: null, r: null };
    if (mix.report && S.coachText[mix.id] !== mix.report) {
      S.coachText[mix.id] = mix.report;
      try { S.coachObj[mix.id] = JSON.parse(mix.report); } catch (e) { S.coachObj[mix.id] = null; }
    }
    const r = mix.report ? S.coachObj[mix.id] : null;
    if (r && r.kind === "fix") S.fixLast = r;     // (kept while the Rack shows another track for a moment)
    return { mix, r: r && r.kind === "fix" ? r : S.fixLast || null };
  }
  function fixItems(data) {
    const list = tracks(data);
    const byName = (name) => list.find((t) => t.name.toLowerCase() === String(name || "").toLowerCase() && t.role !== "fullmix" && t.role !== "master");
    const byLine = (l) => list.find((t) => (l.hash === "master" && t.role === "master") || (l.hash === "fullmix" && t.role === "fullmix") || (l.hash && t.hash === l.hash));
    const out = [];
    const { mix, r } = mixFixReport(data);
    const rank = { setup: 0, together: 1, mix: 2, order: 3 };
    for (const x of (r && r.items) || [])
      out.push({ id: x.id, level: x.done ? "ok" : x.kind === "warn" ? "warn" : x.kind === "calm" ? "ok" : "info", from: x.from === "setup" ? "setup" : "mix", title: x.title,
        sub: x.done ? "Done since Mixology listened: play the song to check it." : x.need, tip: x.text, done: !!x.done, tool: x.tool,
        tracks: (x.tracks || []).map(byName).filter(Boolean) });
    for (const l of data.together || [])
      if (l.level === "warn") { const t = byLine(l); out.push({ level: "warn", from: "together", title: l.text, tracks: t ? [t] : [], fixes: l }); }
    for (const t of list) {
      for (const u of t.units) for (const l of u.together || []) if (l.level === "warn") out.push({ level: "warn", from: "together", title: l.text, tracks: [t] });
      const o = orderLine(t);
      if (o) out.push({ level: "warn", from: "order", title: "Mixology Link sits before Voxology on " + t.name, sub: "Move Mixology Link to the last insert slot, so Mixology mixes the finished vocal.", tracks: [t] });
    }
    if ((data.otherLayouts || []).length)
      out.push({ level: "warn", from: "setup", title: "A Sonicology plug-in from another beta is in this project", sub: "Install this beta, so all three plug-ins are the same version: the Rack can't see the other one.", tracks: [] });
    // What Mixology can add when it can't speak yet
    if (!mix) out.push({ level: "info", from: "hint", title: "Put Mixology on your master channel", sub: "Its Mix Assistant listens to the whole mix and says what to fix first (Analyze Mix).", tracks: [] });
    else if (r && !r.analyzed) out.push({ level: "info", from: "hint", title: r.busy ? "Analyze Mix is listening" : "Analyze Mix hasn't listened yet",
      sub: r.busy ? "Keep the song playing: Mixology's advice for the whole mix lands here." : "Press ANALYZE MIX in Mixology (Overview) and play the busiest part of the song: its advice for the whole mix lands here.", tracks: [] });
    const lv = { warn: 0, info: 1, ok: 2 };
    const sorted = out.map((x, i) => ({ x, i })).sort((a, b) => (lv[a.x.level] - lv[b.x.level]) || (a.x.done - b.x.done) || ((rank[a.x.from] ?? 4) - (rank[b.x.from] ?? 4)) || a.i - b.i).map((o) => o.x);
    // Said once: Mixology's setup check and the Rack both notice a vocal without a Link, or two tracks with one name.
    const topic = (x) => (x.id === "setup-vocal-link" || /no Mixology Link/i.test(x.title) ? "nolink"
      : x.id === "setup-shared-name" || /tracks are named/i.test(x.title) ? "samename" : "t:" + x.title);
    const said = [];
    return sorted.filter((x) => {
      const k = topic(x);
      const dup = said.some((y) => y.k === k && (!y.tracks.length || !x.tracks.length || x.tracks.every((t) => y.tracks.includes(t.key))));
      said.push({ k, tracks: x.tracks.map((t) => t.key) });
      return !dup;
    });
  }
  const TOOL_NAME = { balance: "Balance", pocket: "Vocal Pocket", clean: "Clean Up", polish: "Polish" };
  function fixPanel(data) {
    const items = fixItems(data);
    const { r } = mixFixReport(data);
    const warn = items.filter((x) => x.level === "warn").length;
    const from = r && r.analyzed ? `${r.fromProject ? "Mixology's advice from the last session" : `From Analyze Mix (${Math.round(r.seconds || 0)} s heard${r.style ? ", " + r.style : ""})`} and the Rack` : "From the Rack";
    const li = items.map((x, n) => {
      const chips = x.tracks.map((t) => `<button class="sr-fx-track" data-sr="track" data-track="${esc(t.key)}" title="Open ${esc(t.name)} on the RACK page">${esc(t.name)}</button>`
        + (coachOf(t) ? `<button class="sr-fx-coach" data-sr="fix-coach" data-track="${esc(t.key)}" title="Open the Coach on ${esc(t.name)}">${icon("coach")}COACH</button>` : "")).join("");
      const tool = x.tool && TOOL_NAME[x.tool] && !x.done ? `<em class="sr-fx-tool">In Mixology: ${esc(TOOL_NAME[x.tool])}</em>` : "";
      return `<li class="sr-fx sr-fx-${x.level}${x.done ? " sr-fx-done" : ""}" ${x.tip ? `title="${esc(x.tip)}"` : ""}>
        <span class="sr-fx-n">${x.done ? icon("check") : n + 1}</span>
        <div class="sr-fx-body"><b>${lineIcon(x.level)}${esc(x.title)}</b>${x.sub ? `<p>${esc(x.sub)}</p>` : ""}
          ${chips || tool || (x.fixes && fixButtons(x.fixes)) ? `<div class="sr-fx-acts">${chips}${tool}${x.fixes ? fixButtons(x.fixes) : ""}</div>` : ""}</div>
      </li>`;
    }).join("");
    return `<section class="sr-panel sr-right sr-fixfirst">
      <h3 class="sr-ph">${icon("coach")}WHAT TO FIX FIRST<small>${warn ? `${warn} worth fixing` : items.length ? "nothing urgent" : ""}</small></h3>
      <div class="sr-coach" data-scroll="fixfirst">
        <div class="sr-banner">${icon("pulse")}WORST FIRST</div>
        <p class="sr-fx-from">${esc(from)}</p>
        ${items.length ? `<ol class="sr-fixlist">${li}</ol>` : `<p class="sr-fx-none">${icon("check", "sr-li-ok")}Nothing to fix: the plug-ins work well together${r && r.analyzed ? " and Mixology's Mix Assistant has no points" : ""}.</p>`}
        ${r && r.more ? `<p class="sr-fx-more">${r.more} more in Mixology's Mix Assistant (Overview).</p>` : ""}
      </div>
    </section>`;
  }

  function sessionPage(data) {
    const list = tracks(data);
    if (!list.length) return `<main class="sr-main sr-empty"><div class="sr-panel sr-wide"><h3>No Sonicology plug-ins are playing</h3><p>Press play in your DAW: plug-ins take part while the song plays.</p></div></main>`;
    const fixes = fixItems(data);
    const warnsOf = (t) => fixes.filter((x) => x.level === "warn" && x.tracks.some((y) => y.key === t.key)).length;
    const rows = list.map((t) => {
      const chips = t.units.map((u) => {
        const P = PRODUCTS[u.product];
        const mods = (u.modules || []).filter((m) => live(m));
        return `<span class="sr-srow-unit"><b class="sr-badge sr-tone-${P.tone}">${P.badge}</b>${u.on === false ? '<em class="sr-bypassed">BYPASSED</em>' : ""}${mods.map((m) =>
          `<button class="sr-spill sr-st-${m.state} sr-tone-${P.tone}" data-sr="goto" data-track="${esc(t.key)}" data-unit="${esc(u.id)}" data-key="${esc(m.key)}" title="${esc(cat(m.key).name)}: ${esc(m.summary || STATE_WORD[m.state])}">${esc(cat(m.key).hex)}</button>`).join("") || '<span class="sr-dim">nothing on</span>'}</span>`;
      }).join("");
      const w = warnsOf(t);
      return `<div class="sr-srow">
        <button class="sr-srow-name" data-sr="track" data-track="${esc(t.key)}" title="Open ${esc(t.name)} on the RACK page"><span>${esc(t.name)}</span><small>${esc(t.role === "fullmix" ? "Full Mix" : t.role === "master" ? "Master" : t.role)}</small></button>
        <div class="sr-srow-chain">${chips}</div>
        <span class="sr-srow-warn ${w ? "sr-on" : ""}" title="${w ? w + " thing" + (w > 1 ? "s" : "") + " worth fixing (WHAT TO FIX FIRST)" : "Nothing worth fixing"}">${icon(w ? "warn" : "check", w ? "sr-li-warn" : "sr-li-ok")}${w || ""}</span>
      </div>`;
    }).join("");
    const saved = !guideOn() && guideSaved(data);
    const start = S.opts && S.opts.request ? `<button class="sr-gstart" data-sr="guide-start" title="A first mix, one step at a time: the setup, Mixology listening, the Coach on every track, the master">${icon("star")}${saved ? "GO ON WITH GUIDED MIX" : "GUIDED MIX"}</button>` : "";
    return `<main class="sr-main sr-main-session ${(S.W || 1280) < 1440 ? "sr-narrow" : ""}">
      ${guideOn() ? guidePanel(data, false) : `<section class="sr-panel sr-wide sr-session">
        <h3 class="sr-ph">${icon("layers")}SESSION OVERVIEW<small>every track and what plays on it: click a module to open it</small>${start}</h3>
        <div class="sr-srows" data-scroll="session">${rows}</div>
      </section>`}
      ${fixPanel(data)}
    </main>`;
  }

  // What the line under the knobs says: how to change them here, or why not now.
  function readonlyLine(unit, m) {
    const name = esc(PRODUCTS[unit.product].name);
    if (unit.lock) return `${icon("lock")}${esc(unit.lock)} The Rack can change ${name} again when it has finished.`;
    const honey = unit.product === "honeytune" ? " · they set Honey Tune for all its clips" : "";
    if (canTurn(unit) && ((m.values || []).some((v) => v.key) || m.switchable))
      return `Drag a knob to change it in ${name} (Shift: fine) · wheel: nudge · double-click: default${m.switchable ? " · click ON / OFF to switch it" : ""}${honey}`;
    return `Shows ${name}'s settings live · change them in ${name}${unit.product === "link" ? " (or with Mixology's tools)" : ""}${honey}`;
  }

  // A warning's one-click fixes (RackLink's together()): each switches one module off, through the plug-in that owns it.
  function fixButtons(l) {
    if (!S.opts || !S.opts.request || !Array.isArray(l.fixes) || !l.fixes.length) return "";
    return `<span class="sr-fixes">${l.fixes.map((f) => `<button class="sr-fix" data-sr="fix" data-unit="${esc(f.unit)}" data-gen="${esc(f.gen)}" data-module="${esc(f.module)}" data-on="${f.on ? 1 : 0}">${esc(f.label)}</button>`).join("")}</span>`;
  }

  // ---- page: MODULES (every module in the suite, where it plays in this project) --------------------------------------
  function usage(data) {
    const on = {}, present = {};
    for (const t of tracks(data)) for (const u of t.units) {
      present[u.product === "link" ? "mixology" : u.product] = true;
      for (const m of u.modules || []) {
        const keys = [m.key];
        if (m.key === "mix.space" && Array.isArray(m.layers)) for (const l of m.layers) if (SPACE_FX[l]) keys.push(SPACE_FX[l]);
        for (const k of keys) {
          if (!live(m)) continue;
          (on[k] = on[k] || []).push({ track: t.key, name: t.name, unit: u.id, mod: m.key });
        }
      }
    }
    return { on, present };
  }

  function cardArt(kind, plug) {
    const col = plug === "voxology" || plug === "honeytune" ? ["#ffd36a", "#ff9d2e"] : plug === "masterology" ? ["#ff4d5e", "#ffb14d"] : ["#4fd2ff", "#7a7bff"];
    const P = {
      wave: "M2 30 L10 30 L14 12 L19 46 L24 18 L28 40 L33 24 L38 34 L44 28 L58 30",
      noise: "M2 30 L6 26 L9 34 L12 22 L15 38 L18 27 L21 33 L24 16 L27 44 L30 25 L33 35 L36 28 L40 31 L44 29 L58 30",
      curve: "M2 36 C10 36 12 22 20 22 S30 38 38 32 S48 18 58 20",
      dip: "M2 22 C14 22 18 22 24 26 S30 46 34 44 S40 24 46 22 L58 22",
      sss: "M2 30 L12 30 L14 22 L16 38 L18 26 L20 34 L22 28 L30 30 L36 30 L38 14 L40 46 L42 20 L44 40 L46 30 L58 30",
      bars: "M8 46 V30 M16 46 V18 M24 46 V26 M32 46 V12 M40 46 V22 M48 46 V32",
      gauge: "M10 44 A22 22 0 0 1 50 44 M30 44 L42 24",
      tube: "M22 10 h16 v30 a8 8 0 0 1 -16 0 Z M26 18 v18 M34 18 v18",
      stack: "M6 22 C16 14 24 30 32 22 S48 14 56 22 M6 32 C16 24 24 40 32 32 S48 24 56 32 M6 42 C16 34 24 50 32 42 S48 34 56 42",
      swirl: "M8 30 C8 14 30 14 30 30 S52 46 52 30 M8 30 C8 46 30 46 30 30 S52 14 52 30",
      echo: "M8 30 h4 M18 30 h3 M27 30 h2 M35 30 h2 M42 30 h1 M8 22 v16 M18 24 v12 M27 26 v8 M35 27 v6 M42 28 v4",
      arcs: "M8 46 A22 22 0 0 1 52 46 M16 46 A14 14 0 0 1 44 46 M24 46 A6 6 0 0 1 36 46",
      meter: "M8 46 V36 M16 46 V30 M24 46 V24 M32 46 V18 M40 46 V12 M48 46 V8",
      notes: "M20 40 V14 L44 10 V36 M20 40 a5 4 0 1 1 -1 -1 M44 36 a5 4 0 1 1 -1 -1",
      gate: "M2 40 H16 V18 H44 V40 H58",
      spike: "M2 38 H18 L22 10 L26 38 H32 L34 26 L36 38 H58",
      clip: "M2 30 C8 30 10 14 16 14 H44 C50 14 52 30 58 30 M2 30 C8 30 10 46 16 46 H44 C50 46 52 30 58 30",
      rings: "M24 30 m-14 0 a14 14 0 1 0 28 0 a14 14 0 1 0 -28 0 M36 30 m-14 0 a14 14 0 1 0 28 0 a14 14 0 1 0 -28 0",
      maxbars: "M4 12 H56 M10 46 V20 M18 46 V14 M26 46 V16 M34 46 V13 M42 46 V18 M50 46 V24",
      lowwave: "M2 30 C12 6 22 6 30 30 S48 54 58 30",
    };
    return `<svg class="sr-art sr-art-${plug}" viewBox="0 0 60 56"><defs><linearGradient id="g-${kind}-${plug}" x1="0" x2="1"><stop offset="0" stop-color="${col[0]}"/><stop offset="1" stop-color="${col[1]}"/></linearGradient></defs><path d="${P[kind] || P.wave}" stroke="url(#g-${kind}-${plug})"/></svg>`;
  }

  function modulesPage(data) {
    const { on, present } = usage(data);
    const keys = Object.keys(CATALOG);
    const counts = {};
    for (const c of CATS) counts[c.key] = keys.filter((k) => c.key === "all" || CATALOG[k].cats.includes(c.key)).length;
    const q = S.search.trim().toLowerCase();
    const shown = keys.filter((k) => (S.filter === "all" || CATALOG[k].cats.includes(S.filter))
      && (!q || (CATALOG[k].name + " " + CATALOG[k].desc + " " + PRODUCTS[CATALOG[k].plug].name).toLowerCase().includes(q)));
    if (!S.card || !CATALOG[S.card]) S.card = shown[0] || keys[0];
    const plugPresent = (p) => (p === "honeytune" ? !!(on["ht.tune"] || present.honeytune) : !!present[p]);

    const cards = shown.map((k) => {
      const c = CATALOG[k];
      const n = (on[k] || []).length;
      const tag = c.cats.filter((x) => x !== "utility")[0] || c.cats[0] || "utility";
      const here = plugPresent(c.plug);
      return `<button class="sr-card ${S.card === k ? "sr-picked" : ""} ${here ? "" : "sr-card-away"}" data-sr="card" data-key="${k}">
        <div class="sr-ctags"><span class="sr-ctag sr-ctag-${tag}">${tag.toUpperCase()}</span>${n ? `<span class="sr-con">ON IN ${n}</span>` : ""}</div>
        <div class="sr-cbody">${cardArt(c.art, c.plug)}<div><h5>${esc(c.name.toUpperCase())}</h5><p>${esc(c.desc.split(/[.:;]/)[0])}.</p></div></div>
        <span class="sr-from sr-from-${c.plug}">from ${esc(PRODUCTS[c.plug].name)}</span>
      </button>`;
    }).join("") || `<p class="sr-none">No module matches “${esc(S.search)}”.</p>`;

    const side = CATS.map((c) => `<button class="sr-catrow ${S.filter === c.key ? "sr-picked" : ""}" data-sr="filter" data-filter="${c.key}">${icon(c.icon)}<span>${c.name}</span><b>${counts[c.key]}</b></button>`).join("");
    const proj = tracks(data).map((t) => `<button class="sr-prow" data-sr="track" data-track="${esc(t.key)}"><span>${esc(t.name)}</span><span class="sr-badges">${t.units.map((u) => `<b class="sr-badge sr-tone-${PRODUCTS[u.product].tone}">${PRODUCTS[u.product].badge}</b>`).join("")}</span></button>`).join("");
    const chips = CATS.map((c) => `<button class="sr-fchip ${S.filter === c.key ? "sr-on" : ""}" data-sr="filter" data-filter="${c.key}">${c.key === "all" ? "" : icon(c.icon)}${c.chip}</button>`).join("");

    const c = CATALOG[S.card];
    const where = on[S.card] || [];
    const here = plugPresent(c.plug);
    const whereHtml = where.length ? `<ul class="sr-where">${where.map((w) => `<li><button data-sr="goto" data-track="${esc(w.track)}" data-unit="${esc(w.unit)}" data-key="${esc(w.mod)}">${icon("chain")}<span>${esc(w.name)}</span>${icon("right")}</button></li>`).join("")}</ul>`
      : here ? `<p class="sr-dim">Not on in this project right now. Switch it on in ${esc(PRODUCTS[c.plug].name)}${c.plug === "mixology" ? " (each track's Mixology Link, or the Full Mix)" : ""}.</p>`
      : `<p class="sr-dim">${esc(PRODUCTS[c.plug].name)} isn't in this project. ${c.plug === "voxology" || c.plug === "honeytune" ? "Add Voxology to a vocal track to use it." : c.plug === "masterology" ? "Add Masterology on the master channel, after Mixology." : "Add Mixology Link to a track (and Mixology on the master channel)."}</p>`;
    const detail = `<section class="sr-panel sr-cardinfo">
      <div class="sr-ci-head">${icon(c.icon, "sr-tone-" + PRODUCTS[c.plug].tone)}<h3>${esc(c.name.toUpperCase())}</h3></div>
      <div class="sr-ci-art">${cardArt(c.art, c.plug)}</div>
      <p class="sr-ci-desc">${esc(c.desc)}</p>
      <h4>IDEAL USES</h4><ul class="sr-uses">${c.uses.map((u) => `<li>${icon("check", "sr-li-ok")}<span>${esc(u)}</span></li>`).join("")}</ul>
      <h4>KEY CONTROLS</h4><div class="sr-ci-kn">${c.controls.slice(0, 5).map((n, i) => `<div><b>${esc(n.toUpperCase())}</b>${knob({ norm: 0.25 + 0.12 * i }, 46)}</div>`).join("")}</div>
      <h4>WHERE IT PLAYS</h4>${whereHtml}${turnOnHere(data, S.card)}
      <div class="sr-from-big sr-from-${c.plug}">from ${esc(PRODUCTS[c.plug].name)}</div>
    </section>`;

    return `<main class="sr-main sr-main-modules">
      <section class="sr-panel sr-left sr-browse">
        <h3 class="sr-ph">${icon("layers")}BROWSE MODULES</h3>
        <div class="sr-cats">${side}</div>
        <div class="sr-honey sr-honey-strip"></div>
        <h3 class="sr-ph sr-ph2">${icon("chain")}IN THIS PROJECT</h3>
        <div class="sr-proj" data-scroll="proj">${proj || '<p class="sr-dim">No Sonicology plug-ins are playing.</p>'}</div>
      </section>
      <div class="sr-centre">
        <div class="sr-filters"><label class="sr-search">${icon("search")}<input data-sr="search" placeholder="Search modules..." value="${esc(S.search)}">${S.search ? `<button data-sr="clear">×</button>` : ""}</label>${chips}</div>
        <section class="sr-panel sr-cards" data-scroll="cards">${cards}</section>
      </div>
      ${detail}
    </main>`;
  }

  // TURN ON HERE: the module is in a plug-in on the track you picked, switched off: one click switches it on there.
  function turnOnHere(data, key) {
    const t = trackOf(data, S.track);
    if (!t || !S.opts.request) return "";
    for (const u of t.units) {
      const m = (u.modules || []).find((x) => x.key === key);
      if (m && m.state === "off" && m.switchable && canTurn(u))
        return `<button class="sr-turnon" data-sr="turnon" data-unit="${esc(u.id)}" data-key="${esc(key)}">${icon("power")}TURN ON HERE<small>${esc(t.name)} \u00B7 ${esc(PRODUCTS[u.product].name)}</small></button>`;
    }
    return "";
  }

  // ---- render, update, events -----------------------------------------------------------------------------------------
  function render() {
    const data = S.data;
    if (!data) return;
    const scrolls = {};
    S.stage.querySelectorAll("[data-scroll]").forEach((el) => { scrolls[el.dataset.scroll] = [el.scrollTop, el.scrollLeft]; });
    const focusSearch = document.activeElement && document.activeElement.dataset && document.activeElement.dataset.sr === "search";
    const caret = focusSearch ? document.activeElement.selectionStart : 0;
    const t = trackOf(data, S.track);
    const sel = S.page === "rack" ? findSel(t) : null;
    // Only the parts whose HTML changed are rebuilt (a whole-page rebuild is slow in a plug-in's web view, and a click
    // that lands while its button is being replaced is lost).
    const html = {
      frame: `<div class="sr-frame"><i class="sr-c sr-c-tl"></i><i class="sr-c sr-c-tr"></i><i class="sr-c sr-c-bl"></i><i class="sr-c sr-c-br"></i></div>`,
      header: header(data),
      page: S.page === "rack" ? rackPage(data) : S.page === "session" ? sessionPage(data) : S.page === "coach" ? coachPage(data) : modulesPage(data),
      footer: S.page === "coach" ? coachBar(data) : footer(data, sel ? sel.unit : t ? t.units[0] : null),
      menu: S.menu ? trackMenu(data) : "",
    };
    for (const k of Object.keys(html)) if (html[k] !== S.html[k]) { S.parts[k].innerHTML = html[k]; S.html[k] = html[k]; }
    S.eqKey = "";
    S.stage.querySelectorAll("[data-scroll]").forEach((el) => { const s = scrolls[el.dataset.scroll]; if (s) { el.scrollTop = s[0]; el.scrollLeft = s[1]; } });
    if (focusSearch) { const i = S.stage.querySelector('[data-sr="search"]'); if (i) { i.focus(); i.setSelectionRange(caret, caret); } }
  }

  // What the page shows, so it is rebuilt only when that changes: on the RACK page the picked track's modules in detail,
  // the other tracks by their modules' keys and states; on the other pages every unit's modules by state and summary.
  // Not the live readings (meters, a dynamic EQ's moving cuts, a knob that only shows something like Masterology's
  // Dynamics bands): updateLive changes those in place. (Beta 27: the COACH, MODULES and SESSION pages took every
  // plug-in's knob texts, so a playing song rebuilt their HTML several times a second.)
  function signature(data) {
    const t = trackOf(data, S.track);
    const here = new Set(t ? t.units.map((u) => u.id) : []);
    if (S.page !== "rack")
      return JSON.stringify((data.units || []).map((u) => [u.id, u.track, u.role, u.product, u.on, u.version, u.lock || "", u.gen != null,
          (u.modules || []).map((m) => [m.key, m.state, m.summary, !!m.switchable]), (u.together || []).map((l) => l.level)]))
        + JSON.stringify((data.together || []).map((l) => [l.level, l.hash])) + JSON.stringify(data.mix && data.mix.source)
        + (S.page === "coach" ? coachSig(t) : "") + (guideOn() ? guideSig(data) : "")
        + (S.page === "session" ? (mixFixReport(data).mix || {}).report + tracks(data).map((x) => (orderLine(x) ? 1 : 0)).join("") + JSON.stringify(data.otherLayouts || []) : "");
    return JSON.stringify((data.units || []).map((u) => (here.has(u.id)
      ? [u.id, u.track, u.role, u.product, u.on, u.version, (u.modules || []).map((m) => [m.key, m.state, m.summary, m.by, (m.values || []).map((v) => v.key ? v.text : ""),
          (m.bands || []).map((b) => [b.type, Math.round(b.hz || 0), b.name, b.on]), m.layers]), u.together]
      : [u.id, u.track, u.role, u.product, u.on, u.version, (u.modules || []).map((m) => m.key + ":" + m.state)])))
      + JSON.stringify(data.together) + JSON.stringify(data.mix && data.mix.source)
      + JSON.stringify((trackFader(data, t) || {}).v || null) + (orderLine(t) ? "order" : "");
  }
  // What changes the GUIDED MIX panel (its listening bar moves in place: updateLive).
  function guideSig(data) {
    const g = S.guide, { r } = mixFixReport(data);
    return JSON.stringify([g.at, g.done, guideSteps(data).map((x) => x.id), r && [r.analyzed, r.busy, r.seconds, (r.hearing || 0) >= (r.enough || 20), r.playing],
      guideSteps(data).filter((x) => x.track).map((x) => guideCoachState(x.track))]);
  }
  // What changes the COACH page (its progress moves in place: updateLive).
  function coachSig(t) {
    const c = coachOf(t);
    if (!c) return "nocoach";
    const r = c.r, p = r.plan || {};
    return JSON.stringify([c.kind, c.unit && c.unit.id, c.unit && c.unit.lock, r.state, r.style, r.intensity, r.canUndo, r.reference, r.hearing, r.track, r.role, r.heardEnough,
      r.genre, r.target, r.canFinish, p.ok, p.summary, p.changes, p.upToDate, S.coachWhy, r.rev, r.preview, r.planGapDb, coachAsks(c)]);
  }

  // Mixology's faders of the tracks the Rack shows (its "mix.faders", through its DAW bridge): not a module of the Full
  // Mix but a knob of each track (GAIN STAGING), so they leave the modules here.
  function pullFaders(data) {
    for (const u of data.units || []) {
      const i = (u.modules || []).findIndex((m) => m.key === "mix.faders");
      if (i >= 0) u.faders = u.modules.splice(i, 1)[0];
    }
  }
  function trackFader(data, t) {
    if (!data || !t || !t.hash || t.role === "fullmix" || t.role === "master") return null;
    const mu = (data.units || []).find((u) => u.product === "mixology" && u.role === "fullmix" && u.faders);
    const v = mu && (mu.faders.values || []).find((x) => x.key === "fader." + t.hash);
    return v ? { unit: mu, mod: mu.faders, v } : null;
  }

  // A plug-in sends its modules' settings only while a Rack is open, and only the picked track's; between, the Rack
  // keeps the last ones it had (a moment without them must not empty the knobs).
  function keepDetails(data) {
    for (const u of data.units || []) {
      const c = (S.detail[u.id] = S.detail[u.id] || {});
      for (const m of u.modules || []) {
        const has = (m.values && m.values.length) || (m.bands && m.bands.length);
        if (u.detailed !== false) c[m.key] = { values: m.values, bands: m.bands, spectrum: m.spectrum };
        else if (!has && c[m.key]) { m.values = c[m.key].values; m.bands = c[m.key].bands; if (!m.spectrum) m.spectrum = c[m.key].spectrum; }
      }
    }
  }

  function updateLive(data) {
    // meters, loudness and the module's history graph, without rebuilding the page
    const t = trackOf(data, S.track);
    if (guideOn()) {
      const { r } = mixFixReport(data);
      const bar = S.stage.querySelector("[data-guide-bar]");
      const w = r && r.busy ? clamp(((r.hearing || 0) / (r.enough || 20)) * 100, 0, 100).toFixed(0) + "%" : "0%";
      if (bar && bar.style.width !== w) bar.style.width = w;
    }
    if (S.page === "coach") {
      const c = coachOf(t);
      const pct = c ? clamp(c.r.progress || 0, 0, 100) : 0;
      const bar = S.stage.querySelector("[data-coach-bar]"), txt = S.stage.querySelector("[data-coach-pct]");
      if (bar && bar.style.width !== pct + "%") bar.style.width = pct + "%";
      if (txt && txt.textContent !== pct + " %") txt.textContent = pct + " %";
      const heard = S.stage.querySelector("[data-coach-heard]"), ht = c ? secsText(c.r.heardSeconds) + " heard" : "";
      if (heard && heard.textContent !== ht) heard.textContent = ht;
      return;
    }
    const sel = S.page === "rack" ? findSel(t) : null;
    const unit = sel ? sel.unit : t ? t.units[0] : null;
    const set = (sel2, text) => { const el = S.stage.querySelector(`[data-val="${sel2}"]`); if (el && el.textContent !== text) el.textContent = text; };
    const meter = (id, db) => {
      const el = S.stage.querySelector(`[data-meter="${id}"] i`);
      const w = (clamp((db + 60) / 60, 0, 1) * 100).toFixed(1) + "%";
      if (el && el.style.width !== w) el.style.width = w;
    };
    if (unit) { meter("in", unit.inDb ?? -100); meter("out", unit.outDb ?? -100); set("in", dbText(unit.inDb, "dB")); set("out", dbText(unit.outDb, "dB")); }
    if (t && S.page === "rack") updateGain(t);
    const mix = data.mix || {};
    set("lufsI", dbText(mix.lufsI, "LUFS")); set("lufsS", dbText(mix.lufsS, "LUFS")); set("tp", dbText(mix.tp, "dBTP"));
    if (sel) {
      const c = cat(sel.mod.key);
      if (sel.mod.meterKind === "gr") set("meter", num(sel.mod.meter || 0));
      // knobs that only show something (no key): their reading and arc, in place
      (sel.mod.values || []).forEach((v, i) => {
        if (v.key) return;
        const cell = S.stage.querySelector(`.sr-mod [data-vi="${i}"]`);
        if (!cell) return;
        const txt = cell.querySelector(".sr-kval-t") || cell.querySelector("i");
        if (txt && txt.textContent !== v.text) txt.textContent = v.text;
        const arc = cell.querySelector(".sr-kval");
        if (arc) {
          const html = knobArc(clamp(v.norm == null ? 0.5 : v.norm, 0, 1), v.bi);
          if (arc.dataset.a !== html) { arc.dataset.a = html; arc.innerHTML = html; }
        }
      });
      const isEq = c.graph === "eq" && (sel.mod.bands || []).length;
      const wrap = S.stage.querySelector(isEq ? ".sr-graphwrap .sr-graph-eq" : ".sr-graphwrap .sr-graph-hist");
      if (wrap) {
        let html = null;
        if (!isEq) html = historyGraph(sel.unit.id + "|" + sel.mod.key, sel.mod.meterKind === "gr" || c.graph === "gr" ? "gr" : "level", 640, 210);
        else {
          const k = JSON.stringify([(sel.mod.bands || []).map((b) => b.db), sel.mod.spectrum]);
          if (k !== S.eqKey) { S.eqKey = k; html = eqGraph(sel.mod, 640, 210); }
        }
        if (html) { const tmp = document.createElement("div"); tmp.innerHTML = html; wrap.replaceWith(tmp.firstChild); }
      }
    }
  }

  function remember(data) {
    // a few seconds of each module's meter (gain reduction or level), for its graph
    for (const u of data.units || []) for (const m of u.modules || []) {
      const key = u.id + "|" + m.key;
      const v = m.meterKind === "gr" ? m.meter || 0 : m.meterKind === "level" ? m.meter : u.outDb;
      if (v == null) continue;
      const h = (S.hist[key] = S.hist[key] || []);
      h.push(v);
      if (h.length > 120) h.splice(0, h.length - 120);
    }
  }

  // Beta 25: the insert order on a vocal. The DAW doesn't tell the plug-ins, and Mixology Link belongs after Voxology (so
  // Mixology hears the finished vocal). Voxology's processing changes how the level moves (its compressor and rider most):
  // when the Link's input moves like Voxology's input and not like its output, the Link sits before it. Compared with
  // each level's own average taken out (the Link's Trim, Voxology's gain don't count), over several seconds of playing,
  // only when Voxology clearly changes the movement, and only after the same answer for a while (no guess on a hunch).
  function watchOrder(data) {
    const t = trackOf(data, S.track);
    if (!t || t.role === "fullmix" || t.role === "master") return;
    const o = (S.order[t.key] = S.order[t.key] || { s: [], before: 0, after: 0, warn: false });
    const vox = t.units.find((u) => u.product === "voxology" && u.on !== false);
    const link = t.units.find((u) => u.product === "link" && u.on !== false);
    if (!vox || !link || vox.role === "beat" || !known(vox.inDb) || !known(vox.outDb) || !known(link.inDb) || vox.inDb < -50) return;
    const x = [vox.inDb, vox.outDb, link.inDb], last = o.s[o.s.length - 1];
    if (last && last[0] === x[0] && last[1] === x[1] && last[2] === x[2]) return;   // the same publish again
    o.s.push(x);
    if (o.s.length > 50) o.s.shift();
    if (o.s.length < 30) return;
    const n = o.s.length, mean = [0, 1, 2].map((i) => o.s.reduce((a, r) => a + r[i], 0) / n);
    const apart = (i, j) => o.s.reduce((a, r) => a + Math.abs((r[i] - mean[i]) - (r[j] - mean[j])), 0) / n;
    const shaped = apart(0, 1);                 // how much Voxology changes the movement
    const likeIn = apart(2, 0), likeOut = apart(2, 1);
    const verdict = shaped < 1.2 ? "" : likeIn < 0.5 * likeOut && likeIn < 0.6 * shaped ? "before" : likeOut < 0.5 * likeIn ? "after" : "";
    o.before = verdict === "before" ? o.before + 1 : 0;
    o.after = verdict === "after" ? o.after + 1 : 0;
    if (o.before >= 10) o.warn = true;
    if (o.after >= 10) o.warn = false;
  }
  const orderLine = (t) => t && S.order[t.key] && S.order[t.key].warn
    ? { level: "warn", text: "Mixology Link seems to sit before Voxology on this track: what it hears moves like the raw vocal, not like Voxology's output. Move Mixology Link to the last insert slot (after Voxology), so Mixology mixes the finished vocal." }
    : null;

  // The track the view asks the plug-ins about in detail: the picked one, or the Full Mix on the SESSION page.
  const wantTrack = () => (S.page === "session" ? "fullmix" : S.track);

  async function poll() {
    if (!S.root) return;
    let data = null;
    if (S.opts.dataFile && !S.noFile) {
      try {
        const r = await fetch("rack-data/" + encodeURIComponent(wantTrack() || "_") + ".json?n=" + Date.now(), { cache: "no-store" });
        if (r.ok) data = await r.json();
        else S.noFile = true;
      } catch (e) { S.noFile = true; }   // this plug-in doesn't serve it: getRack from now on
    }
    if (!data) { try { data = await S.opts.getRack(wantTrack() || ""); } catch (e) { data = null; } }
    if (!S.root) return;
    if (data) {
      pullFaders(data);
      keepDetails(data);
      settleHeld(data);
      remember(data);
      watchOrder(data);
      dragText(data);
      guideObserve(data);
      const first = !S.data;
      S.data = data;
      if (first || !trackOf(data, S.track)) S.track = S.track && trackOf(data, S.track) ? S.track : hostTrackKey(data);
      const sig = signature(data);
      if (first || sig !== S.sig) {
        S.sig = sig;
        if (S.down) S.pending = true;   // a button is held: rebuild after the click, never under it
        else render();
      } else updateLive(data);
    }
    S.timer = setTimeout(poll, S.drag || S.wheel ? 80 : 150);   // quicker while a knob turns, so its value follows
  }

  // Every part built again from scratch (a button disabled in place by a click is drawn as it should be).
  function redraw() {
    if (!S.root || !S.data) return;
    S.html = {};
    S.sig = "";
    if (S.down) S.pending = true;
    else render();
  }

  // While you drag, the page isn't rebuilt (the knob is under your mouse): the plug-in's own text for the value
  // ("−18.0 dB") goes into it in place.
  function dragText(data) {
    const d = S.drag;
    if (!d || !d.moved || !d.el.isConnected) return;
    const u = (data.units || []).find((x) => x.id === d.unit.id);
    const m = u && (d.module === "mix.faders" ? u.faders : (u.modules || []).find((x) => x.key === d.module));
    const v = m && (m.values || []).find((x) => x.key === d.control);
    const cell = d.el.closest(".sr-kcell, .sr-gain-fader");
    const t = cell && cell.querySelector(".sr-kval-t");
    if (v && t && t.textContent !== v.text) t.textContent = v.text;
  }

  function onClick(e) {
    if (S.rowDragged) { S.rowDragged = false; return; }   // (the click that ends a row drag)
    const el = e.target.closest("[data-sr]");
    if (!el) { if (S.menu) { S.menu = false; render(); } return; }
    const a = el.dataset.sr;
    const list = S.data ? tracks(S.data) : [];
    const idx = list.findIndex((t) => t.key === S.track);
    if (a !== "track-menu" && a !== "menu") S.menu = false;
    if (a === "back") { close(); if (S.opts && S.opts.back) S.opts.back(); return; }
    else if (a === "theme") {
      setTheme(S.theme === "light" ? "dark" : "light");
      if (S.opts && S.opts.onTheme) { try { S.opts.onTheme(S.theme); } catch (e) { /* the plug-in's problem */ } }
    }
    else if (a === "page") { S.page = el.dataset.page; }
    else if (a === "track-prev" && idx > 0) { S.track = list[idx - 1].key; S.sel = null; }
    else if (a === "track-next" && idx >= 0 && idx < list.length - 1) { S.track = list[idx + 1].key; S.sel = null; }
    else if (a === "track-menu") { S.menu = !S.menu; }
    else if (a === "track") { S.track = el.dataset.track; S.sel = null; S.page = "rack"; }
    else if (a === "fix-coach") { S.track = el.dataset.track; S.sel = null; S.page = "coach"; }
    else if (a === "mod") { S.sel = { unit: el.dataset.unit, key: el.dataset.key }; }
    else if (a === "unfold") { S.unfold[el.dataset.unit] = !S.unfold[el.dataset.unit]; }
    else if (a === "filter") { S.filter = el.dataset.filter; S.card = null; }
    else if (a === "card") { S.card = el.dataset.key; }
    else if (a === "goto") { S.track = el.dataset.track; S.sel = { unit: el.dataset.unit, key: el.dataset.key }; S.page = "rack"; }
    else if (a === "clear") { S.search = ""; S.card = null; }
    else if (a === "switch") { switchModule(el.dataset.unit, el.dataset.key, el); return; }
    else if (a === "fix") { applyFix(el); return; }
    else if (a === "order-reset") { resetOrder(el.dataset.unit); return; }
    else if (a === "guide-start") { guideStart(S.data, false); }
    else if (a === "guide-restart") { guideStart(S.data, true); }
    else if (a === "guide-exit") {
      // closed half-way: GUIDED MIX goes on from here next time; closed at the end: it starts fresh
      if (S.guide.at === "done") { S.guide = null; guideSave(); } else { S.guide.on = false; guideSave(); S.guide = null; }
      S.page = "session";
    }
    else if (a === "guide-go") { guideGo(el.dataset.step); }
    else if (a === "guide-next") { if (S.guide.at === "first") S.guide.done.first = "seen"; guideMove(1); }
    else if (a === "guide-back") { guideMove(-1); }
    else if (a === "guide-skip") { S.guide.done[S.guide.at] = "skipped"; guideMove(1); }
    else if (a === "guide-analyze" || a === "guide-finish" || a === "guide-cancel") {
      const mix = mixOwner(S.data);
      if (!mix) { toast("Put Mixology on your master channel first."); return; }
      const step = a === "guide-analyze" ? "mixanalyze" : a === "guide-finish" ? "mixfinish" : "mixcancel";
      el.disabled = true;
      send(mix, { change: "coach", module: "coach", control: step + ".0", value: 1, gesture: "set" }, () => {
        if (step === "mixanalyze") toast("Mixology is listening: play the busiest part of your song (a chorus).", null, 6000);
        else if (step === "mixfinish") toast("Mixology has heard your mix: WHAT TO FIX FIRST shows what stands out.");
      });
      return;
    }
    else if (a === "undo") { replay(true); return; }
    else if (a === "redo") { replay(false); return; }
    else if (a === "bypass") { toggleBypass(); return; }
    else if (a === "ab") { flipAB(el.dataset.ab); return; }
    else if (a === "coach-plan" || a === "coach-apply" || a === "coach-undo" || a === "coach-finish" || a === "coach-stop"
             || a === "coach-preview" || a === "coach-flip" || a === "coach-keep" || a === "coach-back") {
      const t = trackOf(S.data, S.track), c = coachOf(t);
      if (!c) return;
      const step = a === "coach-stop" ? coachStopStep(c) : a.slice(6);
      if (!c.unit) return;
      if (step === "flip" && el.dataset.to === c.r.preview) return;   // (already playing)
      el.disabled = true;
      const mixk = c.kind === "mix", mk = c.kind === "master";
      send(c.unit, { change: "coach", module: "coach", control: coachControl(c, t, step), value: 1, gesture: "set" }, () => {
        if (step === "plan") toast(mk ? "Masterology is listening: play the song (the whole song is best; BUILD NOW after 12 seconds). Nothing changes until you apply."
          : mixk ? (c.r.heardEnough ? `Mixology is building the plan for ${t.name}. Nothing changes until you apply.` : `Play a part where ${t.name} plays: the Coach listens to it first. Nothing changes until you apply.`)
          : "Voxology is listening: play the song (about 12 seconds of singing). Nothing changes until you apply.", null, 6000);
        else if (step === "finish") toast("Building the master from what it heard. Nothing changes until you apply.", null, 5000);
        else if (step === "cancel" || step === "discard") toast("Stopped: nothing changed.");
        else if (step === "preview") toast("PREVIEW: you hear the plan (nothing is kept yet). Flip PLAN / YOURS while the song plays.", null, 6000);
        else if (step === "flip") return;
        else if (step === "back") toast("Back to yours: nothing changed.");
        else if (step === "apply" || step === "keep") toast(mk ? "Applied: Masterology set the plan on your master (one step in its UNDO). UNDO puts the settings from before back."
          : mixk ? `Applied: Mixology set the plan on ${t.name} (one step in its History). UNDO puts the settings from before back.`
          : `Applied: Voxology set the plan on ${t.name}. UNDO puts the settings from before back.`, null, 6000);
        else toast("Undone: the settings from before are back.");
      });
      return;
    }
    else if (a === "coach-style" || a === "coach-int" || a === "coach-genre" || a === "coach-target") {
      const t = trackOf(S.data, S.track), c = coachOf(t);
      if (!c) return;
      const mk = c.kind === "master";
      const list = a === "coach-style" ? c.r.styles : a === "coach-genre" ? c.r.genres : a === "coach-target" ? c.r.targets : c.r.intensities;
      const now = a === "coach-style" ? c.r.style : a === "coach-genre" ? c.r.genre : a === "coach-target" ? c.r.target : c.r.intensity;
      const n = (list || []).length;
      if (n < 2) return;
      const i = a === "coach-int" ? clamp(+el.dataset.i || 0, 0, n - 1) : ((now || 0) + (+el.dataset.dir || 1) + n) % n;
      const control = { "coach-style": "aeStyle", "coach-genre": "amGenre", "coach-target": "amTarget", "coach-int": mk ? "amIntensity" : "aeIntensity" }[a];
      send(c.unit, { change: "value", module: "coach", control, value: i / (n - 1), gesture: "set" }, true);
      return;
    }
    else if (a === "coach-why") { S.coachWhy = S.coachWhy === el.dataset.key ? null : el.dataset.key; }
    else if (a === "savechain") {
      const unit = unitById(el.dataset.unit);
      if (!unit) return;
      el.disabled = true;
      // Voxology names it after the track and the time, as here ("Lead Vox 17 Nov 22.41").
      const d = new Date(), two = (n) => String(n).padStart(2, "0");
      const name = `${unit.track || "Rack chain"} ${d.getDate()} ${"Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split(" ")[d.getMonth()]} ${two(d.getHours())}.${two(d.getMinutes())}`;
      send(unit, { change: "save", module: "save", control: "", value: 1, gesture: "set" }, () =>
        toast(`Saved in Voxology's MY CHAINS as "${name}": use it on your next song (Voxology: MY CHAINS).`, null, 7000));
      setTimeout(() => { if (el.isConnected) el.disabled = false; }, 2500);
      return;
    }
    else if (a === "turnon") {
      const unit = unitById(el.dataset.unit), key = el.dataset.key;
      if (!unit) return;
      el.disabled = true;
      send(unit, { change: "state", module: key, control: "", value: 1, gesture: "set" }, () => {
        rememberChange({ kind: "state", unit: unit.id, gen: unit.gen, module: key, before: 0, after: 1 }, `${cat(key).name} on`);
        toast(`${cat(key).name} is on in ${PRODUCTS[unit.product].name} (${unit.track || "this track"}).`, () => replay(true));
      });
      return;
    }
    else if (a === "toast-act") { const act = S.toastAct; hideToast(); if (act) act(); return; }
    else return;
    render();
  }

  // ---- stage 2: turning knobs and switching modules through the plug-in that owns them --------------------------------
  const unitById = (id) => (S.data && (S.data.units || []).find((u) => u.id === id)) || null;

  // One request at a time, in order (a drag's begin before its moves, its moves before its end).
  function send(unit, req, check) {
    const r = Object.assign({ unit: unit.id, gen: unit.gen }, req);
    S.chain = S.chain.then(async () => {
      let res = null;
      try { res = await S.opts.request(r); } catch (e) { res = { ticket: 0, why: "The Rack couldn't reach the plug-in." }; }
      if (!res || !res.ticket) {
        toast((res && res.why) || "That didn't go through.");
        if (r.change === "value") delete S.held[heldKey(unit, r.module, r.control)];
        if (r.change === "coach") redraw();
        return;
      }
      if (check) watchAnswer(unit, res.ticket, r, 0, typeof check === "function" ? check : null);
    });
    return S.chain;
  }

  // After a click or the end of a drag: say so when the plug-in refused it (`done`: what to do once it took it).
  function watchAnswer(unit, ticket, r, tries, done) {
    setTimeout(async () => {
      if (!S.root || !S.opts.answer) return;
      let a = null;
      try { a = await S.opts.answer(unit.id, unit.gen, ticket); } catch (e) { a = null; }
      if (!a) return;
      if (a.outcome === "pending" && tries < 10) { watchAnswer(unit, ticket, r, tries + 1, done); return; }
      if (a.outcome === "refused" || a.outcome === "gone") {
        toast(a.why || (a.outcome === "gone" ? "That plug-in has closed or moved." : "Not done."));
        if (r.change === "value") { delete S.held[heldKey(unit, r.module, r.control)]; render(); }
      } else if (a.outcome === "applied") {
        if (r.change === "value") watchSnapBack(unit, r);
        if (done) done();
      }
      if (r.change === "coach") redraw();   // (Beta 32: the button it disabled comes back even when nothing else changed)
    }, tries ? 300 : 450);
  }

  // A value the plug-in took, and a moment later it is back where it was: the track's automation is reading that
  // knob (Cubase: R lit), and the DAW moves it back as the song plays. Say so once a while per knob. (Not for
  // Honey Tune: its settings aren't automated.)
  function watchSnapBack(unit, r) {
    if (unit.product === "honeytune") return;
    const k = heldKey(unit, r.module, r.control);
    const sent = (S.sent[k] = { value: r.value, at: Date.now() });
    setTimeout(() => {
      if (!S.root || S.sent[k] !== sent || (S.drag && S.drag.key === k) || (S.wheel && S.wheel.key === k)) return;
      const u = unitById(unit.id);
      if (!u || u.gen !== unit.gen || u.detailed === false) return;
      const m = (u.modules || []).find((x) => x.key === r.module);
      const v = m && (m.values || []).find((x) => x.key === r.control);
      if (!v || v.norm == null || Math.abs(v.norm - r.value) <= 0.03) return;
      if (Date.now() - (S.snapTold[k] || 0) < 30000) return;
      S.snapTold[k] = Date.now();
      toast(`${v.name} went back to where it was: your DAW's automation is reading it (in Cubase: the R button is lit on this track). ` +
        "Switch Read off, or use Write (W) to record the move.", null, 8000);
    }, 1800);
  }

  // A message at the bottom of the Rack; `act` gives it an UNDO button.
  function toast(text, act, ms) {
    if (!S.stage) return;
    let el = S.stage.querySelector(".sr-toast");
    if (!el) { el = document.createElement("div"); el.className = "sr-toast"; S.stage.appendChild(el); }
    S.toastAct = act || null;
    el.innerHTML = `${icon("info")}<span>${esc(text)}</span>${act ? '<button class="sr-toast-btn" data-sr="toast-act">UNDO</button>' : ""}`;
    el.classList.toggle("sr-toast-act", !!act);
    el.classList.add("sr-shown");
    clearTimeout(S.toastTimer);
    S.toastTimer = setTimeout(hideToast, ms || (act ? 8000 : 4500));
  }

  function hideToast() {
    clearTimeout(S.toastTimer);
    S.toastAct = null;
    const el = S.stage && S.stage.querySelector(".sr-toast");
    if (el) el.classList.remove("sr-shown", "sr-toast-act");
  }

  // A Coach warning's fix: switch that module (on or off) in its own plug-in, with an UNDO that switches it back.
  function applyFix(el) {
    const unit = unitById(el.dataset.unit) || { id: el.dataset.unit, gen: +el.dataset.gen };
    if (unit.gen !== +el.dataset.gen || unit.lock || !S.opts.request) { toast(unit.lock ? unit.lock : "That plug-in has closed or moved."); return; }
    const module = el.dataset.module, on = el.dataset.on === "1";
    const label = el.textContent;
    el.disabled = true;
    const go = (v, then) => send(unit, { change: "state", module, control: "", value: v ? 1 : 0, gesture: "set" }, then || true);
    go(on, () => {
      rememberChange({ kind: "state", unit: unit.id, gen: unit.gen, module, before: on ? 0 : 1, after: on ? 1 : 0 }, label);
      toast("Done: " + label.replace(/^Turn /, "turned ") + ".", () => replay(true));
    });
  }

  function switchModule(unitId, key, el) {
    const unit = unitById(unitId);
    const mod = unit && (unit.modules || []).find((m) => m.key === key);
    if (!unit || !mod || !canTurn(unit)) return;
    el.classList.add("sr-pending");
    const before = mod.state === "off" ? 0 : 1;
    send(unit, { change: "state", module: key, control: "", value: 1 - before, gesture: "set" },
      () => rememberChange({ kind: "state", unit: unit.id, gen: unit.gen, module: key, before, after: 1 - before }, `${cat(key).name} ${before ? "off" : "on"}`));
  }

  // A knob's value held where you put it until the plug-in's own value catches up (it publishes a few times a second).
  function settleHeld(data) {
    const now = Date.now();
    for (const k of Object.keys(S.held)) {
      const h = S.held[k];
      if ((S.drag && S.drag.key === k) || (S.wheel && S.wheel.key === k)) continue;
      const [uid, module, control] = k.split("|");
      const u = (data.units || []).find((x) => x.id === uid);
      const m = u && (u.modules || []).find((x) => x.key === module);
      const v = m && (m.values || []).find((x) => x.key === control);
      if (!v || Math.abs((v.norm ?? -1) - h.norm) < 0.006 || now - h.at > 2500) delete S.held[k];
    }
  }

  function paintKnob(el, norm) {
    const svg = el.querySelector(".sr-kval"), cap = el.querySelector(".sr-kcap");
    if (svg) svg.innerHTML = knobArc(norm, el.dataset.bi === "1");
    if (cap) cap.style.transform = `rotate(${(-135 + norm * 270).toFixed(1)}deg)`;
    el.dataset.norm = String(norm);
    S.html.page = "";   // drawn in place: the next render rewrites the page whatever it holds
  }

  // ---- Beta 33: moving a module (drag its row in SIGNAL CHAIN) -------------------------------------------------------
  // Only the modules a plug-in marks movable, only among that plug-in's own movable modules (the plug-in keeps the
  // rest where they must be: tuning first, the limiter last). A short move is still a click (it picks the module).
  const movableKeys = (u) => (u.modules || []).filter((m) => m.movable).map((m) => m.key);
  function onRowDown(e) {
    const el = e.target.closest && e.target.closest('[data-move="1"]');
    if (!el || e.button > 0 || S.drag) return;
    const unit = unitById(el.dataset.unit);
    if (!unit || !canTurn(unit)) return;
    S.rowDrag = { el, unit, key: el.dataset.key, y: e.clientY, started: false, at: -1, line: null };
  }
  function onRowMove(e) {
    const d = S.rowDrag;
    if (!d) return;
    const dy = (e.clientY - d.y) / (S.scale || 1);
    if (!d.started && Math.abs(dy) < 6) return;
    if (!d.started) {
      d.started = true;
      d.el.classList.add("sr-rowdrag");
      try { d.el.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
    }
    d.el.style.transform = `translateY(${dy.toFixed(1)}px)`;
    // the drop place: before the first movable row (of this plug-in) whose middle is below the pointer
    const rows = [...S.stage.querySelectorAll(`.sr-row[data-move="1"][data-unit="${CSS.escape(d.unit.id)}"]`)].filter((r) => r !== d.el);
    let before = null;
    for (const r of rows) {
      const b = r.getBoundingClientRect();
      if (e.clientY < b.top + b.height / 2) { before = r; break; }
    }
    const keys = movableKeys(d.unit).filter((k) => k !== d.key);
    d.at = before ? keys.indexOf(before.dataset.key) : rows.length ? keys.indexOf(rows[rows.length - 1].dataset.key) + 1 : 0;
    for (const r of rows) r.classList.remove("sr-drop-before", "sr-drop-after");
    if (before) before.classList.add("sr-drop-before");
    else if (rows.length) rows[rows.length - 1].classList.add("sr-drop-after");
  }
  function onRowUp() {
    const d = S.rowDrag;
    if (!d) return;
    S.rowDrag = null;
    if (!d.started) return;   // a click: it picks the module as usual
    S.rowDragged = true;
    setTimeout(() => { S.rowDragged = false; }, 400);   // (no click follows when the pointer ended elsewhere)
    d.el.classList.remove("sr-rowdrag");
    d.el.style.transform = "";
    S.stage.querySelectorAll(".sr-drop-before, .sr-drop-after").forEach((r) => r.classList.remove("sr-drop-before", "sr-drop-after"));
    const from = movableKeys(d.unit).indexOf(d.key);
    if (d.at < 0 || from < 0 || d.at === from) { S.html.page = ""; S.pending = true; return; }
    const name = cat(d.key).name;
    send(d.unit, { change: "order", module: "order", control: d.key, value: d.at, gesture: "set" }, () => {
      rememberChange({ kind: "order", unit: d.unit.id, gen: d.unit.gen, module: d.key, before: from, after: d.at }, `${name} moved`);
      toast(`${name} moved: ${PRODUCTS[d.unit.product].name} plays it there now. RESET ORDER puts back its own order.`);
    });
    S.html.page = "";   // drawn again (the plug-in's new order arrives with its next report)
    S.pending = true;
  }
  function resetOrder(unitId) {
    const u = unitById(unitId);
    if (!u || !canTurn(u)) return;
    const before = movableKeys(u);
    send(u, { change: "order", module: "order", control: "reset", value: 0, gesture: "set" }, () => {
      rememberChange({ kind: "batch", items: before.map((k, i) => ({ kind: "order", unit: u.id, gen: u.gen, module: k, before: i, after: null })) },
        `${PRODUCTS[u.product].name}'s order`);
      toast(`${PRODUCTS[u.product].name} plays its modules in its own order again.`);
    });
  }

  function onKnobDown(e) {
    const el = e.target.closest && e.target.closest("[data-knob]");
    if (!el || e.button > 0) return;
    const unit = unitById(el.dataset.unit);
    if (!unit || !canTurn(unit)) return;
    e.preventDefault();
    try { el.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
    S.drag = { el, unit, key: el.dataset.knob, module: el.dataset.module, control: el.dataset.control, steps: +el.dataset.steps || 0,
      start: +el.dataset.norm, norm: +el.dataset.norm, y: e.clientY, moved: false, begun: false, lastSend: 0, timer: 0 };
    el.classList.add("sr-dragging");
  }

  function onKnobMove(e) {
    const d = S.drag;
    if (!d) return;
    const dy = (d.y - e.clientY) / (S.scale || 1);   // in the stage's pixels
    if (!d.moved && Math.abs(dy) < 3) return;
    d.moved = true;
    let n = clamp(d.start + dy / (e.shiftKey ? 900 : 220), 0, 1);
    if (d.steps >= 2) n = Math.round(n * (d.steps - 1)) / (d.steps - 1);
    if (n === d.norm && d.begun) return;
    d.norm = n;
    S.held[d.key] = { norm: n, at: Date.now() };
    paintKnob(d.el, n);
    const cell = d.el.closest(".sr-kcell");
    if (cell) cell.classList.add("sr-kpending");
    // begin at once, then the newest value about 25 times a second
    const now = Date.now();
    if (!d.begun) { d.begun = true; d.lastSend = now; send(d.unit, { change: "value", module: d.module, control: d.control, value: n, gesture: "begin" }); return; }
    clearTimeout(d.timer);
    const go = () => { if (S.drag !== d) return; d.lastSend = Date.now(); send(d.unit, { change: "value", module: d.module, control: d.control, value: d.norm, gesture: "move" }); };
    if (now - d.lastSend >= 40) go(); else d.timer = setTimeout(go, 40 - (now - d.lastSend));
  }

  function onKnobUp() {
    const d = S.drag;
    if (!d) return;
    S.drag = null;
    clearTimeout(d.timer);
    d.el.classList.remove("sr-dragging");
    if (d.moved) {
      S.held[d.key] = { norm: d.norm, at: Date.now() };
      send(d.unit, { change: "value", module: d.module, control: d.control, value: d.norm, gesture: "end" },
        valueDone(d.unit, d.module, d.control, d.start, d.norm));
    } else if (d.steps >= 2) {
      // a click: the next option (round to the first after the last)
      const i = Math.round(d.start * (d.steps - 1));
      const n = ((i + 1) % d.steps) / (d.steps - 1);
      S.held[d.key] = { norm: n, at: Date.now() };
      paintKnob(d.el, n);
      const cell = d.el.closest(".sr-kcell");
      if (cell) cell.classList.add("sr-kpending");
      send(d.unit, { change: "value", module: d.module, control: d.control, value: n, gesture: "set" }, valueDone(d.unit, d.module, d.control, d.start, n));
    }
  }

  // Double-click: back to the plug-in's default for that knob (a knob with a few options steps on a click instead).
  function onDblClick(e) {
    const el = e.target.closest && e.target.closest("[data-knob]");
    if (!el || (+el.dataset.steps || 0) >= 2) return;
    const unit = unitById(el.dataset.unit);
    if (!unit || !canTurn(unit)) return;
    e.preventDefault();
    delete S.held[el.dataset.knob];
    const cell = el.closest(".sr-kcell");
    if (cell) cell.classList.add("sr-kpending");
    const before = +el.dataset.norm, module = el.dataset.module, control = el.dataset.control;
    send(unit, { change: "reset", module, control, value: 0, gesture: "set" },
      () => rememberChange({ kind: "value", unit: unit.id, gen: unit.gen, module, control, before, after: null }, `${knobName(unit, module, control)} to default`));
  }

  // The mouse wheel over a knob: a small nudge (Shift: finer), or the next / previous option. One touch for the host
  // (begin, moves, end once the wheel has been still for a moment), so a DAW records it as one move.
  function onWheel(e) {
    const el = e.target.closest && e.target.closest("[data-knob]");
    if (!el) return;
    const unit = unitById(el.dataset.unit);
    if (!unit || !canTurn(unit) || S.drag) return;
    e.preventDefault();
    const raw = Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? e.deltaY : e.deltaX;   // Shift + wheel scrolls sideways
    if (!raw) return;
    const notches = clamp(-raw / (e.deltaMode === 1 ? 3 : e.deltaMode === 2 ? 1 : 100), -3, 3);
    const key = el.dataset.knob, steps = +el.dataset.steps || 0;
    let w = S.wheel;
    if (w && w.key !== key) { endWheel(); w = null; }
    if (!w) w = S.wheel = { key, unit, module: el.dataset.module, control: el.dataset.control, steps, norm: +el.dataset.norm, start: +el.dataset.norm, begun: false, timer: 0, lastSend: 0, sendTimer: 0, acc: 0, lastStep: 0 };
    let n = w.norm;
    if (steps >= 2) {
      // one option per click of the wheel (a trackpad's many small ones add up first)
      w.acc += notches;
      const now = Date.now();
      if (Math.abs(w.acc) < 0.6 || now - w.lastStep < 120) return;
      const i = clamp(Math.round(w.norm * (steps - 1)) + (w.acc > 0 ? 1 : -1), 0, steps - 1);
      w.acc = 0; w.lastStep = now;
      n = i / (steps - 1);
    } else n = clamp(w.norm + notches * (e.shiftKey ? 0.002 : 0.01), 0, 1);
    clearTimeout(w.timer);
    w.timer = setTimeout(endWheel, 400);
    if (n === w.norm) return;
    w.norm = n;
    S.held[key] = { norm: n, at: Date.now() };
    paintKnob(el, n);
    const cell = el.closest(".sr-kcell");
    if (cell) cell.classList.add("sr-kpending");
    const now = Date.now();
    if (!w.begun) { w.begun = true; w.lastSend = now; send(unit, { change: "value", module: w.module, control: w.control, value: n, gesture: "begin" }); return; }
    clearTimeout(w.sendTimer);
    const go = () => { if (S.wheel !== w) return; w.lastSend = Date.now(); send(unit, { change: "value", module: w.module, control: w.control, value: w.norm, gesture: "move" }); };
    if (now - w.lastSend >= 40) go(); else w.sendTimer = setTimeout(go, 40 - (now - w.lastSend));
  }

  function endWheel() {
    const w = S.wheel;
    if (!w) return;
    S.wheel = null;
    clearTimeout(w.timer);
    clearTimeout(w.sendTimer);
    if (!w.begun) return;
    S.held[w.key] = { norm: w.norm, at: Date.now() };
    send(w.unit, { change: "value", module: w.module, control: w.control, value: w.norm, gesture: "end" }, valueDone(w.unit, w.module, w.control, w.start, w.norm));
  }

  // ---- stage 2 step 4: UNDO / REDO, A/B, BYPASS (the Rack's own changes, applied through the same requests) -----------
  const knobName = (unit, module, control) => {
    const u = unitById(unit.id || unit), m = u && (module === "mix.faders" ? u.faders : (u.modules || []).find((x) => x.key === module));
    const v = m && (m.values || []).find((x) => x.key === control);
    return v ? v.name : control;
  };
  function valueDone(unit, module, control, before, after) {
    const what = module === "mix.faders" ? "Track" : cat(module).name;
    return () => rememberChange({ kind: "value", unit: unit.id, gen: unit.gen, module, control, before, after }, `${what} ${knobName(unit, module, control)}`);
  }
  function rememberChange(entry, label) {
    if (S.replaying) return;
    entry.label = label;
    S.undo.push(entry);
    if (S.undo.length > 60) S.undo.shift();
    S.redo = [];
    S.html.header = "";
    if (S.root && !S.down) render();
  }
  const pause = (ms) => new Promise((r) => setTimeout(r, ms));

  // One change back (undo) or forward again (redo); a batch (A/B, BYPASS) as one.
  async function replay(back) {
    const from = back ? S.undo : S.redo, to = back ? S.redo : S.undo;
    const e = from.pop();
    if (!e) return;
    to.push(e);
    S.html.header = "";
    const items = e.kind === "batch" ? e.items : [e];
    const gone = await applyItems(items.map((x) => Object.assign({}, x, { target: back ? x.before : x.after })));
    toast((back ? "Undone: " : "Redone: ") + e.label + (gone ? ` (${gone} of it not: that plug-in has closed or moved)` : "."));
    if (S.root) render();
  }

  // Sends each item's target (a knob's 0..1, null = its default; a module's or a plug-in's 1 / 0), a few at a time so no
  // plug-in's queue overflows. Returns how many couldn't be sent (their plug-in is gone).
  async function applyItems(items) {
    S.replaying = true;
    let gone = 0, n = 0;
    try {
      for (const x of items) {
        const u = unitById(x.unit);
        if (!u || u.gen !== x.gen) { gone++; continue; }
        if (x.kind === "value") {
          if (x.target == null) send(u, { change: "reset", module: x.module, control: x.control, value: 0, gesture: "set" });
          else {
            S.held[heldKey(u, x.module, x.control)] = { norm: x.target, at: Date.now() };
            send(u, { change: "value", module: x.module, control: x.control, value: x.target, gesture: "set" });
          }
        } else if (x.kind === "order") {
          send(u, { change: "order", module: "order", control: x.target == null ? "reset" : x.module, value: x.target == null ? 0 : x.target, gesture: "set" });
        } else send(u, { change: x.kind, module: x.kind === "power" ? "power" : x.module, control: "", value: x.target, gesture: "set" });
        if (++n % 12 === 0) { await S.chain; await pause(120); }
      }
      await S.chain;
    } finally { S.replaying = false; }
    return gone;
  }

  // A / B: two versions of this track's settings (every knob and switch the Rack shows for it). The first click on B
  // starts it as a copy of A; after that, a click sends the differences (one undo step).
  function snapshot(t) {
    const list = [];
    for (const u of t.units) {
      if (!canTurn(u) || u.detailed === false) continue;
      for (const m of u.modules || []) {
        if (m.switchable) list.push({ kind: "state", unit: u.id, gen: u.gen, module: m.key, v: m.state === "off" ? 0 : 1 });
        for (const v of m.values || []) if (v.key && v.norm != null) list.push({ kind: "value", unit: u.id, gen: u.gen, module: m.key, control: v.key, v: v.norm });
      }
    }
    return list;
  }
  async function flipAB(to) {
    const t = trackOf(S.data, S.track);
    if (!t) return;
    const ab = (S.ab[t.key] = S.ab[t.key] || { cur: "a", a: null, b: null });
    if (ab.cur === to) return;
    const now = snapshot(t);
    ab[ab.cur] = now;
    const want = ab[to];
    ab.cur = to;
    S.html.header = "";
    render();
    if (!want) { toast(`${to.toUpperCase()} starts as a copy of ${to === "b" ? "A" : "B"}: change things, then flip between A and B.`); return; }
    const was = new Map(now.map((x) => [x.unit + "|" + x.module + "|" + (x.control || ""), x]));
    const items = [];
    for (const x of want) {
      const cur = was.get(x.unit + "|" + x.module + "|" + (x.control || ""));
      if (!cur || cur.gen !== x.gen) continue;
      if (x.kind === "value" ? Math.abs(cur.v - x.v) > 0.002 : cur.v !== x.v)
        items.push({ kind: x.kind, unit: x.unit, gen: x.gen, module: x.module, control: x.control, before: cur.v, after: x.v, target: x.v });
    }
    // switches first (a module switched on before its knobs move), then the knobs
    items.sort((p, q) => (p.kind === "state" ? 0 : 1) - (q.kind === "state" ? 0 : 1));
    if (!items.length) { toast(`${to.toUpperCase()}: the same as ${to === "b" ? "A" : "B"} so far.`); return; }
    const gone = await applyItems(items);
    rememberChange({ kind: "batch", items }, `A / B: to ${to.toUpperCase()}`);
    toast(`${to.toUpperCase()}: ${items.length - gone} change${items.length - gone === 1 ? "" : "s"} sent.`);
  }

  // BYPASS: this track's plug-ins off (each one's own bypass) and back on. Honey Tune has none.
  async function toggleBypass(restoreOnly) {
    const t = S.data && trackOf(S.data, S.track);
    if (!t) return;
    const was = S.bypass[t.key];
    if (was) {
      delete S.bypass[t.key];
      const items = was.map((x) => ({ kind: "power", unit: x.unit, gen: x.gen, before: 0, after: 1, target: 1 }));
      await applyItems(items);
      if (!restoreOnly) rememberChange({ kind: "batch", items }, "BYPASS off");
    } else {
      const units = t.units.filter((u) => u.product !== "honeytune" && canTurn(u) && u.on !== false);
      if (!units.length) { toast("Nothing on this track to bypass from the Rack."); return; }
      S.bypass[t.key] = units.map((u) => ({ unit: u.id, gen: u.gen }));
      const items = units.map((u) => ({ kind: "power", unit: u.id, gen: u.gen, before: 1, after: 0, target: 0 }));
      await applyItems(items);
      rememberChange({ kind: "batch", items }, "BYPASS on");
    }
    S.html.header = "";
    if (S.root) render();
  }

  function onDown() { S.down = true; }
  function onUp() {
    S.down = false;
    if (S.pending) { S.pending = false; setTimeout(() => { if (S.root) render(); }, 0); }   // after the click's own render
  }

  function onInput(e) {
    if (e.target.dataset.sr === "search") { S.search = e.target.value; S.card = null; render(); }
  }

  function onKey(e) {
    if (e.key === "Escape") { if (S.menu) { S.menu = false; render(); } else { close(); if (S.opts && S.opts.back) S.opts.back(); } }
  }

  // The stage is drawn at 960 px high, 1280 - 1708 px wide (4:3 to 16:9), and scaled to the window.
  function fit() {
    if (!S.root) return;
    const w = window.innerWidth, h = window.innerHeight;
    const H = 960;
    const W = clamp(Math.round((H * w) / h), 1280, 1708);
    const s = Math.min(w / W, h / H);
    S.scale = s;
    if (W !== S.W) { S.W = W; if (S.data) render(); }
    S.stage.style.width = W + "px";
    S.stage.style.height = H + "px";
    S.stage.style.transform = `translate(${((w - W * s) / 2).toFixed(1)}px, ${((h - H * s) / 2).toFixed(1)}px) scale(${s.toFixed(5)})`;
  }

  function open(opts) {
    if (S.root) close();
    S.opts = Object.assign({ host: "mixology", getRack: async () => null, back: null, art: "rack/" }, opts || {});
    ART = S.opts.art;
    S.data = null; S.sig = ""; S.hist = {}; S.order = {}; S.menu = false; S.noFile = false;
    if (opts && opts.page) S.page = opts.page;
    if (opts && opts.track) S.track = opts.track;
    S.root = document.createElement("div");
    S.root.className = "sr-root";
    setTheme(S.opts.theme === "light" || S.opts.theme === "dark" ? S.opts.theme : savedTheme() || "dark");
    S.stage = document.createElement("div");
    S.stage.className = "sr-stage";
    S.parts = {};
    S.html = {};
    S.detail = {};
    for (const k of ["frame", "header", "page", "footer", "menu"]) {
      const d = document.createElement("div");
      d.className = "sr-part";
      d.style.display = "contents";   // the parts sit in the stage's grid as if they were the stage's own children
      S.stage.appendChild(d);
      S.parts[k] = d;
    }
    S.root.appendChild(S.stage);
    document.body.appendChild(S.root);
    S.root.addEventListener("click", onClick);
    S.root.addEventListener("pointerdown", onKnobDown);
    S.root.addEventListener("pointerdown", onRowDown);
    S.root.addEventListener("pointerdown", onDown);
    S.root.addEventListener("dblclick", onDblClick);
    S.root.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("pointermove", onKnobMove, true);
    window.addEventListener("pointermove", onRowMove, true);
    window.addEventListener("pointerup", onRowUp, true);
    window.addEventListener("pointercancel", onRowUp, true);
    window.addEventListener("pointerup", onKnobUp, true);
    window.addEventListener("pointercancel", onKnobUp, true);
    window.addEventListener("pointerup", onUp, true);
    window.addEventListener("pointercancel", onUp, true);
    S.root.addEventListener("input", onInput);
    window.addEventListener("keydown", onKey);
    window.addEventListener("resize", fit);
    fit();
    poll();
  }

  function close() {
    if (!S.root) return;
    // BYPASS is for listening in the Rack: leaving brings every bypassed plug-in back.
    for (const key of Object.keys(S.bypass)) {
      for (const x of S.bypass[key]) {
        const u = unitById(x.unit);
        if (u && u.gen === x.gen) send(u, { change: "power", module: "power", control: "", value: 1, gesture: "set" });
      }
      delete S.bypass[key];
    }
    clearTimeout(S.timer);
    window.removeEventListener("keydown", onKey);
    window.removeEventListener("resize", fit);
    window.removeEventListener("pointerup", onUp, true);
    window.removeEventListener("pointercancel", onUp, true);
    window.removeEventListener("pointermove", onKnobMove, true);
    window.removeEventListener("pointermove", onRowMove, true);
    window.removeEventListener("pointerup", onRowUp, true);
    window.removeEventListener("pointercancel", onRowUp, true);
    window.removeEventListener("pointerup", onKnobUp, true);
    window.removeEventListener("pointercancel", onKnobUp, true);
    if (S.drag) onKnobUp();
    if (S.wheel) endWheel();
    hideToast();
    S.down = false; S.pending = false; S.held = {}; S.sent = {}; S.rowDrag = null; S.rowDragged = false;
    S.root.remove();
    S.root = null; S.stage = null; S.data = null;
  }

  // For the preview and tests: pick a page / track / module without clicking.
  function show(o) {
    if (o.page) S.page = o.page;
    if (o.track) { S.track = o.track; S.sel = null; }
    if (o.unit && o.key) S.sel = { unit: o.unit, key: o.key };
    if (o.card) S.card = o.card;
    if (o.filter) S.filter = o.filter;
    if (o.menu != null) S.menu = o.menu;
    if (o.theme) setTheme(o.theme);
    render();
  }

  // getRack's reply: the Rack's JSON as base64 (see RackHost.h viewBase64), or the JSON itself (an older plug-in).
  function fromBase64Json(s) {
    if (s == null || s === "") return null;
    if (typeof s === "object") return s;
    s = String(s).trim();
    if (s[0] === "{" || s[0] === "[") return JSON.parse(s);
    const bin = atob(s);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return JSON.parse(new TextDecoder("utf-8").decode(bytes));
  }

  window.SonicologyRack = { open, close, show, fromBase64Json, isOpen: () => !!S.root, theme: () => S.theme, catalog: CATALOG, products: PRODUCTS };
})();
