// Sonicology Rack: one view of every Sonicology plug-in in the project (Voxology, Honey Tune, Mixology, each Mixology
// Link, Masterology): which modules run on each track, what they are set to, and how the plug-ins work together.
//
// THIS FILE, rack.css AND plugin/ui/rack/ ARE THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology).
// Change them in Mixology (the master copy), copy them to the other two.
//
// The Rack only shows; the plug-ins do the processing (stage 1 of docs/rack/RACK-PLAN.md in Mixology's repo).
// A plug-in opens it over its own page:
//   SonicologyRack.open({ host: "voxology" | "mixology" | "masterology", getRack: (track) => Promise<data>, back: () => {},
//                         art: "rack/" (where the rack_*.webp files are, from the page),
//                         theme: "dark" | "light" (the plug-in's saved one), onTheme: (theme) => {} (save it) })
// Clicking the cube switches between the dark and the light view.
// getRack() returns what every plug-in publishes into the shared block (RackLink), as JSON:
//   { host, hostHash, units: [ { id, product, version, track, hash, role, on, inDb, outDb, modules: [ { key, state,
//     summary, by, meterKind, meter, values: [ { name, text, norm, bi } ], bands: [ { type, hz, db, q } ], layers } ],
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
  const HOSTS = { voxology: "Voxology", mixology: "Mixology", masterology: "Masterology" };
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
      "Cleans the recording first: steady noise and room echo turned down, rumble cut, pops, mouth clicks and breaths tamed, the gaps quieted.",
      ["Home recordings with hiss, fan or AC noise", "A roomy vocal that should sound closer", "Pops, clicks and loud breaths"],
      ["Noise", "Room", "Low Cut", "Pops", "Clicks", "Breaths"]),
    "vox.eq": M("voxology", "Tone EQ", "TONE EQ", ["vocal", "mixing"], "sliders", "eq", "curve",
      "Shapes the vocal's tone in five bands: body, mud, nasal, presence and air.",
      ["Clearing mud (200 - 500 Hz)", "Presence so the words cut through", "Air on top for a modern sheen"],
      ["Body", "Mud", "Nasal", "Presence", "Air"]),
    "vox.dyneq": M("voxology", "Dynamic EQ", "DYN EQ", ["vocal", "dynamics"], "dyneq", "eq", "dip",
      "Cuts boom, mud, honk and harshness only when they jump out; Smooth turns down narrow ringing resonances while they ring.",
      ["Harsh notes that only bite when you push", "Boomy low notes", "Whistly or nasal resonances (Smooth)"],
      ["Max Cut", "Frequency", "Sensitivity", "Smooth"]),
    "vox.deess": M("voxology", "De-Esser", "DE-ESS", ["vocal", "dynamics"], "s", "gr", "sss",
      "Tames sharp s, t, sh and ch sounds without dulling the voice.",
      ["Bright vocals with piercing s sounds", "After a presence boost", "Before reverb and delay, so they don't splash"],
      ["Amount", "Sensitivity", "Frequency"]),
    "vox.rider": M("voxology", "Rider", "RIDER", ["vocal", "dynamics"], "bars", "gr", "bars",
      "Rides the level word by word, like a hand on the fader, so quiet words don't get lost.",
      ["Words that drop under the beat", "Verses with loud and soft lines", "Less work for the compressor"],
      ["Target", "Range", "Speed"]),
    "vox.comp": M("voxology", "Compressor", "COMP", ["vocal", "dynamics"], "gauge", "gr", "gauge",
      "Evens the performance: Peak catches the spikes, the leveler holds the rest steady, Mix blends it in parallel.",
      ["A steady, upfront vocal", "Taming shouted lines", "Parallel punch with Mix"],
      ["Peak", "Threshold", "Ratio", "Makeup", "Mix"]),
    "vox.sat": M("voxology", "Saturation", "SAT", ["vocal", "creative"], "tube", "level", "tube",
      "Adds harmonics for warmth, density and presence: Tape, Tube or Clip.",
      ["A thin vocal that needs body", "Cutting through a dense beat", "Grit as an effect"],
      ["Mode", "Drive", "Mix"]),
    "vox.voices": M("voxology", "Voices", "VOICES", ["vocal", "creative"], "voices", "level", "stack",
      "Doubles, extra voices and harmonies in key, up to 8 voices, or the chords you play in over MIDI.",
      ["A wider, bigger hook", "Harmonies without singing them", "Stacks for a choir feel"],
      ["Double", "Width", "Voice 1", "Voice 2", "Stack", "Level"]),
    "vox.dimension": M("voxology", "Dimension", "DIMENSION", ["vocal", "creative"], "rings3", "level", "swirl",
      "Movement and width on the finished vocal: chorus, flanger or phaser.",
      ["A lush, wide hook", "Movement on long notes", "Effect sections"],
      ["Mode", "Amount", "Speed", "Width"]),
    "vox.delay": M("voxology", "Delay", "DELAY", ["vocal", "space"], "clock", "level", "echo",
      "Echoes locked to the song's tempo; they duck while you sing, and Throw puts an echo on each line's last word.",
      ["Depth without washing out the words", "Echo throws at the end of lines", "Ping-pong width"],
      ["Time", "Feedback", "Mix", "Tone", "Duck", "Throw"]),
    "vox.reverb": M("voxology", "Reverb", "REVERB", ["vocal", "space"], "arcs", "level", "arcs",
      "A vocal plate: a space for the voice that blooms in the gaps; Throw adds a wash after each line.",
      ["Placing the vocal in a polished space", "A wash after each line (Throw)", "Clear words with pre-delay"],
      ["Decay", "Pre-delay", "Mix", "Tone", "Duck", "Throw"]),
    "vox.output": M("voxology", "Output", "OUTPUT", ["vocal", "utility"], "out", "level", "meter",
      "The vocal's final level into your mix.",
      ["Matching the level to before Voxology", "Gain staging into the mixer"],
      ["Output"]),
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
  function knob(v, size) {
    const norm = clamp(v.norm == null ? 0.5 : v.norm, 0, 1);
    const ang = -135 + norm * 270;
    const r = 41, c = 50;
    const arc = (a0, a1) => {
      const p = (a) => [c + r * Math.sin((a * Math.PI) / 180), c - r * Math.cos((a * Math.PI) / 180)];
      const [x0, y0] = p(a0), [x1, y1] = p(a1);
      return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
    };
    const from = v.bi ? 0 : -135;
    const lit = Math.abs(ang - from) < 0.5 ? "" : `<path d="${arc(Math.min(from, ang), Math.max(from, ang))}" class="sr-karc ${v.bi && ang < 0 ? "sr-karc-neg" : ""}"/>`;
    return `<div class="sr-knob" style="--sz:${size}px">
      <img class="sr-kring" src="${ART}rack_knob_ring.webp" alt="" draggable="false">
      <svg class="sr-kval" viewBox="0 0 100 100"><path d="${arc(-135, 135)}" class="sr-kdim"/>${lit}</svg>
      <img class="sr-kcap" src="${ART}rack_knob_cap.webp" alt="" draggable="false" style="transform:rotate(${ang.toFixed(1)}deg)">
    </div>`;
  }

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
    return `<header class="sr-head">
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
        </div>
        <div class="sr-sep"></div>
        <div class="sr-suites">${chips}${miss}</div>
        <button class="sr-back" data-sr="back">${icon("back")}BACK TO ${esc((HOSTS[S.opts.host] || "PLUG-IN").toUpperCase())}</button>
      </div>
    </header>`;
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
  function rackPage(data) {
    const t = trackOf(data, S.track);
    if (!t) return `<main class="sr-main sr-empty"><div class="sr-panel sr-wide"><h3>No Sonicology plug-ins are playing</h3><p>Press play in your DAW: plug-ins take part while the song plays, and only in the project that plays.</p></div></main>`;
    const sel = findSel(t);
    const chain = chainOf(t);

    // left: SIGNAL CHAIN, grouped by plug-in, modules that are off folded away
    let rows = "";
    for (const u of t.units) {
      const P = PRODUCTS[u.product];
      const mods = u.modules || [];
      const shown = mods.filter((m) => m.state !== "off" || S.unfold[u.id]);
      const off = mods.length - mods.filter((m) => m.state !== "off").length;
      rows += `<div class="sr-group sr-tone-${P.tone}"><b class="sr-badge sr-tone-${P.tone}">${P.badge}</b><span>${esc(P.name.toUpperCase())}</span>${u.on === false ? '<em class="sr-bypassed">BYPASSED</em>' : ""}<small>v${esc(u.version || "")}</small></div>`;
      for (const m of shown) {
        const c = cat(m.key);
        const picked = sel && sel.unit.id === u.id && sel.mod.key === m.key;
        rows += `<button class="sr-row ${picked ? "sr-picked" : ""} sr-st-${m.state}" data-sr="mod" data-unit="${esc(u.id)}" data-key="${esc(m.key)}">
          ${icon(c.icon)}<span class="sr-rname">${esc(c.name)}</span>${pill(m.state)}</button>`;
      }
      if (!mods.length) rows += `<div class="sr-row sr-row-note">Nothing to show yet</div>`;
      if (off > 0) rows += `<button class="sr-fold" data-sr="unfold" data-unit="${esc(u.id)}">${S.unfold[u.id] ? "hide the " + off + " off" : "+ " + off + " off"}</button>`;
    }

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
      const knobs = vals.slice(0, nk).map((v) => `<div class="sr-kcell"><b>${esc(v.name.toUpperCase())}</b>${knob(v, ks)}<span class="sr-kval-t">${esc(v.text)}</span></div>`).join("");
      const more = vals.slice(nk).map((v) => `<span class="sr-chip"><b>${esc(v.name)}</b>${esc(v.text)}</span>`).join("");
      const meterNow = m.meterKind === "gr" ? `<div class="sr-big"><b data-val="meter">${num(m.meter || 0)}</b><small>dB gain reduction</small></div>` : "";
      detail = `<section class="sr-panel sr-mod">
        <div class="sr-mod-head">
          <div class="sr-mod-ic sr-tone-${PRODUCTS[sel.unit.product].tone}">${icon(c.icon)}</div>
          <div class="sr-mod-t"><h2>${esc(c.name.toUpperCase())}</h2><p>${where}</p></div>
          <div class="sr-mod-sum"><span>${esc(m.summary || "")}</span>${m.by ? `<small>set by ${esc(m.by)}</small>` : ""}</div>
          ${pill(m.state)}
        </div>
        <div class="sr-graphwrap">${graph}${meterNow}</div>
        ${c.graph === "eq" ? bandChips(m) : ""}
        <div class="sr-knobs ${vals.length ? "" : "sr-knobs-none"}">
          ${vals.length ? `<div class="sr-krow">${knobs}</div>` : `<p class="sr-nokn">${m.state === "off" ? "Off: it passes the sound through untouched." : "No settings to show."}</p>`}
          ${more ? `<div class="sr-more">${more}</div>` : ""}
          <div class="sr-readonly">Shows ${esc(PRODUCTS[sel.unit.product].name)}'s settings live · change them in ${esc(PRODUCTS[sel.unit.product].name)}${sel.unit.product === "link" ? " (or with Mixology's tools)" : ""}</div>
        </div>
      </section>`;
    }

    // right: RACK COACH (how they work together, what's playing, about this module)
    const lines = [];
    for (const l of data.together || []) if (!l.hash || l.hash === t.hash || (t.role === "master" && l.hash === "master") || (t.role === "fullmix" && l.hash === "fullmix")) lines.push(l);
    for (const u of t.units) for (const l of u.together || []) lines.push(l);
    lines.sort((a, b) => (a.level === "warn" ? 0 : a.level === "info" ? 2 : 1) - (b.level === "warn" ? 0 : b.level === "info" ? 2 : 1));
    const together = lines.length ? lines.map((l) => `<li class="sr-tl sr-tl-${l.level || "ok"}">${lineIcon(l.level)}<span>${esc(l.text)}</span></li>`).join("")
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
        <div class="sr-rows" data-scroll="rows">${rows}</div>
        <div class="sr-honey"></div>
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
          ${overview ? `<h4>CHAIN OVERVIEW</h4><ul class="sr-overview">${overview}</ul>` : ""}
          ${about}
        </div>
      </section>
    </main>`;
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
      <h4>WHERE IT PLAYS</h4>${whereHtml}
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
      page: S.page === "rack" ? rackPage(data) : modulesPage(data),
      footer: footer(data, sel ? sel.unit : t ? t.units[0] : null),
      menu: S.menu ? trackMenu(data) : "",
    };
    for (const k of Object.keys(html)) if (html[k] !== S.html[k]) { S.parts[k].innerHTML = html[k]; S.html[k] = html[k]; }
    S.eqKey = "";
    S.stage.querySelectorAll("[data-scroll]").forEach((el) => { const s = scrolls[el.dataset.scroll]; if (s) { el.scrollTop = s[0]; el.scrollLeft = s[1]; } });
    if (focusSearch) { const i = S.stage.querySelector('[data-sr="search"]'); if (i) { i.focus(); i.setSelectionRange(caret, caret); } }
  }

  // What changes the page's layout: the picked track's modules in detail (the MODULES page: every unit's), the other
  // tracks by their modules' keys and states. Not the live values (meters, a dynamic EQ's moving cuts): updateLive.
  function signature(data) {
    const t = trackOf(data, S.track);
    const here = new Set(t ? t.units.map((u) => u.id) : []);
    const all = S.page !== "rack";
    return JSON.stringify((data.units || []).map((u) => (all || here.has(u.id)
      ? [u.id, u.track, u.role, u.product, u.on, u.version, (u.modules || []).map((m) => [m.key, m.state, m.summary, m.by, (m.values || []).map((v) => v.text),
          (m.bands || []).map((b) => [b.type, Math.round(b.hz || 0), b.name, b.on]), m.layers]), u.together]
      : [u.id, u.track, u.role, u.product, u.on, u.version, (u.modules || []).map((m) => m.key + ":" + m.state)])))
      + JSON.stringify(data.together) + JSON.stringify(data.mix && data.mix.source);
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
    const sel = S.page === "rack" ? findSel(t) : null;
    const unit = sel ? sel.unit : t ? t.units[0] : null;
    const set = (sel2, text) => { const el = S.stage.querySelector(`[data-val="${sel2}"]`); if (el && el.textContent !== text) el.textContent = text; };
    const meter = (id, db) => {
      const el = S.stage.querySelector(`[data-meter="${id}"] i`);
      const w = (clamp((db + 60) / 60, 0, 1) * 100).toFixed(1) + "%";
      if (el && el.style.width !== w) el.style.width = w;
    };
    if (unit) { meter("in", unit.inDb ?? -100); meter("out", unit.outDb ?? -100); set("in", dbText(unit.inDb, "dB")); set("out", dbText(unit.outDb, "dB")); }
    const mix = data.mix || {};
    set("lufsI", dbText(mix.lufsI, "LUFS")); set("lufsS", dbText(mix.lufsS, "LUFS")); set("tp", dbText(mix.tp, "dBTP"));
    if (sel) {
      const c = cat(sel.mod.key);
      if (sel.mod.meterKind === "gr") set("meter", num(sel.mod.meter || 0));
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

  async function poll() {
    if (!S.root) return;
    let data = null;
    try { data = await S.opts.getRack(S.track || ""); } catch (e) { data = null; }
    if (!S.root) return;
    if (data) {
      keepDetails(data);
      remember(data);
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
    S.timer = setTimeout(poll, 150);
  }

  function onClick(e) {
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
    else if (a === "mod") { S.sel = { unit: el.dataset.unit, key: el.dataset.key }; }
    else if (a === "unfold") { S.unfold[el.dataset.unit] = !S.unfold[el.dataset.unit]; }
    else if (a === "filter") { S.filter = el.dataset.filter; S.card = null; }
    else if (a === "card") { S.card = el.dataset.key; }
    else if (a === "goto") { S.track = el.dataset.track; S.sel = { unit: el.dataset.unit, key: el.dataset.key }; S.page = "rack"; }
    else if (a === "clear") { S.search = ""; S.card = null; }
    else return;
    render();
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
    if (W !== S.W) { S.W = W; if (S.data) render(); }
    S.stage.style.width = W + "px";
    S.stage.style.height = H + "px";
    S.stage.style.transform = `translate(${((w - W * s) / 2).toFixed(1)}px, ${((h - H * s) / 2).toFixed(1)}px) scale(${s.toFixed(5)})`;
  }

  function open(opts) {
    if (S.root) close();
    S.opts = Object.assign({ host: "mixology", getRack: async () => null, back: null, art: "rack/" }, opts || {});
    ART = S.opts.art;
    S.data = null; S.sig = ""; S.hist = {}; S.menu = false;
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
    S.root.addEventListener("pointerdown", onDown);
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
    clearTimeout(S.timer);
    window.removeEventListener("keydown", onKey);
    window.removeEventListener("resize", fit);
    window.removeEventListener("pointerup", onUp, true);
    window.removeEventListener("pointercancel", onUp, true);
    S.down = false; S.pending = false;
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

  window.SonicologyRack = { open, close, show, isOpen: () => !!S.root, theme: () => S.theme, catalog: CATALOG, products: PRODUCTS };
})();
