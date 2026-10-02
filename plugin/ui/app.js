// Polisher liquid-glass UI.
// Talks to the plug-in through JUCE's web relays (parameters) and the "polisherMeters" event.

import * as Juce from "./juce/index.js";

const $ = (id) => document.getElementById(id);
const MINUS = "−";
const fmtNum = (v, digits = 1) => (v < 0 ? MINUS : "") + Math.abs(v).toFixed(digits);
const fmtDb = (v, unit, digits = 1) => (v <= -99 ? `${MINUS}inf ${unit}` : `${fmtNum(v, digits)} ${unit}`);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// ---------------------------------------------------------------------------------------------
// Scale the fixed 1600x900 design to the window.
const stage = $("stage");
function fit() {
  const s = Math.min(window.innerWidth / 1600, window.innerHeight / 900);
  stage.style.transform = `scale(${s})`;
}
window.addEventListener("resize", fit);
fit();

// ---------------------------------------------------------------------------------------------
// Parameters (live in this build: the Maximizer).
const P = {
  drive: Juce.getSliderState("drive"),
  ceiling: Juce.getSliderState("ceiling"),
  release: Juce.getSliderState("release"),
  adaptive: Juce.getToggleState("adaptive"),
  bypass: Juce.getToggleState("bypass"),
  autoreset: Juce.getToggleState("autoreset"),
  resetOnChange: Juce.getToggleState("resetOnChange"),
  levelMatch: Juce.getToggleState("levelMatch"),
  clipOn: Juce.getToggleState("clipOn"),
  clipLevel: Juce.getSliderState("clipLevel"),
  clipSoftness: Juce.getSliderState("clipSoftness"),
  clipOs: Juce.getComboBoxState("clipOversampling"),
  eqOn: Juce.getToggleState("eqOn"),
  bassOn: Juce.getToggleState("bassOn"),
  imgOn: Juce.getToggleState("imgOn"),
  imgLow: Juce.getSliderState("imgLow"),
  imgMid: Juce.getSliderState("imgMid"),
  imgHigh: Juce.getSliderState("imgHigh"),
  bassMono: Juce.getSliderState("bassMono"),
  bassTame: Juce.getSliderState("bassTame"),
  bassPunch: Juce.getSliderState("bassPunch"),
  amGenre: Juce.getComboBoxState("amGenre"),
  amIntensity: Juce.getComboBoxState("amIntensity"),
  amTarget: Juce.getComboBoxState("amTarget"),
  eqGain: [1, 2, 3, 4, 5, 6, 7].map((b) => Juce.getSliderState("eqGain" + b)),
  eqFreq: [1, 2, 3, 4, 5, 6, 7].map((b) => Juce.getSliderState("eqFreq" + b)),
  dynOn: Juce.getToggleState("dynOn"),
  dynAdaptive: Juce.getToggleState("dynAdaptive"),
  dynX: [1, 2, 3].map((x) => Juce.getSliderState("dynX" + x)),
  dynThr: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynThr" + b)),
  dynRatio: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynRatio" + b)),
  dynAtk: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynAtk" + b)),
  dynRel: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynRel" + b)),
  dynGain: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynGain" + b)),
  satOn: Juce.getToggleState("satOn"),
  satMode: Juce.getComboBoxState("satMode"),
  satDrive: Juce.getSliderState("satDrive"),
  satMix: Juce.getSliderState("satMix"),
  ulOn: Juce.getToggleState("ulOn"),
  ulAmount: Juce.getSliderState("ulAmount"),
  ulLength: Juce.getSliderState("ulLength"),
  ulFocus: Juce.getComboBoxState("ulFocus"),
  stemOn: Juce.getToggleState("stemOn"),
  tsOn: Juce.getToggleState("tsOn"),
  tsAmount: Juce.getSliderState("tsAmount"),
  tsRange: Juce.getSliderState("tsRange"),
  tsSpeed: Juce.getComboBoxState("tsSpeed"),
  rtOn: Juce.getToggleState("rtOn"),
  rtAmount: Juce.getSliderState("rtAmount"),
  rtSens: Juce.getSliderState("rtSens"),
  rtRelease: Juce.getSliderState("rtRelease"),
  rtFocus: Juce.getComboBoxState("rtFocus"),
  stemGain: ["stemVoc", "stemDrums", "stemBass", "stemOther"].map((id) => Juce.getSliderState(id)),
};
const resetMeter = Juce.getNativeFunction("resetMeter");
const amStart = Juce.getNativeFunction("startAutoMaster");
const amCancel = Juce.getNativeFunction("cancelAutoMaster");
const amUndo = Juce.getNativeFunction("undoAutoMaster");
const amGetReport = Juce.getNativeFunction("getAutoMasterReport");

function scaledToNorm(state, v) {
  const { start, end, skew } = state.properties;
  return Math.pow(clamp((v - start) / (end - start), 0, 1), skew);
}

// ---------------------------------------------------------------------------------------------
// Latest meter frame from the plug-in.
const M = {
  momentary: -100, shortTerm: -100, integrated: -100, range: 0, truePeak: -100,
  peakRecent: -100, peakL: -100, peakR: -100, gr: 0, clip: 0, mix: null, master: null,
  inShort: -100, inIntegrated: -100, sr: 48000, tame: 0, punch: 0, bassWidth: 0,
  amState: 0, amProgress: 0, amHearing: false, amUndo: false, amReport: 0, matchDb: 0, corr: 1,
  dynGr: [0, 0, 0, 0], satHarm: -100, stemShare: [0, 0, 0, 0], rtCut: 0, rtHz: 0, tsDb: 0, tsHz: 0,
  ulBoost: 0, ulSquash: 0,
};
let grWindow = [];           // ~3 s of gain-reduction samples for Learn
let clipWindow = [];         // ~3 s of clipper peak-reduction samples
let tameWindow = [];         // ~3 s of Sub Tame cuts
let punchWindow = [];        // ~3 s of Punch boosts
let lastAudioTime = 0;

// ---------------------------------------------------------------------------------------------
// Module catalogue. Only the Maximizer processes audio in this build.
const modules = [
  { id: "ul", name: "Unlimiter", live: true, soon: "",
    what: "Puts back the punch a limiter or clipper squashed out of a beat, loop or mix, so the hits stand out again before mastering.",
    cells: [], controls: [] },
  { id: "eq", name: "Tonal EQ", live: true, soon: "",
    what: "Shapes the overall tone in seven musical bands, from the sub up to the air, and nudges the mix toward your genre's reference balance.",
    cells: [["Low End", "30 Hz", "0.0 dB", .5], ["Bass", "90 Hz", "0.0 dB", .5], ["Low Mid", "350 Hz", "0.0 dB", .5], ["Mid", "1 kHz", "0.0 dB", .5],
            ["Upper Mid", "2.5 kHz", "0.0 dB", .5], ["Presence", "5 kHz", "0.0 dB", .5], ["Air", "12 kHz", "0.0 dB", .5]],
    controls: [["Low End / Bass", "Weight of the kick and 808. Small moves here change how big the record feels."],
               ["Low Mid", "Where mud and boxiness build up. Cutting here often reads as clearer and louder."],
               ["Upper Mid / Presence", "Vocal consonants, snare crack and edge. A little goes a long way."],
               ["Air", "Sheen on top. Shelves rather than peaks, so hats get shine instead of bite."]] },
  { id: "stem", name: "Stem EQ", live: true, soon: "",
    what: "Turns the vocals, drums, bass or everything else up or down inside your finished stereo mix, like reaching back into the session and moving one fader.",
    cells: [],
    controls: [["Vocals / Drums / Bass / Other", "Turns that part of the mix up or down (±12 dB). Other is everything else: synths, keys, guitars, pads, effects."],
               ["How it works", "A small AI model listens to the mix and works out, moment by moment and frequency by frequency, how much of the sound is vocals, drums, bass or other. Your four levels are mixed by those shares into one EQ that updates about 180 times a second."],
               ["Trade-offs", "Adds 40 ms of delay (Cubase compensates it). Where two parts share the same notes, a move nudges both a little; Other moves touch the drums slightly. Moves up to about ±6 dB stay clean."]] },
  { id: "ts", name: "Tone Steady", live: true, soon: "",
    what: "Keeps your tone steady as the song plays: when a section drifts muddier, harsher or thinner than the rest of the song, it nudges those frequencies back.",
    cells: [], controls: [] },
  { id: "rt", name: "Resonance Tamer", live: true, soon: "",
    what: "Finds ringing tones that stick out (boxiness, whistly vocal tones, harsh hats) and cuts just those, only while they ring.",
    cells: [], controls: [] },
  { id: "bass", name: "Bass Control", live: true, soon: "",
    what: "Keeps the low end solid on every system: mono below a crossover, controls 808 level jumps, and adds kick punch.",
    cells: [],
    controls: [["Mono Below", "Stereo sub wastes headroom and collapses on club systems and phones."],
               ["Sub Tame", "Evens out 808 slides so the limiter doesn't grab the whole mix on long notes."],
               ["Punch", "Adds attack to the start of the kick without turning the sub up."]] },
  { id: "dyn", name: "Dynamics", live: true, soon: "",
    what: "Four-band compression: steadies the low end without ducking the vocal, and evens out section jumps gently.",
    cells: [["Crossovers", "3 splits", "120/1k/6k", .5], ["Low Ratio", "low band", "1:1", 0], ["Mid Attack", "mid band", "30 ms", .45], ["Release", "all bands", "Auto", .5]],
    controls: [["Crossovers", "Split points between the bands."], ["Ratio", "How firmly each band is held down."],
               ["Attack", "Slow lets drum hits through, fast catches vocal peaks."], ["Release", "Auto follows the tempo so it recovers between hits."]] },
  { id: "img", name: "Stereo Imager", live: true, soon: "",
    what: "Controls width per band: keeps the low end centred and opens up hats, reverbs and ad-libs.",
    cells: [["Lows", "< 250 Hz", "0%", .5], ["Mids", "250 Hz–5 kHz", "0%", .5], ["Highs", "> 5 kHz", "0%", .5]],
    controls: [["Lows", "Kept centred. Wide bass smears and costs headroom."], ["Mids", "Small moves only, so the lead vocal stays locked in."],
               ["Highs", "Where width sounds best: hats, reverbs, ad-libs."]] },
  { id: "sat", name: "Saturation", live: true, soon: "",
    what: "Adds harmonics (tape, tube or clip styles) so bass translates on small speakers and the master feels denser.",
    cells: [["Mode", "character", "Tape", .5], ["Drive", "input", "0.0 dB", 0], ["Mix", "wet / dry", "0%", 0]],
    controls: [["Mode", "Tape = soft and warm. Tube = richer. Clip = aggressive."], ["Drive", "How many harmonics are generated."],
               ["Mix", "Blend keeps transients clean while harmonics fill in underneath."]] },
  { id: "clip", name: "Soft Clipper", live: true, soon: "",
    what: "Shaves the tallest drum peaks before the limiter so the limiter works less. This is how loud modern masters stay punchy.",
    cells: [["Threshold", "dBFS", "0.0 dB", 1], ["Softness", "knee", "70%", .7], ["Oversample", "quality", "8×", .8]],
    controls: [["Threshold", "Where clipping starts. Lower = more peaks shaved."], ["Softness", "Rounded knee: heard as punch, not distortion."],
               ["Oversampling", "Stops harsh aliasing that clipping otherwise adds."]] },
  { id: "max", name: "Maximizer", live: true },
];
let selected = "max";
let learnView = "module";      // "module" or "report" (the Auto-Master report)
let amReport = null;           // last Auto-Master report (parsed), or null

// ---------------------------------------------------------------------------------------------
// Tonal EQ bands. Shapes, widths and ranges match dsp/include/polisher/TonalEQ.h exactly, so the
// curve drawn here is the plug-in's real response.
const EQ_BANDS = [
  { name: "Low End", shape: "low", def: 45, q: 0.707,
    boost: "More sub weight: the 808 and kick tail feel bigger on big speakers. Phones and laptops barely reproduce it.",
    cut: "Less sub rumble: a cleaner bottom and more headroom, so the Maximizer can make it louder." },
  { name: "Bass", shape: "bell", def: 100, q: 0.8,
    boost: "More body in the kick and 808: fuller and warmer. This is the low end most speakers actually play.",
    cut: "A tighter low end: less boom, so the kick punches through." },
  { name: "Low Mid", shape: "bell", def: 350, q: 0.9,
    boost: "More warmth and thickness, but a little too much turns muddy and boxy.",
    cut: "Clears mud and boxiness. The mix often sounds cleaner, and louder at the same level." },
  { name: "Mid", shape: "bell", def: 1000, q: 0.8,
    boost: "Pushes vocals, snares and melodies forward.",
    cut: "Scoops the middle for a more hyped sound, but the vocal can sink." },
  { name: "Upper Mid", shape: "bell", def: 2500, q: 0.9,
    boost: "More vocal clarity and snare crack. Too much sounds harsh on earbuds.",
    cut: "Smooths harshness and tames bright synths." },
  { name: "Presence", shape: "bell", def: 5000, q: 0.9,
    boost: "More edge and definition on vocals and hi-hats.",
    cut: "Softens sibilance ('s' sounds) and bite." },
  { name: "Air", shape: "high", def: 12000, q: 0.707,
    boost: "Sheen on top: hi-hats and vocal breath open up.",
    cut: "A darker, smoother top; tames fizzy hi-hats." },
];
const EQ_MAX = 9, EQ_VIEW = 12;   // knob range, curve display range (dB)
const fmtHz = (f) => (f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 1 : 2).replace(/\.?0+$/, "")} kHz` : `${Math.round(f)} Hz`);
const eqGainAt = (b) => P.eqGain[b].getScaledValue();
const eqFreqAt = (b) => P.eqFreq[b].getScaledValue();
const eqEnabled = () => !!P.eqOn.getValue();
const eqIsFlat = () => EQ_BANDS.every((_, b) => Math.abs(eqGainAt(b)) < 0.005);

// RBJ cookbook biquad for one band -> magnitude (dB) at frequency f.
function eqBandDb(b, gain, freq, f, sr) {
  const band = EQ_BANDS[b];
  const f0 = Math.min(freq, 0.45 * sr);
  const w0 = (2 * Math.PI * f0) / sr, cw = Math.cos(w0), sw = Math.sin(w0);
  const A = Math.pow(10, gain / 40), alpha = sw / (2 * band.q);
  let b0, b1, b2, a0, a1, a2;
  if (band.shape === "bell") {
    b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
  } else {
    const k = 2 * Math.sqrt(A) * alpha, s = band.shape === "low" ? 1 : -1;
    b0 = A * ((A + 1) - s * (A - 1) * cw + k); b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cw); b2 = A * ((A + 1) - s * (A - 1) * cw - k);
    a0 = (A + 1) + s * (A - 1) * cw + k; a1 = -s * 2 * ((A - 1) + s * (A + 1) * cw); a2 = (A + 1) + s * (A - 1) * cw - k;
  }
  const w = (2 * Math.PI * f) / sr, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
  const dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}
function eqResponseDb(f, sr) {
  let total = 0;
  EQ_BANDS.forEach((_, b) => { const g = eqGainAt(b); if (Math.abs(g) > 1e-4) total += eqBandDb(b, g, eqFreqAt(b), f, sr); });
  return total;
}
let learnOn = true;

// ---------------------------------------------------------------------------------------------
// Signal chain rows (positions match the rendered panel image).
const chainRows = $("chain-rows");
function renderChain() {
  chainRows.innerHTML = "";
  modules.forEach((m, i) => {
    const row = document.createElement("div");
    row.className = "row" + (m.live ? "" : " soon");
    // The panel art has 7 slots; with more modules every row draws its own art, packed evenly.
    const packed = modules.length > 7;
    const pitch = packed ? (746 + 5 - 110.2) / modules.length : 69.55;    // rows fill the panel down to y 746
    row.style.top = (110.2 + i * pitch).toFixed(1) + "px";
    if (packed) { row.style.height = (pitch - 5).toFixed(1) + "px"; row.classList.add("packed"); }
    if (m.id === selected) row.appendChild(img("assets/row_red.png"));
    else if (packed || i >= 6) row.appendChild(img("assets/row_blue.png"));

    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "row-btn";
    btn.setAttribute("aria-pressed", String(m.id === selected));
    btn.innerHTML = `<span class="row-text"><span class="row-name">${m.name}</span>
      <span class="row-read mono" id="read-${m.id}">${m.live ? "" : "Coming soon"}</span></span>
      <span class="row-num mono">${String(i + 1).padStart(2, "0")}</span>`;
    btn.addEventListener("click", () => { selected = m.id; learnView = "module"; renderChain(); renderModule(); renderLearn(); });
    row.appendChild(btn);
    chainRows.appendChild(row);
  });
  updateLiveReadouts();
}
function img(src) { const i = document.createElement("img"); i.src = src; i.alt = ""; return i; }

// ---------------------------------------------------------------------------------------------
// Knobs
function knobMarkup(angle) {
  return `<img src="assets/knob.png" alt=""><div class="pointer" style="transform: rotate(${angle}deg)"><span></span></div>`;
}
const angleFor = (norm) => -135 + 270 * clamp(norm, 0, 1);

function liveKnobCell({ label, sub, state, fmt, def, minText, maxText }) {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub mono">${sub}</span>
    <div class="knob" tabindex="0" role="slider" aria-label="${label}">${knobMarkup(0)}</div>
    <div class="minmax mono"><span>${minText}</span><span>${maxText}</span></div>
    <span class="cell-value mono"></span>`;
  const knob = cell.querySelector(".knob");
  const pointer = cell.querySelector(".pointer");
  const value = cell.querySelector(".cell-value");

  const refresh = () => {
    const n = state.getNormalisedValue();
    pointer.style.transform = `rotate(${angleFor(n)}deg)`;
    value.textContent = fmt(state.getScaledValue());
    knob.setAttribute("aria-valuetext", value.textContent);
  };
  const setNorm = (n) => { state.setNormalisedValue(clamp(n, 0, 1)); refresh(); onParamsChanged(); userEdited(); };

  let dragStart = null;
  knob.addEventListener("pointerdown", (e) => {
    knob.setPointerCapture(e.pointerId);
    dragStart = { y: e.clientY, n: state.getNormalisedValue() };
    state.sliderDragStarted();
    e.preventDefault();
  });
  knob.addEventListener("pointermove", (e) => {
    if (!dragStart) return;
    const scale = stage.getBoundingClientRect().height / 900;
    const pixels = (dragStart.y - e.clientY) / scale;
    setNorm(dragStart.n + pixels / (e.shiftKey ? 1200 : 240));
  });
  const end = () => { if (dragStart) { dragStart = null; state.sliderDragEnded(); } };
  knob.addEventListener("pointerup", end);
  knob.addEventListener("pointercancel", end);
  knob.addEventListener("dblclick", () => { state.sliderDragStarted(); setNorm(scaledToNorm(state, def)); state.sliderDragEnded(); });
  knob.addEventListener("wheel", (e) => {
    e.preventDefault();
    state.sliderDragStarted();
    setNorm(state.getNormalisedValue() + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.002 : 0.01));
    state.sliderDragEnded();
  }, { passive: false });
  knob.addEventListener("keydown", (e) => {
    const step = e.shiftKey ? 0.002 : 0.01;
    if (e.key === "ArrowUp" || e.key === "ArrowRight") setNorm(state.getNormalisedValue() + step);
    else if (e.key === "ArrowDown" || e.key === "ArrowLeft") setNorm(state.getNormalisedValue() - step);
    else return;
    e.preventDefault();
  });

  state.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  state.propertiesChangedEvent.addListener(refresh);
  refresh();
  cell.refresh = refresh;
  return cell;
}

function staticKnobCell([label, sub, valueText, norm]) {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub mono">${sub}</span>
    <div class="knob">${knobMarkup(angleFor(norm))}</div>
    <div class="minmax mono"><span></span><span></span></div>
    <span class="cell-value mono">${valueText}</span>
    <button type="button" class="imgbtn power" tabindex="-1" aria-hidden="true"><img src="assets/btn_power.png" alt=""></button>`;
  return cell;
}

function toggleCell({ label, sub, state, onText, offText }) {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub mono">${sub}</span>
    <button type="button" class="pill cell-toggle"></button>
    <span class="cell-value mono" style="color: var(--dim); font-size: 12px; font-weight: 500">click to switch</span>`;
  const btn = cell.querySelector("button");
  const refresh = () => {
    const on = !!state.getValue();
    btn.textContent = on ? onText : offText;
    btn.className = "pill cell-toggle " + (on ? "pill-red" : "pill-blue");
    btn.setAttribute("aria-pressed", String(on));
  };
  btn.addEventListener("click", () => { state.setValue(!state.getValue()); refresh(); onParamsChanged(); userEdited(); });
  state.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  refresh();
  return cell;
}

// Build the Maximizer cells once; they stay bound to the parameters.
const maximizerCells = [
  liveKnobCell({ label: "Drive", sub: "push into limiter", state: P.drive, def: 0, minText: "0", maxText: "+24",
                 fmt: (v) => `+${v.toFixed(1)} dB` }),
  liveKnobCell({ label: "Ceiling", sub: "true peak", state: P.ceiling, def: -1, minText: `${MINUS}3`, maxText: "0",
                 fmt: (v) => `${fmtNum(v, 1)} dBTP` }),
  liveKnobCell({ label: "Release", sub: "base time", state: P.release, def: 120, minText: "10", maxText: "1000",
                 fmt: (v) => `${Math.round(v)} ms` }),
  toggleCell({ label: "Release Mode", sub: "program dependent", state: P.adaptive, onText: "ADAPTIVE", offText: "FIXED" }),
];

function oversamplingCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Oversampling</span><span class="cell-sub mono">quality / CPU</span>
    <div class="seg"></div><span class="cell-value mono os-note"></span>`;
  const seg = cell.querySelector(".seg");
  const note = cell.querySelector(".os-note");
  const labels = ["1x", "2x", "4x", "8x"];
  const buttons = labels.map((label, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = label;
    b.addEventListener("click", () => { P.clipOs.setChoiceIndex(i); refresh(); onParamsChanged(); userEdited(); });
    seg.appendChild(b);
    return b;
  });
  function refresh() {
    const idx = osIndex();
    buttons.forEach((b, i) => { b.className = "pill " + (i === idx ? "pill-red" : "pill-blue"); b.setAttribute("aria-pressed", String(i === idx)); });
    note.textContent = ["cheapest · aliasing", "light", "mastering default", "cleanest · most CPU"][idx];
  }
  P.clipOs.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  P.clipOs.propertiesChangedEvent.addListener(refresh);
  refresh();
  return cell;
}
function osIndex() {
  const n = P.clipOs.properties.choices.length || 4;
  return clamp(Math.round(P.clipOs.value * (n - 1)), 0, 3);
}

function clipMeterCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Clipping</span><span class="cell-sub mono">peak reduction</span>
    <div class="clipmeter screen"><div class="clipmeter-fill" id="clipmeter-fill"></div></div>
    <div class="minmax mono"><span>0</span><span>${MINUS}6 dB</span></div>
    <span class="cell-value mono" id="clipmeter-val">0.0 dB</span>`;
  return cell;
}

const clipperCells = [
  liveKnobCell({ label: "Clip Level", sub: "vs ceiling", state: P.clipLevel, def: 0, minText: `${MINUS}9`, maxText: "+3",
                 fmt: (v) => `${v > 0.049 ? "+" : ""}${fmtNum(v, 1)} dB` }),
  liveKnobCell({ label: "Softness", sub: "knee", state: P.clipSoftness, def: 50, minText: "hard", maxText: "soft",
                 fmt: (v) => `${Math.round(v)} %` }),
  oversamplingCell(),
  clipMeterCell(),
];

function eqKnobCell(b) {
  const band = EQ_BANDS[b];
  const cell = liveKnobCell({ label: band.name, sub: fmtHz(band.def), state: P.eqGain[b], def: 0,
                              minText: `${MINUS}${EQ_MAX}`, maxText: `+${EQ_MAX}`,
                              fmt: (v) => `${v > 0.049 ? "+" : ""}${fmtNum(v, 1)} dB` });
  const sub = cell.querySelector(".cell-sub");
  const refresh = () => (sub.textContent = fmtHz(eqFreqAt(b)));
  const knobRefresh = cell.refresh;
  P.eqFreq[b].valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  P.eqFreq[b].propertiesChangedEvent.addListener(refresh);
  refresh();
  cell.refresh = () => { knobRefresh(); refresh(); };   // after the spectrum point moves this band
  return cell;
}
const eqCells = EQ_BANDS.map((_, b) => eqKnobCell(b));

// Bass Control cells
const bassMonoHz = () => P.bassMono.getScaledValue();
const bassMonoOn = () => bassMonoHz() >= 30;
const fmtMono = (v) => (v < 30 ? "Off" : `${Math.round(v)} Hz`);
function barCell({ label, sub, minText, maxText, id }) {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub mono">${sub}</span>
    <div class="clipmeter screen"><div class="clipmeter-fill" id="${id}-fill"></div></div>
    <div class="minmax mono"><span>${minText}</span><span>${maxText}</span></div>
    <span class="cell-value mono" id="${id}-val"></span>`;
  return cell;
}
const bassCells = [
  liveKnobCell({ label: "Mono Below", sub: "stereo → mono", state: P.bassMono, def: 0, minText: "off", maxText: "250",
                 fmt: fmtMono }),
  liveKnobCell({ label: "Sub Tame", sub: "evens 808 jumps", state: P.bassTame, def: 0, minText: "0", maxText: "100",
                 fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Punch", sub: "kick attack", state: P.bassPunch, def: 0, minText: "0", maxText: "100",
                 fmt: (v) => `${Math.round(v)} %` }),
  barCell({ label: "Taming", sub: "808 cut", minText: "0", maxText: `${MINUS}12 dB`, id: "tamemeter" }),
  barCell({ label: "Bass Width", sub: "your mix < 150 Hz", minText: "mono", maxText: "wide", id: "widthmeter" }),
];

// Stereo Imager cells
const fmtWidth = (v) => `${Math.round(v)} %`;
function corrCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Correlation</span><span class="cell-sub mono">mono safety</span>
    <div class="clipmeter screen"><div class="clipmeter-fill" id="corr-fill"></div></div>
    <div class="minmax mono"><span>${MINUS}1</span><span>+1</span></div>
    <span class="cell-value mono" id="corr-val"></span>`;
  return cell;
}
const imagerCells = [
  liveKnobCell({ label: "Lows", sub: "< 250 Hz", state: P.imgLow, def: 100, minText: "mono", maxText: "200", fmt: fmtWidth }),
  liveKnobCell({ label: "Mids", sub: "250 Hz–5 kHz", state: P.imgMid, def: 100, minText: "mono", maxText: "200", fmt: fmtWidth }),
  liveKnobCell({ label: "Highs", sub: "> 5 kHz", state: P.imgHigh, def: 100, minText: "mono", maxText: "200", fmt: fmtWidth }),
  corrCell(),
];

// Dynamics cells: a band picker (with each band's gain reduction) and the picked band's controls.
const DYN_BANDS = [
  { name: "Low", short: "LOW", holds: "kick and 808" },
  { name: "Low Mid", short: "LO-MID", holds: "body, snare and vocal warmth" },
  { name: "High Mid", short: "HI-MID", holds: "vocal presence, snare crack and synth edges" },
  { name: "High", short: "HIGH", holds: "hi-hats, cymbals, air and sibilance" },
];
let dynBand = 0;
const fmtMs = (v) => `${v < 10 ? v.toFixed(1) : Math.round(v)} ms`;
const fmtSigned = (v) => `${v > 0.049 ? "+" : ""}${fmtNum(v, 1)} dB`;
const dynRatioAt = (b) => P.dynRatio[b].getScaledValue();
const dynGainAt = (b) => P.dynGain[b].getScaledValue();
const dynBandActive = (b) => dynRatioAt(b) > 1.005 || Math.abs(dynGainAt(b)) >= 0.05;
const dynIsNeutral = () => ![0, 1, 2, 3].some(dynBandActive);
function dynRangeText(b) {
  const x = P.dynX.map((s) => s.getScaledValue());
  return b === 0 ? `below ${fmtHz(x[0])}` : b === 3 ? `above ${fmtHz(x[2])}` : `${fmtHz(x[b - 1])}\u2013${fmtHz(x[b])}`;
}
function dynBandCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Band</span><span class="cell-sub mono">pick · reduction</span><div class="band-pick"></div>`;
  const wrap = cell.querySelector(".band-pick");
  const buttons = DYN_BANDS.map((band, b) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.addEventListener("click", () => { dynBand = b; renderModule(); renderLearn(); });
    wrap.appendChild(btn);
    return btn;
  });
  cell.refresh = () => buttons.forEach((btn, b) => {
    btn.className = "pill mono " + (b === dynBand ? "pill-red" : "pill-blue");
    btn.setAttribute("aria-pressed", String(b === dynBand));
    const gr = M.dynGr[b] || 0;
    btn.textContent = `${DYN_BANDS[b].short} ${dynBandActive(b) && gr < -0.05 ? fmtNum(gr) : ""}`.trim();
  });
  cell.refresh();
  return cell;
}
const dynPicker = dynBandCell();
function dynBandCells(b) {
  const split = b < 3
    ? liveKnobCell({ label: "Split", sub: "upper edge", state: P.dynX[b], def: [120, 1000, 6000][b], minText: fmtHz([40, 300, 3000][b]),
                     maxText: fmtHz([300, 3000, 12000][b]), fmt: fmtHz })
    : liveKnobCell({ label: "Split", sub: "lower edge", state: P.dynX[2], def: 6000, minText: "3 kHz", maxText: "12 kHz", fmt: fmtHz });
  return [
    liveKnobCell({ label: "Threshold", sub: "starts at", state: P.dynThr[b], def: -20, minText: `${MINUS}48`, maxText: "0", fmt: (v) => `${fmtNum(v, 1)} dB` }),
    liveKnobCell({ label: "Ratio", sub: "how firm", state: P.dynRatio[b], def: 1, minText: "1:1", maxText: "10:1", fmt: (v) => `${v.toFixed(1)}:1` }),
    liveKnobCell({ label: "Attack", sub: "grab speed", state: P.dynAtk[b], def: [30, 20, 10, 5][b], minText: "0.5", maxText: "100", fmt: fmtMs }),
    liveKnobCell({ label: "Release", sub: "let go", state: P.dynRel[b], def: [200, 150, 120, 100][b], minText: "20", maxText: "1000", fmt: fmtMs }),
    liveKnobCell({ label: "Gain", sub: "band level", state: P.dynGain[b], def: 0, minText: `${MINUS}12`, maxText: "+12", fmt: fmtSigned }),
    split,
  ];
}
const dynCellsByBand = [0, 1, 2, 3].map(dynBandCells);
const dynCells = () => [dynPicker, ...dynCellsByBand[dynBand]];

// Saturation cells
const SAT_MODES = ["Tape", "Tube", "Clip"];
const satModeIndex = () => clamp(Math.round(P.satMode.value * 2), 0, 2);
const satDriveAt = () => P.satDrive.getScaledValue();
const satMixAt = () => P.satMix.getScaledValue();
const satIsNeutral = () => satDriveAt() < 0.05 || satMixAt() < 0.05;
function satModeCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Mode</span><span class="cell-sub mono">character</span>
    <div class="seg"></div><span class="cell-value mono os-note"></span>`;
  const seg = cell.querySelector(".seg"), note = cell.querySelector(".os-note");
  const buttons = SAT_MODES.map((label, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = label.toUpperCase();
    b.addEventListener("click", () => { P.satMode.setChoiceIndex(i); refresh(); onParamsChanged(); userEdited(); });
    seg.appendChild(b);
    return b;
  });
  function refresh() {
    const idx = satModeIndex();
    buttons.forEach((b, i) => { b.className = "pill " + (i === idx ? "pill-red" : "pill-blue"); b.setAttribute("aria-pressed", String(i === idx)); });
    note.textContent = ["smooth · warm", "rich · 2nd harmonic", "firm · bright"][idx];
  }
  P.satMode.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  P.satMode.propertiesChangedEvent.addListener(refresh);
  cell.refresh = refresh;
  refresh();
  return cell;
}
const satCells = [
  satModeCell(),
  liveKnobCell({ label: "Drive", sub: "harmonics", state: P.satDrive, def: 0, minText: "0", maxText: "+18", fmt: (v) => `+${v.toFixed(1)} dB` }),
  liveKnobCell({ label: "Mix", sub: "wet / dry", state: P.satMix, def: 100, minText: "dry", maxText: "wet", fmt: (v) => `${Math.round(v)} %` }),
  barCell({ label: "Harmonics", sub: "added vs signal", minText: `${MINUS}60`, maxText: `${MINUS}10 dB`, id: "harmmeter" }),
];

// Stem EQ cells
const STEMS = ["Vocals", "Drums", "Bass", "Other"];
const stemGainAt = (k) => P.stemGain[k].getScaledValue();
const stemIsNeutral = () => [0, 1, 2, 3].every((k) => Math.abs(stemGainAt(k)) < 0.05);
const stemMoves = () => [0, 1, 2, 3].filter((k) => Math.abs(stemGainAt(k)) >= 0.05);
function stemHearsCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Model Hears</span><span class="cell-sub mono">share of the music</span>
    <div class="stemhear">${STEMS.map((n, k) => `<div class="sh-row"><span class="sh-name mono">${n[0]}</span>
      <div class="sh-bar screen"><div class="sh-fill" id="sh-fill-${k}"></div></div><span class="sh-val mono" id="sh-val-${k}"></span></div>`).join("")}</div>`;
  return cell;
}
const stemCells = [
  ...STEMS.map((name, k) => liveKnobCell({ label: name, sub: k === 3 ? "synths · keys · fx" : "level", state: P.stemGain[k], def: 0,
                                           minText: `${MINUS}12`, maxText: "+12", fmt: fmtSigned })),
  stemHearsCell(),
];

// Tone Steady / Resonance Tamer cells
const tsAmountAt = () => P.tsAmount.getScaledValue();
const rtAmountAt = () => P.rtAmount.getScaledValue();
const tsIsNeutral = () => !P.tsOn.getValue() || tsAmountAt() < 0.5;
const rtIsNeutral = () => !P.rtOn.getValue() || rtAmountAt() < 0.5;
const TS_SPEEDS = ["Slow", "Medium", "Fast"], RT_FOCUS = ["Full", "Boxiness", "Harshness"];
function segCell({ label, sub, state, labels, notes }) {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">${label}</span><span class="cell-sub mono">${sub}</span>
    <div class="seg seg-v"></div><span class="cell-value mono os-note"></span>`;
  const seg = cell.querySelector(".seg"), note = cell.querySelector(".os-note");
  const buttons = labels.map((text, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = text.toUpperCase();
    b.addEventListener("click", () => { state.setChoiceIndex(i); refresh(); onParamsChanged(); userEdited(); });
    seg.appendChild(b);
    return b;
  });
  function refresh() {
    // (comboIndex is defined further down; cells are built at load time, so work it out here)
    const n = labels.length, idx = clamp(Math.round(state.getChoiceIndex ? state.getChoiceIndex() : state.value * (n - 1)), 0, n - 1);
    buttons.forEach((b, i) => { b.className = "pill " + (i === idx ? "pill-red" : "pill-blue"); b.setAttribute("aria-pressed", String(i === idx)); });
    note.textContent = notes[idx];
  }
  state.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  state.propertiesChangedEvent.addListener(refresh);
  cell.refresh = refresh;
  refresh();
  return cell;
}
const tsCells = [
  liveKnobCell({ label: "Amount", sub: "how much drift", state: P.tsAmount, def: 0, minText: "off", maxText: "100", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Range", sub: "largest cut", state: P.tsRange, def: 3, minText: "1", maxText: "6 dB", fmt: (v) => `${MINUS}${v.toFixed(1)} / +${(v / 2).toFixed(1)}` }),
  segCell({ label: "Speed", sub: "reaction", state: P.tsSpeed, labels: TS_SPEEDS, notes: ["1 s \u00b7 sections", "0.4 s \u00b7 phrases", "0.15 s \u00b7 notes"] }),
  barCell({ label: "Steadying", sub: "biggest move now", minText: "0", maxText: "6 dB", id: "tsmeter" }),
];
const rtCells = [
  liveKnobCell({ label: "Amount", sub: "how deep", state: P.rtAmount, def: 0, minText: "off", maxText: "100", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Sensitivity", sub: "smaller peaks", state: P.rtSens, def: 50, minText: "0", maxText: "100", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Release", sub: "let-go speed", state: P.rtRelease, def: 80, minText: "20", maxText: "300", fmt: fmtMs }),
  segCell({ label: "Focus", sub: "where to look", state: P.rtFocus, labels: RT_FOCUS, notes: ["80 Hz \u2013 16 kHz", "150 \u2013 800 Hz", "1.5 \u2013 8 kHz"] }),
  barCell({ label: "Cutting", sub: "deepest now", minText: "0", maxText: `${MINUS}12 dB`, id: "rtmeter" }),
];

// Unlimiter cells
const ulAmountAt = () => P.ulAmount.getScaledValue();
const ulIsNeutral = () => !P.ulOn.getValue() || ulAmountAt() < 0.5;
const UL_FOCUS = ["Full", "Kick", "Snap"];
// Squash reading -> plain words (thresholds match dsp/include/polisher/Unlimiter.h).
const UL_FULL = 8.5, UL_OPEN = 12.5;
const ulScale = (sq) => clamp((UL_OPEN - sq) / (UL_OPEN - UL_FULL), 0, 1);
const squashWord = (sq) => (!sq ? "listening" : sq <= UL_FULL ? "brickwalled" : sq < UL_OPEN ? "squashed" : "open");
const ulCells = [
  liveKnobCell({ label: "Amount", sub: "punch put back", state: P.ulAmount, def: 0, minText: "off", maxText: "100", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Length", sub: "each hit", state: P.ulLength, def: 15, minText: "5", maxText: "50", fmt: fmtMs }),
  segCell({ label: "Focus", sub: "hits to find", state: P.ulFocus, labels: UL_FOCUS, notes: ["every hit", "kick · 808 < 150 Hz", "snare · hats > 1 kHz"] }),
  barCell({ label: "Squash", sub: "your audio", minText: "open", maxText: "wall", id: "ulsquash" }),
  barCell({ label: "Restoring", sub: "lift now", minText: "0", maxText: "12 dB", id: "ulmeter" }),
];

function renderModule() {
  const m = modules.find((x) => x.id === selected);
  const row = $("knob-row");
  row.innerHTML = "";
  $("module-name").textContent = m.name.toUpperCase();
  $("soon-badge").hidden = !!m.live;
  $("btn-bypass").hidden = !m.live;
  if (m.live) {
    row.classList.remove("soon");
    ({ ul: ulCells, clip: clipperCells, eq: eqCells, stem: stemCells, ts: tsCells, rt: rtCells, bass: bassCells, dyn: dynCells(), img: imagerCells, sat: satCells, max: maximizerCells })[m.id].forEach((c) => row.appendChild(c));
    $("module-note").textContent = "Live · drag up/down · Shift = fine · double-click = reset";
    refreshHeadButton();
  } else {
    row.classList.add("soon");
    m.cells.forEach((c) => row.appendChild(staticKnobCell(c)));
    $("module-note").textContent = "Designed · not active in this build";
    $("soon-text").textContent = m.soon;
  }
  updateLiveReadouts();
  drawEq();
}

// ---------------------------------------------------------------------------------------------
// EQ curve and draggable band points on the spectrum.
const plotWrap = $("plot-wrap"), eqNodes = $("eq-nodes"), eqReadout = $("eq-readout");
const yForEqDb = (db) => clamp(100 - (db / EQ_VIEW) * 100, 0, 200);
const nodeEls = EQ_BANDS.map((band, b) => {
  const n = document.createElement("div");
  n.className = "eq-node";
  n.textContent = String(b + 1);
  n.setAttribute("role", "slider");
  n.setAttribute("aria-label", band.name);
  eqNodes.appendChild(n);

  let dragging = false;
  const setFromPointer = (e) => {
    const r = plotWrap.getBoundingClientRect();
    const fx = clamp((e.clientX - r.left) / r.width, 0, 1), fy = clamp((e.clientY - r.top) / r.height, 0, 1);
    const f = 20 * Math.pow(1000, fx);
    const g = clamp((0.5 - fy) * 2 * EQ_VIEW, -EQ_MAX, EQ_MAX);
    const fs = P.eqFreq[b], gs = P.eqGain[b];
    fs.setNormalisedValue(scaledToNorm(fs, clamp(f, fs.properties.start, fs.properties.end)));
    gs.setNormalisedValue(scaledToNorm(gs, Math.abs(g) < 0.15 ? 0 : g));   // gentle snap to 0 dB
    eqCells[b].refresh(); onParamsChanged(); userEdited();
  };
  n.addEventListener("pointerdown", (e) => {
    n.setPointerCapture(e.pointerId); dragging = true; n.classList.add("dragging");
    P.eqFreq[b].sliderDragStarted(); P.eqGain[b].sliderDragStarted();
    e.preventDefault(); e.stopPropagation();
  });
  n.addEventListener("pointermove", (e) => { if (dragging) setFromPointer(e); });
  const end = () => {
    if (!dragging) return;
    dragging = false; n.classList.remove("dragging"); eqReadout.hidden = true;
    P.eqFreq[b].sliderDragEnded(); P.eqGain[b].sliderDragEnded();
  };
  n.addEventListener("pointerup", end);
  n.addEventListener("pointercancel", end);
  n.addEventListener("dblclick", () => {
    for (const [s, v] of [[P.eqGain[b], 0], [P.eqFreq[b], band.def]]) { s.sliderDragStarted(); s.setNormalisedValue(scaledToNorm(s, v)); s.sliderDragEnded(); }
    eqCells[b].refresh(); onParamsChanged(); userEdited();
  });
  n.addEventListener("wheel", (e) => {
    e.preventDefault();
    const gs = P.eqGain[b];
    gs.sliderDragStarted();
    gs.setNormalisedValue(scaledToNorm(gs, clamp(eqGainAt(b) + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.1 : 0.5), -EQ_MAX, EQ_MAX)));
    gs.sliderDragEnded();
    eqCells[b].refresh(); onParamsChanged(); userEdited();
  }, { passive: false });
  n.addEventListener("pointerenter", () => { n.dataset.hover = "1"; drawEq(); });
  n.addEventListener("pointerleave", () => { delete n.dataset.hover; if (!dragging) eqReadout.hidden = true; });
  return n;
});

let eqDrawQueued = false;
function drawEq() {
  if (eqDrawQueued) return;
  eqDrawQueued = true;
  requestAnimationFrame(() => { eqDrawQueued = false; drawEqNow(); });
}
function drawEqNow() {
  const onEqPage = selected === "eq";
  const active = eqEnabled() && !P.bypass.getValue();
  const showCurve = onEqPage || (active && !eqIsFlat());
  $("eq-layer").toggleAttribute("hidden", !showCurve);   // SVG elements have no .hidden property
  $("legend-eq").hidden = !showCurve;
  eqNodes.hidden = !onEqPage;
  if (!showCurve) return;

  const sr = M.sr > 0 ? M.sr : 48000;
  let d = "";
  const N = 180;
  for (let i = 0; i <= N; i++) {
    const f = 20 * Math.pow(1000, i / N);
    const y = yForEqDb(active ? eqResponseDb(f, sr) : 0);
    d += (i ? "L" : "M") + ((i / N) * W).toFixed(1) + "," + y.toFixed(1);
  }
  $("eq-curve").setAttribute("d", d);
  $("eq-fill").setAttribute("d", d + `L${W},100L0,100Z`);
  $("eq-layer").style.opacity = active ? (onEqPage ? "1" : ".55") : ".35";

  nodeEls.forEach((n, b) => {
    const f = eqFreqAt(b), g = eqGainAt(b);
    n.style.left = ((xForFreq(f) / W) * 100).toFixed(2) + "%";
    n.style.top = ((yForEqDb(g) / 200) * 100).toFixed(2) + "%";
    n.classList.toggle("flat", Math.abs(g) < 0.05 || !active);
    const text = `${EQ_BANDS[b].name} · ${fmtHz(f)} · ${g > 0.049 ? "+" : ""}${fmtNum(g, 1)} dB`;
    n.setAttribute("aria-valuetext", text);
    if (n.classList.contains("dragging") || n.dataset.hover) {
      eqReadout.textContent = text;
      eqReadout.style.left = n.style.left;
      eqReadout.style.top = n.style.top;
      eqReadout.hidden = false;
    }
  });
}

// ---------------------------------------------------------------------------------------------
// Learn panel
function para(cls, text) { const p = document.createElement("p"); p.className = cls; p.textContent = text; return p; }
function head(text) { const s = document.createElement("span"); s.className = "l-h"; s.textContent = text; return s; }
function sep() { const d = document.createElement("div"); d.className = "l-sep"; return d; }
function item(name, value, why) {
  const d = document.createElement("div");
  d.className = "l-item";
  d.innerHTML = `<div class="l-item-head"><span class="l-item-name"></span>${value ? '<span class="pill pill-red mono l-item-val"></span>' : ""}</div><p class="l-item-why"></p>`;
  d.querySelector(".l-item-name").textContent = name;
  if (value) d.querySelector(".l-item-val").textContent = value;
  d.querySelector(".l-item-why").textContent = why;
  return d;
}
// A Learn box. Warnings always carry a plan: fix = { need: "Do I need to fix it? …", steps: ["…", …] }.
function tip(title, text, ok, fix) {
  const d = document.createElement("div");
  // A plan whose answer is "No" is information, not a warning: show it calm (blue), not red.
  if (fix && /^No\b/.test(fix.need)) ok = true;
  d.className = "l-tip" + (ok ? " ok" : "");
  d.appendChild(head(title));
  d.appendChild(para("l-p", text));
  if (fix) {
    const need = document.createElement("p");
    need.className = "l-p l-need";
    need.innerHTML = "<b>Do I need to fix it?</b> ";
    need.appendChild(document.createTextNode(fix.need));
    d.appendChild(need);
    if (fix.steps && fix.steps.length) {
      const how = document.createElement("p");
      how.className = "l-p l-how";
      how.innerHTML = "<b>How:</b>";
      d.appendChild(how);
      const ol = document.createElement("ol");
      ol.className = "l-steps";
      for (const st of fix.steps) { const li = document.createElement("li"); li.textContent = st; ol.appendChild(li); }
      d.appendChild(ol);
    }
  }
  return d;
}

function maxGrRecent() { return grWindow.length ? Math.min(...grWindow) : 0; }
// How much louder the master is than the incoming mix right now (short-term), or null.
function louderBy() {
  return M.shortTerm > -60 && M.inShort > -60 ? M.shortTerm - M.inShort : null;
}
function avgGrRecent() { return grWindow.length ? grWindow.reduce((a, b) => a + b, 0) / grWindow.length : 0; }

function renderLearn() {
  const body = $("learn-body");
  body.innerHTML = "";
  if (!learnOn) {
    const off = document.createElement("div");
    off.className = "l-off";
    off.innerHTML = `<span class="wm">LEARN IS OFF</span><p>Turn it on to see what each control is doing to your track, in plain producer language.</p>`;
    body.appendChild(off);
    return;
  }

  if (learnView === "report" && amReport) { renderReport(body); return; }

  const m = modules.find((x) => x.id === selected);
  if (!m.live) {
    body.appendChild(head("WHAT IT WILL DO"));
    body.appendChild(para("l-p", m.what));
    body.appendChild(sep());
    body.appendChild(head("THE CONTROLS"));
    m.controls.forEach(([n, w]) => body.appendChild(item(n, null, w)));
    body.appendChild(tip("ARRIVING SOON", m.soon + " When Auto-Master sets it, this panel will explain the exact reason for every value.", true));
    return;
  }

  if (m.id === "clip") { renderClipperLearn(body); liftWarning(body); prependAmSection(body, "clip"); return; }
  if (m.id === "eq") { renderEqLearn(body); liftWarning(body); prependAmSection(body, "eq"); return; }
  if (m.id === "bass") { renderBassLearn(body); liftWarning(body); prependAmSection(body, "bass"); return; }
  if (m.id === "img") { renderImagerLearn(body); liftWarning(body); prependAmSection(body, "img"); return; }
  if (m.id === "dyn") { renderDynLearn(body); liftWarning(body); prependAmSection(body, "dyn"); return; }
  if (m.id === "sat") { renderSatLearn(body); liftWarning(body); prependAmSection(body, "sat"); return; }
  if (m.id === "ul") { renderUlLearn(body); liftWarning(body); prependAmSection(body, "ul"); return; }
  if (m.id === "ts") { renderTsLearn(body); liftWarning(body); prependAmSection(body, "ts"); return; }
  if (m.id === "rt") { renderRtLearn(body); liftWarning(body); prependAmSection(body, "rt"); return; }
  if (m.id === "stem") {
    renderStemLearn(body); liftWarning(body); prependAmSection(body, "stem");
    const b = stemSuggestButton();
    if (b) { const at = body.querySelector(".l-sep"); body.insertBefore(b, at ? at.nextSibling : null); }
    return;
  }

  const drive = P.drive.getScaledValue();
  const ceiling = P.ceiling.getScaledValue();
  const release = Math.round(P.release.getScaledValue());
  const adaptive = !!P.adaptive.getValue();
  const bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const peakGr = maxGrRecent();
  const avgGr = avgGrRecent();

  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) {
    body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the Maximizer."));
  } else if (!playing) {
    body.appendChild(para("l-p", "Press play in your DAW. As the music runs I'll describe what the Maximizer is doing to it."));
  } else {
    const st = M.shortTerm > -99 ? `${fmtNum(M.shortTerm)} LUFS` : "—";
    const it = M.integrated > -99 ? `${fmtNum(M.integrated)} LUFS integrated` : "Integrated is still measuring";
    const gain = louderBy();
    const vsMix = gain === null ? "" : `, ${gain >= 0 ? "+" : MINUS}${Math.abs(gain).toFixed(1)} dB ${gain >= 0 ? "louder" : "quieter"} than your mix`;
    const grText = peakGr < -0.2 ? `The limiter is taking up to ${fmtNum(-peakGr)} dB off the loudest moments.` : "The limiter is barely touching it: the peaks already fit under the ceiling.";
    body.appendChild(para("l-p", `Your master is running at ${st} short-term${vsMix} (${it}). ${grText}`));
  }

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Drive", `+${drive.toFixed(1)} dB`,
    drive < 0.05
      ? "No push yet. Turn Drive up to make the master louder; the limiter catches the peaks so nothing clips."
      : P.clipOn.getValue()
        ? `Pushing the mix ${drive.toFixed(1)} dB into the Soft Clipper and then the limiter. The quiet and medium parts come up by that much; the loud hits are shaved and held at the ceiling.`
        : `Pushing the mix ${drive.toFixed(1)} dB into the limiter. The quiet and medium parts come up by that much; the loud hits are held at the ceiling.`));
  body.appendChild(item("Ceiling", `${fmtNum(ceiling, 1)} dBTP`,
    `Nothing leaves Polisher above ${fmtNum(ceiling, 1)} dBTP, including peaks that fall between samples. ` +
    (ceiling > -0.95 ? "Above −1 dBTP, the MP3/AAC versions streaming services make can clip. −1.0 is the safe choice."
                     : "That leaves room for the MP3/AAC conversion streaming services do, so the streamed version won't clip.")));
  body.appendChild(item("Release", `${release} ms`,
    adaptive
      ? `Adaptive around ${release} ms: recovers quickly after single hits (drums) and moves slowly on sustained sound (808s, pads), so it doesn't pump.`
      : `Fixed at ${release} ms. Shorter sounds louder but can distort bass; longer is smoother but can pump on drums.`));

  const gain = louderBy();
  if (bypassed || !playing) {
    body.appendChild(tip("TIP", "Set Drive by ear while watching Short-term in the footer. Most streaming releases land between −14 and −9 LUFS.", true));
  } else if (gain !== null && drive >= 9 && drive - gain > 6) {
    body.appendChild(tip("DIMINISHING RETURNS",
      `Drive is at +${drive.toFixed(1)} dB, but the master is only ${gain.toFixed(1)} dB louder than your mix. The loud hits already reach the ceiling, so the extra Drive is going into squashing them, not into loudness. ` +
      "", false, { need: "Yes. The extra Drive only adds squashing, not loudness.",
        steps: ["Turn Drive down slowly while watching Short-term in the footer.",
                "Stop where Short-term starts to drop, then go back up about 0.5 dB.",
                P.clipOn.getValue() ? "For more loudness, lower Clip Level (Soft Clipper, in the chain on the left) by 1 dB." : "For more loudness, turn on the Soft Clipper (chain, left): on spiky drums it gets loudness the limiter can't."] }));
  } else if (avgGr < -6 || peakGr < -9) {
    body.appendChild(tip("FIX IT IN THE MIX",
      `That's heavy limiting (up to ${fmtNum(-peakGr)} dB).`, false,
      { need: "Only if the kick or snare sound weaker than in your mix.",
        steps: ["Turn on MATCH and flip A/B, listening only to the kick and snare.",
                "If they lost punch: turn Drive down 1–2 dB, or turn on the Soft Clipper (chain, left) so it takes some of the peaks.",
                "Best fix, in the mix: put a clipper or limiter on the drum bus taking 2–3 dB off the hits, then run Auto-Master again."] }));
  } else if (M.integrated > -99 && M.integrated > [-14, -9, -7][clamp(Math.round(P.amTarget.value * 2), 0, 2)] + 1) {
    const tgt = [-14, -9, -7][clamp(Math.round(P.amTarget.value * 2), 0, 2)];
    body.appendChild(tip("HEADS UP",
      `Your master is at ${fmtNum(M.integrated)} LUFS, louder than your ${["Stream", "Loud", "Club"][clamp(Math.round(P.amTarget.value * 2), 0, 2)]} ${fmtNum(tgt, 0)} target. Spotify and YouTube play everything at about −14 LUFS, so extra loudness is turned down there: it buys density, not volume.`, false,
      { need: "Only if it sounds squashed. Loud is a style choice (common in Trap), not a mistake.",
        steps: ["Turn on MATCH and flip A/B. If B sounds flatter or the drums weaker, it's too loud.",
                "To bring it down: lower Drive (above) until Integrated reads your target, or click Auto-Master again.",
                "For streaming only, choose Stream −14 or Loud −9 as the Target and run Auto-Master again."] }));
  } else if (M.integrated > -99 && M.integrated < -16) {
    body.appendChild(tip("SOUNDS CLEAN", "Plenty of dynamics left. For a streaming release, you can push Drive until Integrated reaches around −14 to −10 LUFS.", true));
  } else {
    body.appendChild(tip("SOUNDS HEALTHY", "Limiting is moderate, so transients should stay intact. When you A/B, turn on MATCH so B isn't simply louder: louder always sounds \u201cbetter\u201d.", true));
  }
  const note = para("l-p", "When Auto-Master arrives, this panel will explain its own choices for every module.");
  note.style.cssText = "font-size:12px;color:var(--dim)";
  body.appendChild(note);
  liftWarning(body);
  prependAmSection(body, "max");
}

function renderEqLearn(body) {
  const bypassed = !!P.bypass.getValue();
  const on = eqEnabled();
  const moved = EQ_BANDS.map((band, b) => ({ band, b, g: eqGainAt(b), f: eqFreqAt(b) })).filter((x) => Math.abs(x.g) >= 0.05);
  const sgn = (g) => `${g > 0 ? "+" : MINUS}${Math.abs(g).toFixed(1)} dB`;

  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the chain."));
  else if (!on) body.appendChild(para("l-p", "The Tonal EQ is off, so your mix's tone goes through unchanged. Turn it on to hear your band settings."));
  else if (!moved.length) body.appendChild(para("l-p", "All seven bands are at 0 dB, so the EQ isn't touching the sound (and uses no CPU). Turn a knob, or drag a numbered point on the spectrum: sideways moves the frequency, up/down sets the gain."));
  else body.appendChild(para("l-p", "Shaping the tone: " + moved.map((x) => `${sgn(x.g)} ${x.band.name} at ${fmtHz(x.f)}`).join(", ") +
    ". The blue curve on the spectrum is the change; the red line is the master after the whole chain."));

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  if (moved.length && on) moved.forEach((x) => body.appendChild(item(`${x.band.name} · ${fmtHz(x.f)}`, sgn(x.g), x.g > 0 ? x.band.boost : x.band.cut)));
  else modules.find((m) => m.id === "eq").controls.forEach(([n, w]) => body.appendChild(item(n, null, w)));

  if (bypassed || !on) {
    body.appendChild(tip("TIP", "Cut before you boost: a 1–2 dB Low Mid cut often sounds clearer and louder than boosting the highs.", true));
    return;
  }
  const big = moved.filter((x) => Math.abs(x.g) >= 4.5).sort((a, c) => Math.abs(c.g) - Math.abs(a.g))[0];
  const lowBoost = Math.max(eqGainAt(0), eqGainAt(1));
  const bite = Math.max(0, eqGainAt(4)) + Math.max(0, eqGainAt(5));
  const peakGr = maxGrRecent();
  if (big) {
    body.appendChild(tip("FIX IT IN THE MIX",
      `${sgn(big.g)} of ${big.band.name} is a big move for a master: it changes every sound in that range, not just the one that needs it.`, false,
      { need: "Recommended, but the master still works as it is.",
        steps: [`In your mix, solo the main sound in the ${big.band.name} range (usually the 808, the kick or the vocal).`,
                "Turn that one sound up or down (or EQ just that track) by about the same amount.",
                "Bring this band back toward 0 and run Auto-Master again; master EQ moves of 1–3 dB sound most natural."] }));
  } else if (lowBoost > 2 && peakGr < -6) {
    body.appendChild(tip("HEADS UP",
      `The low-end boost is making the Maximizer work harder (up to ${fmtNum(-peakGr)} dB). Low end eats headroom, so each dB of boost there costs loudness.`, false,
      { need: "Optional. Do it if the master sounds squashed or the kick loses punch.",
        steps: ["Lower Low End or Bass by 1 dB.", "Or leave the bass and cut Low Mid by 1 dB instead: the bass reads clearer without using headroom.",
                "Watch the Maximizer's GR: under about 4 dB is comfortable."] }));
  } else if (bite > 4) {
    body.appendChild(tip("HEADS UP", "That much boost between 2.5 and 5 kHz can get harsh, especially on earbuds.", false,
      { need: "Check first: it depends on your song.", steps: ["Listen at low volume, and on earbuds or a phone.", "If vocals or hats sting, lower Upper Mid or Presence by 1–2 dB."] }));
  } else if (eqGainAt(6) > 4) {
    body.appendChild(tip("HEADS UP", "Lots of Air can make hi-hats and 's' sounds sizzle.", false,
      { need: "Only if hats or 's' sounds poke out.", steps: ["Listen to the hats and the vocal's 's' sounds.", "If they sizzle, lower Air by 1–2 dB, or cut Presence by 1 dB."] }));
  } else if (!moved.length) {
    body.appendChild(tip("TRY THIS", "Look at the white line (your mix). A hump that sits above its neighbours, often around 200–400 Hz, is a good place for a 1–2 dB cut.", true));
  } else {
    body.appendChild(tip("LISTEN FOR TONE", "Click EQ ON/OFF (top right of this panel) to compare. EQ boosts add level, and louder always sounds \u201cbetter\u201d, so judge the tone, not the volume.", true));
  }
}

function renderBassLearn(body) {
  const bypassed = !!P.bypass.getValue();
  const on = !!P.bassOn.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const monoHz = Math.round(bassMonoHz()), monoOn = bassMonoOn();
  const tame = Math.round(P.bassTame.getScaledValue()), punch = Math.round(P.bassPunch.getScaledValue());
  const width = Math.round(M.bassWidth || 0);
  const peakTame = tameWindow.length ? Math.min(...tameWindow) : 0;
  const avgTame = tameWindow.length ? tameWindow.reduce((a, b) => a + b, 0) / tameWindow.length : 0;
  const peakPunch = punchWindow.length ? Math.max(...punchWindow) : 0;
  const peakGr = maxGrRecent();
  const neutral = !monoOn && tame < 1 && punch < 1;

  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the chain."));
  else if (!on) body.appendChild(para("l-p", "Bass Control is off, so the low end goes through unchanged."));
  else if (!playing) body.appendChild(para("l-p", "Press play. I'll measure how stereo your bass is and show what each tool is doing to it."));
  else {
    const parts = [];
    parts.push(`Your mix's bass (below 150 Hz) is ${width} % stereo.`);
    if (monoOn) parts.push(`Below ${monoHz} Hz it's now mono.`);
    if (tame >= 1) parts.push(peakTame < -0.3 ? `Sub Tame is turning down low notes that jump out, by up to ${fmtNum(-peakTame)} dB.` : "Sub Tame is ready, but no low note is jumping out right now.");
    if (punch >= 1) parts.push(peakPunch > 0.3 ? `Punch is adding up to ${peakPunch.toFixed(1)} dB of thump to each kick.` : "Punch is on, but it isn't hearing clear kick hits right now.");
    if (neutral) parts.push("All three tools are at neutral, so the low end passes through untouched.");
    body.appendChild(para("l-p", parts.join(" ")));
  }

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Mono Below", fmtMono(monoHz),
    !monoOn ? "Off: the bass keeps whatever stereo width your mix has."
      : `Everything below ${monoHz} Hz is mono. Club systems and phones play bass in mono anyway, so stereo bass there partly cancels and loses power. ` +
        (monoHz > 160 ? "Above about 150 Hz this starts narrowing synths and guitars too." : "80–120 Hz is the safe range.")));
  body.appendChild(item("Sub Tame", `${tame} %`,
    tame < 1 ? "Off: low notes keep their natural level differences."
      : `Listens to the low end's own average level. When an 808 note or kick jumps more than ${(6 - 3 * tame / 100).toFixed(1)} dB above it, that note is turned down, so the Maximizer doesn't grab the whole mix on big bass notes.`));
  body.appendChild(item("Punch", `${punch} %`,
    punch < 1 ? "Off: kicks stay as they are."
      : `Adds up to ${(4 * punch / 100).toFixed(1)} dB of thump in the first moments of each kick. The sustained sub stays where it was, so it hits harder without getting boomier.`));

  if (bypassed || !on || !playing) {
    body.appendChild(tip("TIP", "Start with Mono Below at 100 Hz. It's the safest low-end move in mastering: you'll barely hear it on headphones, but the bass hits harder on big systems.", true));
  } else if (width > 35 && !monoOn) {
    body.appendChild(tip("HEADS UP", `Your bass is ${width} % stereo below 150 Hz. On club systems and phones, stereo bass partly cancels and loses power.`, false,
      { need: "Yes. It's safe and you'll barely hear the change on headphones.", steps: ["Set Mono Below (above) to 100 Hz.", "Or run Auto-Master: it sets this for you."] }));
  } else if (monoHz > 180) {
    body.appendChild(tip("HEADS UP", `Mono Below at ${monoHz} Hz is high: it narrows the low mids too (guitars, synth bodies, reverbs).`, false,
      { need: "Only if the mix sounds narrower than you want.", steps: ["Lower Mono Below to 100–120 Hz.", "Flip A/B with MATCH on to compare the width."] }));
  } else if (avgTame < -5) {
    body.appendChild(tip("FIX IT IN THE MIX", `Sub Tame is cutting up to ${fmtNum(-peakTame)} dB: some 808 notes are much louder than others.`, false,
      { need: "Optional. Sub Tame is already handling it; fixing it in the mix just sounds more natural.",
        steps: ["In your 808 track, find the notes that jump out (usually the lowest or longest ones).", "Lower their velocity or volume by a few dB.", "Run Auto-Master again: it will use less Sub Tame."] }));
  } else if (punch >= 1 && peakGr < -6) {
    body.appendChild(tip("HEADS UP", `The Maximizer is squashing the extra punch (up to ${fmtNum(-peakGr)} dB of limiting).`, false,
      { need: "Only if the kick doesn't hit harder with Punch on.", steps: ["Lower Drive on the Maximizer by 1 dB.", "Or turn on the Soft Clipper (chain, left) so the kick attacks survive."] }));
  } else if (neutral) {
    body.appendChild(tip("TRY THIS", "Set Mono Below to 100 Hz first. Then, if your 808 notes jump in level, add Sub Tame until the Taming meter moves 1–3 dB on the loud notes.", true));
  } else {
    body.appendChild(tip("SOUNDS SOLID", "The low end is controlled. Check the kick and 808 on phone speakers or earbuds: if they vanish, add a little Bass on the Tonal EQ rather than more Low End.", true));
  }
}

function renderImagerLearn(body) {
  const on = !!P.imgOn.getValue(), bypassed = !!P.bypass.getValue();
  const [lo, mi, hi] = [P.imgLow, P.imgMid, P.imgHigh].map((x) => Math.round(x.getScaledValue()));
  const corr = M.corr;
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched."
    : !on ? "The Stereo Imager is off, so the stereo picture is unchanged."
    : lo === 100 && mi === 100 && hi === 100 ? "All three bands are at 100 %, so the stereo picture is untouched."
    : `Width is ${lo} % in the lows, ${mi} % in the mids and ${hi} % in the highs. Only the stereo (side) part is changed; anything in the center (vocal, kick, bass) stays exactly where it is.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Lows", `${lo} %`, lo < 100 ? "Narrower low end: tighter bass that translates to club systems and phones." : lo > 100 ? "Wider low end: rarely a good idea on a master; bass loses focus and power in mono." : "Unchanged. For mono bass use Mono Below in Bass Control."));
  body.appendChild(item("Mids", `${mi} %`, mi > 100 ? "Wider mids: guitars, synths and backing vocals spread out. Small moves only (up to about 120 %), or the lead vocal can feel detached." : mi < 100 ? "Narrower mids: a more focused, centered sound." : "Unchanged."));
  body.appendChild(item("Highs", `${hi} %`, hi > 100 ? "Wider highs: hi-hats, reverbs and ad-libs open up. This is where width sounds best." : hi < 100 ? "Narrower highs: a more focused top." : "Unchanged."));
  body.appendChild(item("Correlation", `${corr < 0 ? MINUS : "+"}${Math.abs(corr).toFixed(2)}`, "How mono-safe the master is: +1 is mono, around +0.3 to +0.7 is a healthy wide mix, below 0 means parts cancel when played on one speaker (phones, club PAs)."));
  if (on && !bypassed && corr < 0.1 && performance.now() - lastAudioTime < 1500)
    body.appendChild(tip("HEADS UP", "Correlation is near or below zero: parts of the mix will cancel on mono speakers (phones, club PAs).", false,
      { need: "Yes.", steps: ["Set Mids and Highs back to 100 %.", "If Correlation is still low, check the mix for stereo wideners or out-of-phase effects: press Cubase's Mono button on the Stereo Out and listen for parts that disappear."] }));
  else if (on && lo > 100)
    body.appendChild(tip("HEADS UP", "Widening the lows on a master usually costs punch and mono compatibility.", false,
      { need: "Yes.", steps: ["Set Lows back to 100 %.", "For width, use Highs (about 110–115 %) instead."] }));
  else
    body.appendChild(tip("TRY THIS", "Highs to about 115 % and Mids to 105 %: a subtle lift in width that stays mono-safe. Watch Correlation stay above +0.3.", true));
}

function renderDynLearn(body) {
  const on = !!P.dynOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const b = dynBand, band = DYN_BANDS[b];
  const thr = P.dynThr[b].getScaledValue(), ratio = dynRatioAt(b), atk = P.dynAtk[b].getScaledValue(),
        rel = P.dynRel[b].getScaledValue(), gain = dynGainAt(b);
  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the chain."));
  else if (!on) body.appendChild(para("l-p", "Dynamics is off, so every band passes through unchanged."));
  else if (dynIsNeutral()) body.appendChild(para("l-p", "Every band is at 1:1 with no gain change, so Dynamics isn't touching the sound. Raise a band's Ratio to start holding it down."));
  else {
    const parts = [0, 1, 2, 3].filter(dynBandActive).map((k) => {
      const gr = M.dynGr[k] || 0;
      const r = dynRatioAt(k);
      return `${DYN_BANDS[k].name} (${dynRangeText(k)}): ${r > 1.005 ? `${r.toFixed(1)}:1` : "no compression"}${Math.abs(dynGainAt(k)) >= 0.05 ? `, ${fmtSigned(dynGainAt(k))}` : ""}${r > 1.005 && playing ? `, pulling down ${gr < -0.05 ? fmtNum(-gr) : "0.0"} dB right now` : ""}.`;
    });
    body.appendChild(para("l-p", parts.join(" ")));
  }
  body.appendChild(sep());
  body.appendChild(head(`${band.name.toUpperCase()} BAND (${dynRangeText(b)})`));
  body.appendChild(para("l-p", `Holds the ${band.holds}.`));
  body.appendChild(item("Threshold", `${fmtNum(thr, 1)} dB`, "Where the band starts being held down. Lower = more of the music is compressed. Set it so the reduction only moves on the loudest hits."));
  body.appendChild(item("Ratio", `${ratio.toFixed(1)}:1`, ratio <= 1.005 ? "1:1: this band isn't compressed." : ratio <= 2.5 ? "A gentle mastering ratio: evens the band out without flattening it." : ratio <= 4 ? "Firm: the band gets noticeably steadier. Check the punch with MATCH." : "Heavy for a master: the band will sound squashed. 1.5:1 to 3:1 is typical."));
  body.appendChild(item("Attack", fmtMs(atk), atk >= 20 ? "Slow: the start of each hit gets through before the band is turned down, so drums keep their punch." : atk >= 5 ? "Medium: catches most of each hit, a little of the snap stays." : "Fast: catches peaks almost instantly, smoother but less punchy."));
  body.appendChild(item("Release", fmtMs(rel), (rel <= 80 ? "Fast: recovers between hits; too fast can sound like fluttering on bass." : rel <= 300 ? "Medium: recovers between beats, the usual choice for a master." : "Slow: smooth and steady, evens out whole phrases.") + (P.dynAdaptive.getValue() ? " Adaptive release is on: quick after short hits, this setting on sustained notes." : "")));
  body.appendChild(item("Gain", fmtSigned(gain), Math.abs(gain) < 0.05 ? "The band's level after compression is unchanged." : gain > 0 ? "Turns the band up after compression: makes up what it lost, or brings it forward." : "Turns the band down after compression."));
  body.appendChild(item("Split", fmtHz(P.dynX[Math.min(b, 2)].getScaledValue()), b < 3 ? "Where this band ends and the next begins." : "Where this band starts."));
  const worst = Math.min(...[0, 1, 2, 3].map((k) => (dynRatioAt(k) > 1.005 ? M.dynGr[k] || 0 : 0)));
  if (on && !bypassed && playing && worst < -6)
    body.appendChild(tip("HEADS UP", `One band is being pulled down ${fmtNum(-worst)} dB. On a master, 1 to 3 dB of reduction is usually plenty; more starts to sound squashed.`, false,
      { need: "Yes, unless you want an obviously compressed sound.", steps: ["Click the band whose button shows the biggest number.", "Raise its Threshold until that number stays under about 3 dB.", "Or lower its Ratio to 2:1."] }));
  else if (on && [0, 1, 2, 3].some((k) => dynRatioAt(k) > 4))
    body.appendChild(tip("HEADS UP", "A band is above 4:1. That's heavy for a master.", false,
      { need: "Optional. Check with MATCH whether it sounds squashed.", steps: ["Set that band's Ratio to 2:1.", "Lower its Threshold a little if you still want the same control."] }));
  else
    body.appendChild(tip("TRY THIS", "On the LOW band: Ratio 2:1, Attack 30 ms, then lower the Threshold until the band pulls 1 to 2 dB on the kick and 808. The low end gets steadier without ducking the vocal. Compare with MATCH on.", true));
}

function renderSatLearn(body) {
  const on = !!P.satOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const mode = SAT_MODES[satModeIndex()], drive = satDriveAt(), mix = satMixAt(), harm = M.satHarm;
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Saturation is off, so nothing is added."
    : drive < 0.05 ? "Drive is at 0, so Saturation passes the sound untouched. Turn Drive up to add harmonics."
    : mix < 0.05 ? "Mix is at 0 %, so you only hear the dry signal."
    : !playing ? "Press play to see how much Saturation is adding."
    : harm <= -60 ? `${mode} is on, but the mix is too quiet for it to add much yet.`
    : `${mode} is adding harmonics about ${fmtNum(-harm, 0)} dB below the music${harm > -25 ? ": clearly audible, the master is getting denser" : harm > -40 ? ": felt more than heard, the usual sweet spot" : ": very subtle"}.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Mode", mode, mode === "Tape" ? "Smooth and warm: rounds off the loudest moments and adds odd harmonics. The safest choice on a full mix."
    : mode === "Tube" ? "Rich: mostly the 2nd harmonic, which makes bass and vocals sound fuller and helps an 808 show up on phone speakers."
    : "Firm and bright: a harder knee with more upper harmonics. Adds edge and bite; use less Drive."));
  body.appendChild(item("Drive", `+${drive.toFixed(1)} dB`, drive < 0.05 ? "Off." : drive <= 6 ? "Light: only the loudest moments are shaped." : drive <= 12 ? "Medium: the master gets noticeably denser." : "Heavy: obvious color. Pull Mix back if it gets harsh."));
  body.appendChild(item("Mix", `${Math.round(mix)} %`, mix >= 99.5 ? "Fully saturated signal." : `Blends ${Math.round(mix)} % of the harmonics in and keeps the rest clean, so transients stay sharp while the harmonics fill in underneath.`));
  body.appendChild(item("Level", "", "Quiet parts pass at the same level, so turning Drive up changes color, not volume. It reacts to how hot your mix is: louder mixes saturate more at the same Drive."));
  if (on && !bypassed && playing && harm > -18)
    body.appendChild(tip("HEADS UP", "The harmonics are strong. That's a sound, not a mistake, but on most masters it reads as distortion.", false,
      { need: "Only if it sounds harsh or crunchy to you.", steps: ["Turn on MATCH and flip A/B.", "If B sounds distorted: lower Drive by 3 dB, or Mix to about 30 %.", "Aim for the Harmonics meter around −25 to −35 dB."] }));
  else
    body.appendChild(tip("TRY THIS", "Tape, Drive +9 dB, Mix 50 %: a little density you feel more than hear (the Harmonics meter around −35 dB). For thin bass on phones, try Tube instead. Always compare with MATCH on.", true));
}

// Auto-Master's Stem EQ suggestion (never applied by Auto-Master itself): one click sets the knobs.
function stemSuggestion() {
  const s = amReport && amReport.ok && amReport.stemSuggest;
  if (!s || s.length !== 4) return null;
  const pending = s.some((v, k) => Math.abs(v - stemGainAt(k)) > 0.05) || !P.stemOn.getValue();
  return pending ? s : null;
}
function stemSuggestButton() {
  const s = stemSuggestion();
  if (!s) return null;
  const moves = [0, 1, 2, 3].filter((k) => Math.abs(s[k] - stemGainAt(k)) > 0.05);
  const b = document.createElement("button");
  b.type = "button";
  b.className = "pill pill-red stem-apply";
  b.textContent = "APPLY: " + (moves.length ? moves.map((k) => `${STEMS[k].toUpperCase()} ${fmtSigned(s[k])}`).join(", ") : "STEM EQ ON");
  b.addEventListener("click", () => {
    P.stemOn.setValue(true);
    s.forEach((v, k) => { const st = P.stemGain[k]; st.sliderDragStarted(); st.setNormalisedValue(scaledToNorm(st, v)); st.sliderDragEnded(); });
    stemCells.forEach((c) => c.refresh && c.refresh());
    onParamsChanged(); userEdited();
    toast("Stem EQ suggestion applied. Turn on MATCH and flip A/B to judge it.");
    renderLearn();
  });
  return b;
}

function renderStemLearn(body) {
  const on = !!P.stemOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const moves = stemMoves(), share = M.stemShare || [0, 0, 0, 0];
  const heard = STEMS.map((n, k) => `${n.toLowerCase()} ${share[k]} %`).join(", ");
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Stem EQ is off, so every part of the mix passes untouched."
    : !moves.length ? (playing ? `All four levels are at 0 dB, so the mix passes untouched. The model is listening: it hears ${heard}.`
                               : "All four levels are at 0 dB, so the mix passes untouched. Press play to see what the model hears.")
    : `${moves.map((k) => `${STEMS[k]} ${fmtSigned(stemGainAt(k))}`).join(", ")}: only the parts of the sound the model hears as ${moves.length > 1 ? "those stems" : STEMS[moves[0]].toLowerCase()} are turned ${moves.every((k) => stemGainAt(k) > 0) ? "up" : moves.every((k) => stemGainAt(k) < 0) ? "down" : "up or down"}; the rest stays where it was.`
      + (playing ? ` Right now it hears ${heard}.` : "")));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  const why = [
    (v) => v > 0 ? "Brings the vocal forward, like pushing its fader. Great for a vocal that sits a little low under the beat." : "Tucks the vocal back into the beat.",
    (v) => v > 0 ? "More kick, snare and hats. Adds punch and energy." : "Softer drums: the hits sit back and the music around them opens up.",
    (v) => v > 0 ? "More bass and 808. Check on small speakers and keep an eye on the Maximizer: bass is what limiters grab first." : "Less bass and 808. Useful when the low end swamps the mix.",
    (v) => v > 0 ? "More of everything else: synths, keys, guitars, pads and effects." : "Less of everything else, so vocals and drums stand out.",
  ];
  STEMS.forEach((n, k) => {
    const v = stemGainAt(k);
    body.appendChild(item(n, fmtSigned(v), Math.abs(v) < 0.05 ? "Untouched." : why[k](v)));
  });
  body.appendChild(item("Model Hears", "", "How the model divides the music right now, averaged over about a second. A part it barely hears won't move much when you turn it up or down."));
  const big = moves.filter((k) => Math.abs(stemGainAt(k)) > 6.05);
  if (on && !bypassed && big.length)
    body.appendChild(tip("HEADS UP", `${big.map((k) => STEMS[k]).join(" and ")} ${big.length > 1 ? "are" : "is"} moved more than 6 dB. Big moves can nudge the parts that share notes with ${big.length > 1 ? "them" : "it"} too.`, false,
      { need: "Only if something besides that part changes level.", steps: ["Turn on MATCH and flip A/B.", "Listen to the parts you didn't touch: do they jump or thin out?", "If they do, back the move off to about 6 dB, or make the rest of the change with Tonal EQ."] }));
  else if (on && !bypassed && moves.includes(3))
    body.appendChild(tip("GOOD TO KNOW", "Other moves touch the drums a little, because the model sometimes hears cymbals and percussion as Other.", true,
      { need: "No, unless the drums change level when you move Other.", steps: ["Turn on MATCH and flip A/B.", "If the hats or snare move with Other, keep Other within about 3 dB."] }));
  else
    body.appendChild(tip("TRY THIS", "Vocal sitting low? Vocals +2 dB is often all it takes. Flip MATCH on and A/B: the beat should stay put while the vocal steps forward.", true));
}

function renderUlLearn(body) {
  const on = !!P.ulOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const amount = ulAmountAt(), len = P.ulLength.getScaledValue(), focus = UL_FOCUS[comboIndex(P.ulFocus)];
  const sq = M.ulSquash || 0, lift = M.ulBoost || 0, scale = ulScale(sq);
  const sqText = sq ? `${fmtNum(sq)} dB` : "";
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "The Unlimiter is off, so your audio passes through untouched."
    : !playing ? "Press play: the Unlimiter listens for a second to measure how squashed your audio is."
    : !sq ? "Listening: it needs about a second of music to measure how squashed your audio is."
    : amount < 0.5 ? (scale > 0 ? `Your audio measures ${squashWord(sq)} (Squash ${sqText}). Turn Amount up to put the punch back.`
                                : `Your audio still has its punch (Squash ${sqText}), so there's nothing to undo. Leave Amount at 0.`)
    : scale === 0 ? `Your audio still has its punch (Squash ${sqText}), so the Unlimiter rests: it only puts back what a limiter took away.`
    : lift < 0.3 ? `Your audio measures ${squashWord(sq)} (Squash ${sqText}). Between hits nothing is lifted; it waits for the next hit.`
    : `Lifting each hit by up to ${fmtNum(lift)} dB: your audio measures ${squashWord(sq)} (Squash ${sqText}), so the hits get back some of the height the limiter took.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Squash", sq ? sqText : "–", "How far the peaks stand above the average level over the last 3 seconds. 13 dB or more is an open, unmastered mix; 8.5 dB or less is brickwalled (limited hard). The more squashed, the harder the Unlimiter works; on an open mix it does nothing."));
  body.appendChild(item("Amount", `${Math.round(amount)} %`, amount < 0.5 ? "Off." : `Puts back about ${Math.round(amount)} % of the punch the limiter took away, scaled by how squashed the audio measures.`));
  body.appendChild(item("Length", fmtMs(len), len < 12 ? "Short: just the click and snap of each hit comes back. Cleanest." : len > 30 ? "Long: the body of each hit comes back too. Bigger, but the music around the hits pumps a little." : "A middle setting: the attack and a little body of each hit."));
  body.appendChild(item("Focus", focus, focus === "Full" ? "Listens for every hit." : focus === "Kick" ? "Listens for the kick and 808 (below 150 Hz), so only those hits come back up." : "Listens for snares, claps and hats (above 1 kHz), so only those hits come back up."));
  body.appendChild(item("Good to know", "", "Like the limiter it undoes, it lifts the whole sound at each hit, not just the drum. It can't rebuild a waveform a clipper cut flat; it brings back the contrast between the hits and the rest."));
  if (on && !bypassed && playing && amount > 0.5 && sq && scale === 0)
    body.appendChild(tip("GOOD TO KNOW", "Your audio isn't squashed, so the Unlimiter has nothing to undo and stays out of the way.", true,
      { need: "No. It is resting on purpose.", steps: ["You can leave it as it is, or set Amount to 0."] }));
  else if (on && !bypassed && playing && lift > 9)
    body.appendChild(tip("HEADS UP", `It's lifting some hits by more than 9 dB. That much can make the music around the hits pump.`, false,
      { need: "Only if you hear the vocal or pads jump on each drum hit.", steps: ["Turn on MATCH and flip A/B.", "If B pumps: lower Amount by 20 %, or Length to about 10 ms.", "Or try Focus Kick so only the kick hits are lifted."] }));
  else
    body.appendChild(tip("TRY THIS", "Got a beat that's already loud and flat? Amount 75 %, Length 15 ms, Focus Full, then compare with MATCH on: the drums should hit harder while the level between them stays the same.", true));
}

function renderTsLearn(body) {
  const on = !!P.tsOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const amount = tsAmountAt(), range = P.tsRange.getScaledValue(), speed = TS_SPEEDS[comboIndex(P.tsSpeed)];
  const move = M.tsDb || 0, where = M.tsHz ? fmtHz(M.tsHz) : "";
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Tone Steady is off, so the tone is left exactly as it comes in."
    : amount < 0.5 ? "Amount is at 0, so nothing is changed. Turn Amount up to keep the tone steady through the song."
    : !playing ? "Press play: Tone Steady learns your song's own balance over the first seconds, then keeps each section close to it."
    : Math.abs(move) < 0.3 ? "This part of the song matches its own overall balance, so Tone Steady is resting."
    : `Right now it's ${move < 0 ? "pulling down" : "lifting"} the area around ${where} by ${fmtNum(Math.abs(move))} dB: this part drifted ${move < 0 ? "fuller" : "thinner"} there than the rest of the song.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Amount", `${Math.round(amount)} %`, amount < 0.5 ? "Off." : `Corrects about ${Math.round(amount)} % of each drift, beyond a 1 dB dead zone (3 dB in the highs, where hats and cymbals come and go), so small natural changes are left alone.`));
  body.appendChild(item("Range", `${MINUS}${range.toFixed(1)} / +${(range / 2).toFixed(1)} dB`, `Cuts go at most ${range.toFixed(1)} dB, boosts at most ${(range / 2).toFixed(1)} dB, so it can never reshape your mix, only steady it.`));
  body.appendChild(item("Speed", speed, speed === "Slow" ? "Follows whole sections: the gentlest, best for most masters."
    : speed === "Medium" ? "Follows phrases: catches a verse that gets muddier or a hook that gets harsher." : "Follows single notes and hits: strongest, can make the mix feel controlled."));
  body.appendChild(item("Reference", "", "Your song's own balance over the last ~10 seconds, so the Tonal EQ sets the tone and Tone Steady only keeps it. Quiet breakdowns are left alone."));
  if (on && !bypassed && amount > 70 && speed === "Fast")
    body.appendChild(tip("HEADS UP", "High Amount at Fast speed follows single notes. That can flatten moves you made on purpose, like a drop that's meant to hit with more bass.", false,
      { need: "Only if the big moments sound smaller with it on.", steps: ["Turn on MATCH and flip A/B through the drop or chorus.", "If the impact shrinks: Speed to Medium or Slow.", "Or lower Amount to about 40 %."] }));
  else
    body.appendChild(tip("TRY THIS", "Amount 40 %, Range 3 dB, Speed Slow: sections stay consistent without losing their character. Compare with MATCH on.", true));
}

function renderRtLearn(body) {
  const on = !!P.rtOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const amount = rtAmountAt(), sens = P.rtSens.getScaledValue(), rel = P.rtRelease.getScaledValue(), focus = RT_FOCUS[comboIndex(P.rtFocus)];
  const cut = M.rtCut || 0, where = M.rtHz ? fmtHz(M.rtHz) : "";
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Resonance Tamer is off, so nothing is cut."
    : amount < 0.5 ? "Amount is at 0, so nothing is cut. Turn Amount up to tame ringing tones."
    : !playing ? "Press play to see which ringing tones Resonance Tamer finds."
    : cut > -0.3 ? "Nothing rings out right now, so Resonance Tamer is resting."
    : `Right now it's cutting ${fmtNum(-cut)} dB around ${where}: a tone there rings ${-cut > 4 ? "clearly" : "a little"} above the sound around it.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Amount", `${Math.round(amount)} %`, amount < 0.5 ? "Off." : `How far each ringing tone is pulled down: at 100 % it is cut down to the Sensitivity threshold (no more than 12 dB). ${Math.round(amount)} % takes it part of the way.`));
  body.appendChild(item("Sensitivity", `${Math.round(sens)} %`, `Catches tones that stand ${fmtNum(12 - 9 * sens / 100, 1)} dB or more above their surroundings. Higher catches smaller ones.`));
  body.appendChild(item("Release", fmtMs(rel), rel < 50 ? "Lets go quickly: cuts follow every note; most transparent on busy mixes." : rel > 150 ? "Lets go slowly: smooth, steady taming of tones that keep coming back." : "A middle setting that suits most mixes."));
  body.appendChild(item("Focus", focus, focus === "Full" ? "Looks from 80 Hz to 16 kHz." : focus === "Boxiness" ? "Looks only at 150\u2013800 Hz, where boxy, muddy rings live." : "Looks only at 1.5\u20138 kHz, where harsh, piercing tones live."));
  if (on && !bypassed && playing && cut < -8)
    body.appendChild(tip("HEADS UP", `It's cutting more than 8 dB at ${where}. Deep cuts can make that area sound dull or hollow.`, false,
      { need: "Only if that area sounds dull or hollow to you.", steps: ["Turn on MATCH and flip A/B.", "If B sounds dull there: lower Amount by 20 %, or Sensitivity to about 40 %.", "If the ring is in your mix, an EQ cut on that one track fixes it at the source."] }));
  else
    body.appendChild(tip("TRY THIS", "Amount 50 %, Sensitivity 50 %, Focus Full. Then try Focus Harshness on a bright mix: hats and vocals get smoother without losing air.", true));
}

// Warnings go right under "What's happening" so they're seen without scrolling; friendly tips stay at the end.
function liftWarning(body) {
  const warn = body.querySelector(".l-tip:not(.ok)"), firstSep = body.querySelector(".l-sep");
  if (warn && firstSep) { body.insertBefore(warn, firstSep); warn.style.marginTop = "10px"; }
}

function renderClipperLearn(body) {
  const on = !!P.clipOn.getValue();
  const bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const level = P.clipLevel.getScaledValue();
  const ceiling = P.ceiling.getScaledValue();
  const soft = Math.round(P.clipSoftness.getScaledValue());
  const os = ["1x", "2x", "4x", "8x"][osIndex()];
  const peakClip = clipWindow.length ? Math.min(...clipWindow) : 0;
  const avgClip = clipWindow.length ? clipWindow.reduce((a, b) => a + b, 0) / clipWindow.length : 0;
  const peakGr = maxGrRecent();
  const lvl = `${level > 0.049 ? "+" : ""}${fmtNum(level, 1)} dB`;

  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the chain."));
  else if (!on) body.appendChild(para("l-p", playing
      ? `The Soft Clipper is off, so the Maximizer is doing all the peak control (up to ${fmtNum(-peakGr)} dB right now). Turn it on to let it shave the sharpest drum hits first.`
      : "The Soft Clipper is off. It sits between Drive and the Maximizer and shaves the sharpest peaks, so the limiter has less to do."));
  else if (!playing) body.appendChild(para("l-p", "Press play. I'll show how much the clipper is shaving and how that changes the Maximizer's work."));
  else body.appendChild(para("l-p", peakClip < -0.1
      ? `Shaving the sharpest peaks (kicks, snares, claps) by about ${fmtNum(-avgClip)} dB on average, up to ${fmtNum(-peakClip)} dB on the biggest hits, before the Maximizer, which is now taking up to ${fmtNum(-peakGr)} dB.`
      : "Nothing is reaching the clip point yet, so the clipper is idle. Lower Clip Level or turn up Drive to make it work."));

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Clip Level", lvl,
    level > 0.05
      ? `Only peaks more than ${fmtNum(level, 1)} dB over the ${fmtNum(ceiling, 1)} dBTP ceiling get clipped; the Maximizer handles everything below that. Gentle.`
      : level > -0.05
        ? `Clipping starts right at the ${fmtNum(ceiling, 1)} dBTP ceiling: the clipper takes the drum spikes and the Maximizer just polishes what's left.`
        : `Clipping starts ${fmtNum(-level, 1)} dB below the ceiling, then the level comes back up by ${fmtNum(-level, 1)} dB so peaks still reach ${fmtNum(ceiling, 1)} dBTP. Lower = denser and louder, but more distortion.`));
  body.appendChild(item("Softness", `${soft} %`,
    soft < 25 ? "Hard knee: the most transparent on short drum hits, but it can crunch on sustained bass."
              : soft > 75 ? "Very soft knee: starts bending up to 6 dB before the clip point. Warm and smooth, but it softens the drum attack."
                          : "Medium knee: rounds the peaks gently, so you hear punch rather than distortion."));
  body.appendChild(item("Oversampling", os,
    os === "1x" ? "No oversampling: clipping creates inharmonic 'aliasing' tones that sound harsh and cheap. Use 4x or 8x for masters."
                : `Runs the clipper at ${os} the sample rate so the new harmonics don't fold back as harsh tones. ${os === "8x" ? "Cleanest, uses the most CPU." : "A good balance of clean and CPU."}`));

  if (!on || bypassed || !playing) {
    body.appendChild(tip("TIP", "Turn the clipper on at 0 dB, then lower Clip Level until the Clipping meter shows 1–3 dB on the drums. Compare with the clipper off at the same Drive: it should be denser and louder, with the same peak level.", true));
  } else if (peakClip > -0.1) {
    const quiet = M.integrated > -99 && M.integrated < -16;
    body.appendChild(tip("NOT CLIPPING YET",
      (quiet ? `Your master is still quiet (${fmtNum(M.integrated)} LUFS integrated), so the peaks never reach the clip point. ` : "The peaks aren't reaching the clip point. ") +
      `Right now Clip Level only adds ${fmtNum(Math.max(0, -level), 1)} dB of level. Raise Drive on the Maximizer until the Clipping meter shows 1–3 dB on the drums.`, true));
  } else if (avgClip < -3) {
    body.appendChild(tip("FIX IT IN THE MIX",
      `That's heavy clipping (about ${fmtNum(-avgClip)} dB on average). Snares and claps can turn crunchy.`, false,
      { need: "Only if the snare or claps sound crunchy.", steps: ["Raise Clip Level by 1–2 dB, or raise Softness.", "Best fix, in the mix: clip or limit the drum bus by 2–3 dB, then run Auto-Master again."] }));
  } else if (os === "1x") {
    body.appendChild(tip("HEADS UP", "At 1x the clipper adds aliasing (harsh tones that weren't in your mix).", false,
      { need: "Yes.", steps: ["Click 4x under Oversampling."] }));
  } else if (peakGr < -6) {
    body.appendChild(tip("TRY THIS", "The Maximizer is still working hard. Lower Clip Level a little so the clipper takes more of the peaks and the limiter less.", true));
  } else {
    body.appendChild(tip("GOOD BALANCE", "The clipper handles the drum spikes, the Maximizer handles the rest. That's how loud masters keep their punch.", true));
  }
}

// ---------------------------------------------------------------------------------------------
// Header controls
function setLearn(on) {
  learnOn = on;
  const b = $("learn-toggle");
  b.textContent = on ? "LEARN ON" : "LEARN OFF";
  b.className = "pill learn-toggle " + (on ? "pill-red" : "pill-blue");
  b.setAttribute("aria-pressed", String(on));
  renderLearn();
}
$("learn-toggle").addEventListener("click", () => setLearn(!learnOn));

// Level-matched A/B
function refreshMatch() {
  const on = !!P.levelMatch.getValue();
  const b = $("btn-match");
  b.className = "pill match " + (on ? "pill-red" : "pill-blue");
  b.setAttribute("aria-pressed", String(on));
  b.textContent = on ? `MATCH ${M.matchDb < -0.05 ? fmtNum(M.matchDb) : "0.0"}` : "MATCH";
}
$("btn-match").addEventListener("click", () => {
  const on = !P.levelMatch.getValue();
  P.levelMatch.setValue(on);
  refreshMatch();
  toast(on ? "MATCH on: A and B play at the same loudness (the louder one is turned down), so you compare tone and punch, not volume."
           : "MATCH off: B plays the master at its real loudness.");
});
P.levelMatch.valueChangedEvent.addListener(refreshMatch);

function refreshAB() {
  const bypassed = !!P.bypass.getValue();
  $("btn-a").classList.toggle("on", bypassed);
  $("btn-b").classList.toggle("on", !bypassed);
  $("btn-a").setAttribute("aria-pressed", String(bypassed));
  $("btn-b").setAttribute("aria-pressed", String(!bypassed));
  refreshHeadButton();
}
// The button in the module header: global bypass for the Maximizer, power for the Soft Clipper.
function refreshHeadButton() {
  const bp = $("btn-bypass");
  if (selected === "dyn" || selected === "sat") {
    const on = !!(selected === "dyn" ? P.dynOn : P.satOn).getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = (selected === "dyn" ? "DYNAMICS " : "SAT ") + (on ? "ON" : "OFF");
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "ts" || selected === "rt") {
    const on = !!(selected === "ts" ? P.tsOn : P.rtOn).getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = (selected === "ts" ? "TONE STEADY " : "TAMER ") + (on ? "ON" : "OFF");
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "stem") {
    const on = !!P.stemOn.getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = on ? "STEM EQ ON" : "STEM EQ OFF";
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "img") {
    const on = !!P.imgOn.getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = on ? "IMAGER ON" : "IMAGER OFF";
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "bass") {
    const on = !!P.bassOn.getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = on ? "BASS ON" : "BASS OFF";
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "eq") {
    const on = eqEnabled();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = on ? "EQ ON" : "EQ OFF";
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else if (selected === "clip") {
    const on = !!P.clipOn.getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = on ? "CLIPPER ON" : "CLIPPER OFF";
    bp.setAttribute("aria-pressed", String(on));
    $("knob-row").classList.toggle("dimmed", !on);
  } else {
    const bypassed = !!P.bypass.getValue();
    bp.className = "pill bypass " + (bypassed ? "pill-red" : "pill-blue");
    bp.textContent = bypassed ? "BYPASSED" : "BYPASS";
    bp.setAttribute("aria-pressed", String(bypassed));
    $("knob-row").classList.remove("dimmed");
  }
}
$("btn-a").addEventListener("click", () => { P.bypass.setValue(true); onParamsChanged(); userEdited(); });
$("btn-b").addEventListener("click", () => { P.bypass.setValue(false); onParamsChanged(); userEdited(); });
$("btn-bypass").addEventListener("click", () => {
  if (selected === "clip") P.clipOn.setValue(!P.clipOn.getValue());
  else if (selected === "eq") P.eqOn.setValue(!P.eqOn.getValue());
  else if (selected === "bass") P.bassOn.setValue(!P.bassOn.getValue());
  else if (selected === "img") P.imgOn.setValue(!P.imgOn.getValue());
  else if (selected === "dyn") P.dynOn.setValue(!P.dynOn.getValue());
  else if (selected === "sat") P.satOn.setValue(!P.satOn.getValue());
  else if (selected === "stem") P.stemOn.setValue(!P.stemOn.getValue());
  else if (selected === "ts") P.tsOn.setValue(!P.tsOn.getValue());
  else if (selected === "rt") P.rtOn.setValue(!P.rtOn.getValue());
  else P.bypass.setValue(!P.bypass.getValue());
  onParamsChanged();
  userEdited();
});
P.bypass.valueChangedEvent.addListener(onParamsChanged);
P.clipOn.valueChangedEvent.addListener(onParamsChanged);
P.eqOn.valueChangedEvent.addListener(onParamsChanged);
P.bassOn.valueChangedEvent.addListener(onParamsChanged);
P.imgOn.valueChangedEvent.addListener(onParamsChanged);
P.dynOn.valueChangedEvent.addListener(onParamsChanged);
P.satOn.valueChangedEvent.addListener(onParamsChanged);
P.stemOn.valueChangedEvent.addListener(onParamsChanged);
P.tsOn.valueChangedEvent.addListener(onParamsChanged);
P.rtOn.valueChangedEvent.addListener(onParamsChanged);
for (const s of [...P.dynX, ...P.dynRatio, ...P.dynGain]) s.valueChangedEvent.addListener(() => { dynPicker.refresh(); updateLiveReadouts(); });

function togglePopover(id) {
  for (const p of ["pop-settings", "pop-help"]) $(p).hidden = p === id ? !$(p).hidden : true;
}
$("btn-settings").addEventListener("click", (e) => { e.stopPropagation(); togglePopover("pop-settings"); });
$("btn-help").addEventListener("click", (e) => { e.stopPropagation(); togglePopover("pop-help"); });
document.addEventListener("click", (e) => {
  if (!e.target.closest(".popover")) togglePopover(null);
});

const chkAuto = $("chk-autoreset"), chkAdaptive = $("chk-adaptive");
chkAuto.addEventListener("change", () => P.autoreset.setValue(chkAuto.checked));
chkAdaptive.addEventListener("change", () => { P.adaptive.setValue(chkAdaptive.checked); onParamsChanged(); userEdited(); });
P.autoreset.valueChangedEvent.addListener(() => (chkAuto.checked = !!P.autoreset.getValue()));
const chkChange = $("chk-resetchange");
chkChange.addEventListener("change", () => P.resetOnChange.setValue(chkChange.checked));
P.resetOnChange.valueChangedEvent.addListener(() => (chkChange.checked = !!P.resetOnChange.getValue()));

function restartMeasurement() { resetMeter(); grWindow = []; clipWindow = []; tameWindow = []; punchWindow = []; }
$("btn-reset").addEventListener("click", () => { restartMeasurement(); toast("Loudness measurement reset."); });
// After a control change only the long-term readings start over; Short-term keeps running.
function restartIntegrated() { resetMeter("stats"); }

// Integrated is an average over everything since it last started. After you change a control,
// start it over (once the control settles) so it describes the settings you're hearing now.
let editTimer = null;
function userEdited() {
  if (!P.resetOnChange.getValue()) return;
  clearTimeout(editTimer);
  editTimer = setTimeout(restartIntegrated, 700);
}

let toastTimer = null;
function toast(text) {
  const t = $("toast");
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 2600);
}
// ---------------------------------------------------------------------------------------------
// Auto-Master: genre / intensity / target, the listen -> analyse -> apply flow, the report, undo.
const comboIndex = (st) => { const n = st.properties.choices.length || 3; return clamp(Math.round(st.getChoiceIndex ? st.getChoiceIndex() : st.value * (n - 1)), 0, n - 1); };
const GENRES = ["Trap / Hip-Hop", "R&B", "Pop", "EDM", "Rock", "Acoustic"];
function refreshAmChoices() {
  const g = comboIndex(P.amGenre);
  $("genre-name").textContent = (P.amGenre.properties.choices[g]) || GENRES[g];
  for (const [rowId, st] of [["row-intensity", P.amIntensity], ["row-target", P.amTarget]]) {
    const idx = comboIndex(st);
    $(rowId).querySelectorAll("button[data-i]").forEach((b) => {
      const on = Number(b.dataset.i) === idx;
      b.className = "pill " + (on ? "pill-red" : "pill-blue");
      b.setAttribute("aria-pressed", String(on));
    });
  }
}
function stepGenre(d) {
  const n = P.amGenre.properties.choices.length || GENRES.length;
  P.amGenre.setChoiceIndex((comboIndex(P.amGenre) + d + n) % n);
  refreshAmChoices();
}
$("genre-prev").addEventListener("click", () => stepGenre(-1));
$("genre-next").addEventListener("click", () => stepGenre(1));
for (const [rowId, st] of [["row-intensity", P.amIntensity], ["row-target", P.amTarget]])
  $(rowId).querySelectorAll("button[data-i]").forEach((b) =>
    b.addEventListener("click", () => { st.setChoiceIndex(Number(b.dataset.i)); refreshAmChoices(); }));
for (const st of [P.amGenre, P.amIntensity, P.amTarget]) {
  st.valueChangedEvent.addListener(refreshAmChoices);
  st.propertiesChangedEvent.addListener(refreshAmChoices);
}

let amRequested = false;       // this session pressed Auto-Master (so a new report opens in Learn)
let amSeenVersion = -1;
let amOpenedTime = null;       // the report already opened for the user (by its timestamp)
$("btn-automaster").addEventListener("click", async () => {
  if (M.amState === 1) { amCancel(); amRequested = false; toast("Auto-Master stopped listening."); return; }
  if (M.amState === 2) return;
  const started = await amStart();
  if (started === false) {
    toast("Polisher isn't ready yet: press play in your DAW once, then click Auto-Master again.");
    return;
  }
  amRequested = true;
  M.amState = 1; M.amProgress = 0; M.amHearing = false;
  updateAmButton();
});
function updateAmButton() {
  const btn = $("btn-automaster"), title = $("am-title"), sub = $("am-sub");
  const busy = M.amState === 1 || M.amState === 2;
  btn.classList.toggle("busy", busy);
  $("am-progress").hidden = M.amState !== 1;
  $("am-progress-fill").style.width = (clamp(M.amProgress, 0, 1) * 100).toFixed(0) + "%";
  if (M.amState === 1) {
    title.textContent = "LISTENING";
    sub.textContent = M.amHearing ? `${Math.round(M.amProgress * 12)} / 12 s · click to stop` : "Press play: the loudest part of your song";
  } else if (M.amState === 2) {
    title.textContent = "THINKING";
    sub.textContent = "Choosing every setting (a few seconds)…";
  } else {
    title.textContent = "AUTO-MASTER";
    sub.textContent = amReport && amReport.ok ? "Done · click to run again" : "Listens to your mix and sets every module";
  }
  const undo = $("btn-undo");
  undo.disabled = !M.amUndo;
  undo.classList.toggle("is-disabled", !M.amUndo);
}
$("btn-undo").addEventListener("click", async () => {
  if (!M.amUndo) return;
  await amUndo();
  toast("Undone: every setting is back to how it was before Auto-Master.");
  refreshAllCells();
});

async function fetchReport(openIt) {
  let text = "";
  try { text = await amGetReport(); } catch (e) { text = ""; }
  try { amReport = text ? JSON.parse(text) : null; } catch (e) { amReport = null; }
  // Open a report only once, and only when it's a new one from a run the user started here.
  if (openIt && amRequested && amReport && amReport.time !== amOpenedTime) {
    amOpenedTime = amReport.time;
    amRequested = false;
    learnView = "report";
    toast(amReport.ok ? "Auto-Master set every module. Learn explains each choice."
      : amReport.notAFullMix ? "That sounds like one track, not a full mix. Nothing was changed. See Learn."
      : "Auto-Master couldn't hear enough music. See Learn.");
  }
  if (!amReport && learnView === "report") learnView = "module";
  refreshAllCells();
  updateAmButton();
  renderLearn();
}
// Knobs show values pushed by the plug-in; after Auto-Master or Undo, redraw them all.
function refreshAllCells() {
  for (const c of [...maximizerCells, ...clipperCells, ...eqCells, ...bassCells, ...imagerCells, dynPicker, ...dynCellsByBand.flat(), ...satCells]) if (c.refresh) c.refresh();
  onParamsChanged();
}

const AM_MODULES = [["ul", "UNLIMITER"], ["eq", "TONAL EQ"], ["stem", "STEM EQ"], ["ts", "TONE STEADY"], ["rt", "RESONANCE TAMER"], ["bass", "BASS CONTROL"], ["dyn", "DYNAMICS"], ["img", "STEREO IMAGER"], ["sat", "SATURATION"], ["clip", "SOFT CLIPPER"], ["max", "MAXIMIZER"]];
function notesAsTips(body) {
  for (const raw of amReport.notes || []) {
    // "TITLE: text" plus optional lines "NEED: …" and "STEP: …" (the plan).
    const lines = raw.split("\n");
    const n = lines[0];
    const cut = n.indexOf(":");
    const title = cut > 0 && cut < 24 ? n.slice(0, cut) : "HEADS UP";
    const text = cut > 0 && cut < 24 ? n.slice(cut + 1).trim() : n;
    const need = lines.find((l) => l.startsWith("NEED:"));
    const steps = lines.filter((l) => l.startsWith("STEP:")).map((l) => l.slice(5).trim());
    body.appendChild(tip(title, text.charAt(0).toUpperCase() + text.slice(1), false,
                         need ? { need: need.slice(5).trim(), steps } : undefined));
  }
}
function renderReport(body) {
  const r = amReport;
  if (!r.ok) {
    body.appendChild(head("AUTO-MASTER"));
    body.appendChild(para("l-p", r.summary));
    body.appendChild(tip(r.tipTitle || "TRY AGAIN", r.tip || "Start playback on your chorus or drop, then press Auto-Master. The counter only runs while it hears music.", true));
    return;
  }
  body.appendChild(head("AUTO-MASTER REPORT"));
  const meta = para("l-meta", `${r.genre} · ${r.intensity} · ${r.target}`);
  body.appendChild(meta);
  body.appendChild(para("l-p", r.summary));
  notesAsTips(body);
  for (const [id, label] of AM_MODULES) {
    const list = (r.reasons || []).filter((x) => x.module === id);
    if (!list.length) continue;
    body.appendChild(sep());
    body.appendChild(head(label));
    list.forEach((x) => body.appendChild(item(x.control, x.value, x.why)));
    if (id === "stem") { const b = stemSuggestButton(); if (b) body.appendChild(b); }
  }
  body.appendChild(tip("COMPARE IT", "Turn on MATCH, then press A for your original mix and B for the master: both play at the same loudness, so you hear only what the master changed. The undo arrow at the top puts every setting back.", true));
}

// Current value of a parameter by id, in the same units the report stores.
function paramValue(id) {
  if (id.startsWith("eqGain")) return eqGainAt(Number(id.slice(6)) - 1);
  if (id.startsWith("eqFreq")) return eqFreqAt(Number(id.slice(6)) - 1);
  const map = { drive: P.drive, ceiling: P.ceiling, release: P.release, clipLevel: P.clipLevel, clipSoftness: P.clipSoftness,
                bassMono: P.bassMono, bassTame: P.bassTame, bassPunch: P.bassPunch,
                imgLow: P.imgLow, imgMid: P.imgMid, imgHigh: P.imgHigh, satDrive: P.satDrive, satMix: P.satMix,
                ulAmount: P.ulAmount, ulLength: P.ulLength, tsAmount: P.tsAmount, tsRange: P.tsRange, rtAmount: P.rtAmount, rtSens: P.rtSens, rtRelease: P.rtRelease,
                stemVoc: P.stemGain[0], stemDrums: P.stemGain[1], stemBass: P.stemGain[2], stemOther: P.stemGain[3] };
  const band = /^dyn(X|Thr|Ratio|Atk|Rel|Gain)(\d)$/.exec(id);
  if (band) return P["dyn" + band[1]][Number(band[2]) - 1].getScaledValue();
  if (id === "satMode") return satModeIndex();
  if (id === "tsSpeed") return comboIndex(P.tsSpeed);
  if (id === "ulFocus") return comboIndex(P.ulFocus);
  if (id === "rtFocus") return comboIndex(P.rtFocus);
  if (map[id]) return map[id].getScaledValue();
  const toggles = { eqOn: P.eqOn, bassOn: P.bassOn, clipOn: P.clipOn, adaptive: P.adaptive, bypass: P.bypass,
                    dynOn: P.dynOn, dynAdaptive: P.dynAdaptive, imgOn: P.imgOn, satOn: P.satOn, tsOn: P.tsOn, rtOn: P.rtOn, ulOn: P.ulOn };
  if (toggles[id]) return toggles[id].getValue() ? 1 : 0;
  if (id === "clipOversampling") return osIndex();
  return null;
}
const MODULE_PARAMS = { ul: /^ul/, stem: /^stem/, ts: /^ts/, rt: /^rt/, eq: /^eq/, bass: /^bass/, dyn: /^dyn/, img: /^img/, sat: /^sat/, clip: /^clip/, max: /^(drive|ceiling|release|adaptive)$/ };
function moduleUnchanged(module) {
  const values = (amReport && amReport.values) || {};
  return Object.entries(values).every(([id, v]) => {
    if (!MODULE_PARAMS[module].test(id)) return true;
    const cur = paramValue(id);
    if (cur === null) return true;
    const tol = id.startsWith("eqFreq") || id.startsWith("dynX") ? Math.max(1, v * 0.01) : 0.06;
    return Math.abs(cur - v) <= tol;
  });
}
function prependAmSection(body, module) {
  if (!amReport || !amReport.ok) return;
  const list = (amReport.reasons || []).filter((x) => x.module === module);
  if (!list.length) return;
  const frag = document.createDocumentFragment();
  const link = document.createElement("button");
  link.type = "button";
  link.className = "l-link";
  link.textContent = "\u2190 AUTO-MASTER REPORT";
  link.addEventListener("click", () => { learnView = "report"; renderLearn(); });
  frag.appendChild(link);
  frag.appendChild(head("WHY AUTO-MASTER CHOSE THIS"));
  if (moduleUnchanged(module)) {
    list.forEach((x) => frag.appendChild(item(x.control, x.value, x.why)));
  } else {
    frag.appendChild(para("l-changed", "You've changed this module since Auto-Master set it, so its reasons no longer describe what you hear. Undo (top) puts Auto-Master's settings back."));
  }
  frag.appendChild(sep());
  body.prepend(frag);
}

function onParamsChanged() {
  refreshAB();
  drawEq();
  chkAdaptive.checked = !!P.adaptive.getValue();
  chkAuto.checked = !!P.autoreset.getValue();
  chkChange.checked = !!P.resetOnChange.getValue();
  updateLiveReadouts();
  scheduleLearn();
}
let learnQueued = false;
function scheduleLearn() {
  if (learnQueued) return;
  learnQueued = true;
  requestAnimationFrame(() => { learnQueued = false; renderLearn(); });
}

// ---------------------------------------------------------------------------------------------
// Spectrum
const W = 860, H = 200, POINTS = 96, DB_TOP = -12, DB_BOTTOM = -84;
const xForFreq = (f) => (Math.log10(f / 20) / 3) * W;
function buildSpectrumGrid() {
  const g = $("grid");
  const freqs = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  let lines = "";
  for (let i = 1; i < 4; i++) lines += `<line x1="0" y1="${(H * i) / 4}" x2="${W}" y2="${(H * i) / 4}"/>`;
  freqs.slice(1, -1).forEach((f) => { const x = xForFreq(f).toFixed(1); lines += `<line x1="${x}" y1="0" x2="${x}" y2="${H}"/>`; });
  g.innerHTML = lines;

  $("freq-labels").innerHTML = freqs.map((f) =>
    `<span style="left:${(xForFreq(f) / W * 100).toFixed(2)}%">${f >= 1000 ? f / 1000 + "k" : f}</span>`).join("");
  // Label centres (Hz), spaced so the names don't collide.
  const bands = [["SUB BASS", 36], ["BASS", 115], ["LOW MID", 360], ["MID", 950], ["UPPER MID", 2300], ["PRESENCE", 5600], ["AIR", 13000]];
  $("band-labels").innerHTML = bands.map(([n, f]) =>
    `<span style="left:${(xForFreq(f) / W * 100).toFixed(2)}%">${n}</span>`).join("");
  $("db-scale").innerHTML = [0, 1, 2, 3, 4].map((i) => `<span>${fmtNum(DB_TOP + ((DB_BOTTOM - DB_TOP) * i) / 4, 0)}</span>`).join("");
}
function spectrumPath(values) {
  if (!values || !values.length) return "";
  let d = "";
  for (let i = 0; i < values.length; i++) {
    const x = (i / (values.length - 1)) * W;
    const y = clamp(((DB_TOP - values[i]) / (DB_TOP - DB_BOTTOM)) * H, 0, H);
    d += (i ? "L" : "M") + x.toFixed(1) + "," + y.toFixed(1);
  }
  return d;
}

// ---------------------------------------------------------------------------------------------
// Meters
const meterDefs = [
  { key: "integrated", label: "INTEGRATED", unit: "LUFS", lo: -40, hi: 0 },
  { key: "shortTerm", label: "SHORT-TERM", unit: "LUFS", lo: -40, hi: 0 },
  { key: "truePeak", label: "TRUE PEAK", unit: "dBTP", lo: -30, hi: 0, hot: (v) => v > P.ceiling.getScaledValue() + 0.05 },
  { key: "range", label: "RANGE", unit: "LU", lo: 0, hi: 20, plain: true },
];
function buildMeters() {
  $("meters").innerHTML = meterDefs.map((d) => `
    <div class="meter"><div class="meter-top"><span class="meter-label">${d.label}</span><span class="meter-val mono" id="mv-${d.key}"></span></div>
    <div class="screen meter-track"><div class="meter-fill" id="mf-${d.key}"></div></div></div>`).join("");
}
function updateMeters() {
  for (const d of meterDefs) {
    const v = M[d.key];
    $("mv-" + d.key).textContent = d.plain ? `${v.toFixed(1)} ${d.unit}`
      : d.key === "integrated" && v <= -99 ? `— ${d.unit}` : fmtDb(v, d.unit);
    const pct = v <= -99 ? 0 : clamp(((v - d.lo) / (d.hi - d.lo)) * 100, 0, 100);
    const fill = $("mf-" + d.key);
    fill.style.width = pct.toFixed(1) + "%";
    fill.classList.toggle("hot", !!(d.hot && d.hot(v)));
  }
}

const hold = { l: -100, r: -100, lT: 0, rT: 0, peak: -100, peakT: 0 };
function updateOutputMeter() {
  const now = performance.now();
  const levelPct = (db) => (db <= -60 ? 0 : clamp(((db + 60) / 60) * 100, 0, 100));
  for (const [ch, val] of [["l", M.peakL], ["r", M.peakR]]) {
    const cur = hold[ch + "Disp"] ?? -100;
    const disp = val > cur ? val : Math.max(val, cur - 1.2);   // smooth fall
    hold[ch + "Disp"] = disp;
    $("bar-" + ch).style.height = (100 - levelPct(disp)).toFixed(1) + "%";
    if (val > hold[ch] || now - hold[ch + "T"] > 1500) { hold[ch] = val; hold[ch + "T"] = now; }
    $("hold-" + ch).style.bottom = levelPct(hold[ch]).toFixed(1) + "%";
  }
  if (M.peakRecent > hold.peak || now - hold.peakT > 2000) { hold.peak = M.peakRecent; hold.peakT = now; }
  $("out-peak").textContent = hold.peak <= -99 ? `${MINUS}inf` : fmtNum(hold.peak, 1);
}

// After Auto-Master looked at a module and chose to leave it untouched, say so instead of "Neutral".
const amKept = (module) => !!(amReport && amReport.ok && (amReport.reasons || []).some((x) => x.module === module) && moduleUnchanged(module));
const neutralText = (module) => (amKept(module) ? "Checked \u00b7 kept" : "Neutral");
const dynWorst = () => Math.min(0, ...[0, 1, 2, 3].map((k) => (dynRatioAt(k) > 1.005 ? M.dynGr[k] || 0 : 0)));
function updateLiveReadouts() {
  const gr = M.gr;
  const chip = $("module-chip");
  const m = modules.find((x) => x.id === selected);
  const clipOn = !!P.clipOn.getValue();
  const clipText = M.clip > -0.05 ? "0.0" : fmtNum(M.clip);
  chip.hidden = !m.live;
  chip.textContent = selected === "clip" ? (clipOn ? `CLIP ${clipText} dB` : "OFF")
                   : selected === "eq" ? (!eqEnabled() ? "OFF" : eqIsFlat() ? "FLAT" : "SHAPING")
 : selected === "img" ? (!P.imgOn.getValue() ? "OFF" : `CORR ${M.corr < 0 ? MINUS : "+"}${Math.abs(M.corr).toFixed(2)}`)
                   : selected === "dyn" ? (!P.dynOn.getValue() ? "OFF" : dynIsNeutral() ? "1:1" : `GR ${dynWorst() > -0.05 ? "0.0" : fmtNum(dynWorst())} dB`)
                   : selected === "sat" ? (!P.satOn.getValue() ? "OFF" : satIsNeutral() ? "CLEAN" : SAT_MODES[satModeIndex()].toUpperCase())
                   : selected === "ul" ? (!P.ulOn.getValue() ? "OFF" : ulIsNeutral() ? "IDLE" : !M.ulSquash ? "LISTENING" : (M.ulBoost || 0) < 0.3 ? "RESTING" : `LIFT +${fmtNum(M.ulBoost)} dB`)
                   : selected === "ts" ? (!P.tsOn.getValue() ? "OFF" : tsIsNeutral() ? "IDLE" : Math.abs(M.tsDb || 0) < 0.3 ? "RESTING" : `STEADY ${fmtNum(M.tsDb)} dB`)
                   : selected === "rt" ? (!P.rtOn.getValue() ? "OFF" : rtIsNeutral() ? "IDLE" : (M.rtCut || 0) > -0.3 ? "RESTING" : `CUT ${fmtNum(M.rtCut)} dB`)
                   : selected === "stem" ? (!P.stemOn.getValue() ? "OFF" : stemIsNeutral() ? "FLAT" : `${stemMoves().length} MOVE${stemMoves().length > 1 ? "S" : ""}`)
                   : selected === "bass" ? (!P.bassOn.getValue() ? "OFF" : `WIDTH ${Math.round(M.bassWidth || 0)} %`)
                   : `GR ${gr > -0.05 ? "0.0" : fmtNum(gr)} dB`;
  const r = $("read-max");
  if (r) r.textContent = P.bypass.getValue() ? "Bypassed" : `GR ${gr > -0.05 ? "0.0" : fmtNum(gr)} dB`;
  const bassOn = !!P.bassOn.getValue();
  const tameSet = P.bassTame.getScaledValue() > 0.5, punchSet = P.bassPunch.getScaledValue() > 0.5;
  const rb = $("read-bass");
  if (rb) {
    const parts = [];
    if (bassMonoOn()) parts.push(parts.length || tameSet || punchSet ? "Mono" : `Mono ${Math.round(bassMonoHz())} Hz`);
    if (tameSet) parts.push("Tame");
    if (punchSet) parts.push("Punch");
    rb.textContent = P.bypass.getValue() ? "Bypassed" : !bassOn ? "Off" : parts.length ? parts.join("\u00b7") : "Neutral";
  }
  const tf = $("tamemeter-fill");
  if (tf) {
    tf.style.width = (bassOn && tameSet ? clamp(-M.tame / 12, 0, 1) * 100 : 0).toFixed(1) + "%";
    $("tamemeter-val").textContent = !bassOn || !tameSet ? "off" : `${M.tame < -0.05 ? fmtNum(M.tame) : "0.0"} dB`;
    $("widthmeter-fill").style.width = clamp(M.bassWidth || 0, 0, 100).toFixed(0) + "%";
    $("widthmeter-val").textContent = `${Math.round(M.bassWidth || 0)} % stereo`;
  }
  const ri = $("read-img");
  const imgW = [P.imgLow, P.imgMid, P.imgHigh].map((x) => x.getScaledValue());
  const imgNeutral = imgW.every((v) => Math.abs(v - 100) < 0.5);
  if (ri) ri.textContent = P.bypass.getValue() ? "Bypassed" : !P.imgOn.getValue() ? "Off" : imgNeutral ? neutralText("img") : imgW.map((v) => Math.round(v)).join("/") + " %";
  const cf = $("corr-fill");
  if (cf) {
    cf.style.width = (((clamp(M.corr, -1, 1) + 1) / 2) * 100).toFixed(0) + "%";
    $("corr-val").textContent = `${M.corr < 0 ? MINUS : "+"}${Math.abs(M.corr).toFixed(2)}`;
  }
  const rd = $("read-dyn");
  if (rd) rd.textContent = P.bypass.getValue() ? "Bypassed" : !P.dynOn.getValue() ? "Off" : dynIsNeutral() ? neutralText("dyn")
    : `${[0, 1, 2, 3].filter(dynBandActive).length} band${[0, 1, 2, 3].filter(dynBandActive).length > 1 ? "s" : ""} \u00b7 ${dynWorst() > -0.05 ? "0.0" : fmtNum(dynWorst())} dB`;
  if (selected === "dyn") dynPicker.refresh();
  const rul = $("read-ul");
  if (rul) rul.textContent = P.bypass.getValue() ? "Bypassed" : !P.ulOn.getValue() ? "Off" : ulIsNeutral() ? neutralText("ul")
    : `${Math.round(ulAmountAt())} % \u00b7 ${UL_FOCUS[comboIndex(P.ulFocus)]}`;
  const usf = $("ulsquash-fill");
  if (usf) {
    const sq = M.ulSquash || 0;
    usf.style.width = (sq ? clamp((16 - sq) / 10, 0, 1) * 100 : 0).toFixed(1) + "%";
    $("ulsquash-val").textContent = !sq ? "listening" : `${fmtNum(sq)} dB \u00b7 ${squashWord(sq)}`;
    const v = ulIsNeutral() || P.bypass.getValue() ? 0 : M.ulBoost || 0;
    $("ulmeter-fill").style.width = (clamp(v / 12, 0, 1) * 100).toFixed(1) + "%";
    $("ulmeter-val").textContent = v < 0.3 ? "resting" : `+${v.toFixed(1)} dB`;
  }
  const rts = $("read-ts");
  if (rts) rts.textContent = P.bypass.getValue() ? "Bypassed" : !P.tsOn.getValue() ? "Off" : tsIsNeutral() ? neutralText("ts")
    : `${Math.round(tsAmountAt())} % \u00b7 ${MINUS}${P.tsRange.getScaledValue().toFixed(1)} dB`;
  const rrt = $("read-rt");
  if (rrt) rrt.textContent = P.bypass.getValue() ? "Bypassed" : !P.rtOn.getValue() ? "Off" : rtIsNeutral() ? neutralText("rt")
    : `${Math.round(rtAmountAt())} % \u00b7 ${RT_FOCUS[comboIndex(P.rtFocus)]}`;
  const tsf = $("tsmeter-fill");
  if (tsf) {
    const v = tsIsNeutral() || P.bypass.getValue() ? 0 : Math.abs(M.tsDb || 0);
    tsf.style.width = (clamp(v / 6, 0, 1) * 100).toFixed(1) + "%";
    $("tsmeter-val").textContent = v < 0.3 ? "resting" : `${M.tsDb < 0 ? MINUS : "+"}${v.toFixed(1)} dB @ ${fmtHz(M.tsHz || 0)}`;
  }
  const rtf = $("rtmeter-fill");
  if (rtf) {
    const v = rtIsNeutral() || P.bypass.getValue() ? 0 : -(M.rtCut || 0);
    rtf.style.width = (clamp(v / 12, 0, 1) * 100).toFixed(1) + "%";
    $("rtmeter-val").textContent = v < 0.3 ? "resting" : `${MINUS}${v.toFixed(1)} dB @ ${fmtHz(M.rtHz || 0)}`;
  }
  const rst = $("read-stem");
  if (rst) rst.textContent = P.bypass.getValue() ? "Bypassed" : !P.stemOn.getValue() ? "Off" : stemSuggestion() ? "Suggestion ready"
    : stemIsNeutral() ? neutralText("stem")
    : stemMoves().map((k) => `${["Vox", "Drm", "Bass", "Oth"][k]} ${fmtSigned(stemGainAt(k)).replace(" dB", "")}`).join("\u00b7");
  for (let k = 0; k < 4; k++) {
    const f = $("sh-fill-" + k);
    if (!f) break;
    const v = clamp((M.stemShare || [])[k] || 0, 0, 100);
    f.style.width = v.toFixed(0) + "%";
    $("sh-val-" + k).textContent = `${Math.round(v)} %`;
  }
  const rs = $("read-sat");
  if (rs) rs.textContent = P.bypass.getValue() ? "Bypassed" : !P.satOn.getValue() ? "Off" : satIsNeutral() ? neutralText("sat") : `${SAT_MODES[satModeIndex()]} +${satDriveAt().toFixed(1)} dB`;
  const hf = $("harmmeter-fill");
  if (hf) {
    const active = P.satOn.getValue() && !satIsNeutral() && !P.bypass.getValue();
    hf.style.width = (active ? clamp((M.satHarm + 60) / 50, 0, 1) * 100 : 0).toFixed(1) + "%";
    $("harmmeter-val").textContent = !active ? "off" : M.satHarm <= -60 ? `< ${MINUS}60 dB` : `${fmtNum(M.satHarm, 0)} dB`;
  }
  const re = $("read-eq");
  if (re) {
    const moved = EQ_BANDS.filter((_, b) => Math.abs(eqGainAt(b)) >= 0.05).length;
    re.textContent = P.bypass.getValue() ? "Bypassed" : !eqEnabled() ? "Off" : moved ? `${moved} band${moved > 1 ? "s" : ""} shaped` : "Flat";
  }
  const rc = $("read-clip");
  if (rc) rc.textContent = P.bypass.getValue() ? "Bypassed" : clipOn ? `Clip ${clipText} dB` : "Off";
  const fill = $("clipmeter-fill"), val = $("clipmeter-val");
  if (fill) {
    fill.style.width = (clipOn ? clamp(-M.clip / 6, 0, 1) * 100 : 0).toFixed(1) + "%";
    val.textContent = clipOn ? `${clipText} dB` : "off";
  }
}

// ---------------------------------------------------------------------------------------------
// Meter events from the plug-in (30 per second).
window.__JUCE__.backend.addEventListener("polisherMeters", (frame) => {
  const srChanged = frame.sr && frame.sr !== M.sr;
  const wasAm = M.amState;
  Object.assign(M, frame);
  if (srChanged) drawEq();
  if (M.amReport !== amSeenVersion) {
    const first = amSeenVersion === -1;
    amSeenVersion = M.amReport;
    fetchReport(!first);
  }
  if (wasAm !== M.amState || M.amState === 1) updateAmButton();
  if (P.levelMatch.getValue()) refreshMatch();
  if (M.momentary > -70 || M.peakL > -80) lastAudioTime = performance.now();
  grWindow.push(M.gr);
  if (grWindow.length > 90) grWindow.shift();
  clipWindow.push(M.clip);
  if (clipWindow.length > 90) clipWindow.shift();
  tameWindow.push(M.tame || 0);
  if (tameWindow.length > 90) tameWindow.shift();
  punchWindow.push(M.punch || 0);
  if (punchWindow.length > 90) punchWindow.shift();
  updateMeters();
  updateOutputMeter();
  updateLiveReadouts();
  $("sp-mix").setAttribute("d", spectrumPath(M.mix));
  const master = spectrumPath(M.master);
  $("sp-master").setAttribute("d", master);
  $("sp-master-glow").setAttribute("d", master);
});
setInterval(() => { if (learnOn && learnView === "module" && ["max", "clip", "bass", "img", "dyn", "sat", "stem", "ts", "rt", "ul"].includes(selected)) renderLearn(); }, 1000);

// ---------------------------------------------------------------------------------------------
buildSpectrumGrid();
buildMeters();
renderChain();
renderModule();
setLearn(true);
refreshAmChoices();
updateAmButton();
refreshMatch();
onParamsChanged();
