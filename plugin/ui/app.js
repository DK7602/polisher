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
  clipOn: Juce.getToggleState("clipOn"),
  clipLevel: Juce.getSliderState("clipLevel"),
  clipSoftness: Juce.getSliderState("clipSoftness"),
  clipOs: Juce.getComboBoxState("clipOversampling"),
  eqOn: Juce.getToggleState("eqOn"),
  eqGain: [1, 2, 3, 4, 5, 6, 7].map((b) => Juce.getSliderState("eqGain" + b)),
  eqFreq: [1, 2, 3, 4, 5, 6, 7].map((b) => Juce.getSliderState("eqFreq" + b)),
};
const resetMeter = Juce.getNativeFunction("resetMeter");

function scaledToNorm(state, v) {
  const { start, end, skew } = state.properties;
  return Math.pow(clamp((v - start) / (end - start), 0, 1), skew);
}

// ---------------------------------------------------------------------------------------------
// Latest meter frame from the plug-in.
const M = {
  momentary: -100, shortTerm: -100, integrated: -100, range: 0, truePeak: -100,
  peakRecent: -100, peakL: -100, peakR: -100, gr: 0, clip: 0, mix: null, master: null,
  inShort: -100, inIntegrated: -100, sr: 48000,
};
let grWindow = [];           // ~3 s of gain-reduction samples for Learn
let clipWindow = [];         // ~3 s of clipper peak-reduction samples
let lastAudioTime = 0;

// ---------------------------------------------------------------------------------------------
// Module catalogue. Only the Maximizer processes audio in this build.
const modules = [
  { id: "eq", name: "Tonal EQ", live: true, soon: "",
    what: "Shapes the overall tone in seven musical bands, from the sub up to the air, and nudges the mix toward your genre's reference balance.",
    cells: [["Low End", "30 Hz", "0.0 dB", .5], ["Bass", "90 Hz", "0.0 dB", .5], ["Low Mid", "350 Hz", "0.0 dB", .5], ["Mid", "1 kHz", "0.0 dB", .5],
            ["Upper Mid", "2.5 kHz", "0.0 dB", .5], ["Presence", "5 kHz", "0.0 dB", .5], ["Air", "12 kHz", "0.0 dB", .5]],
    controls: [["Low End / Bass", "Weight of the kick and 808. Small moves here change how big the record feels."],
               ["Low Mid", "Where mud and boxiness build up. Cutting here often reads as clearer and louder."],
               ["Upper Mid / Presence", "Vocal consonants, snare crack and edge. A little goes a long way."],
               ["Air", "Sheen on top. Shelves rather than peaks, so hats get shine instead of bite."]] },
  { id: "bass", name: "Bass Control", soon: "Next up after the Tonal EQ.",
    what: "Keeps the low end solid on every system: mono below a crossover, controls 808 level jumps, and adds kick punch.",
    cells: [["Mono Below", "crossover", "110 Hz", .4], ["Sub Tame", "dynamic", "0.0 dB", .5], ["Punch", "kick attack", "0%", 0]],
    controls: [["Mono Below", "Stereo sub wastes headroom and collapses on club systems and phones."],
               ["Sub Tame", "Evens out 808 slides so the limiter doesn't grab the whole mix on long notes."],
               ["Punch", "Adds attack to the start of the kick without turning the sub up."]] },
  { id: "dyn", name: "Dynamics", soon: "Planned for an upcoming build.",
    what: "Multiband compression that controls the low end without ducking the vocal, and evens out section jumps gently.",
    cells: [["Crossovers", "3 bands", "120/2k/8k", .5], ["Low Ratio", "low band", "1:1", 0], ["Mid Attack", "mid band", "30 ms", .45], ["Release", "all bands", "Auto", .5]],
    controls: [["Crossovers", "Split points between the bands."], ["Ratio", "How firmly each band is held down."],
               ["Attack", "Slow lets drum hits through, fast catches vocal peaks."], ["Release", "Auto follows the tempo so it recovers between hits."]] },
  { id: "img", name: "Stereo Imager", soon: "Planned for an upcoming build.",
    what: "Controls width per band: keeps the low end centred and opens up hats, reverbs and ad-libs.",
    cells: [["Lows", "< 250 Hz", "0%", .5], ["Mids", "250 Hz–5 kHz", "0%", .5], ["Highs", "> 5 kHz", "0%", .5]],
    controls: [["Lows", "Kept centred. Wide bass smears and costs headroom."], ["Mids", "Small moves only, so the lead vocal stays locked in."],
               ["Highs", "Where width sounds best: hats, reverbs, ad-libs."]] },
  { id: "sat", name: "Saturation", soon: "Planned for an upcoming build.",
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
    row.style.top = (110.2 + i * 69.55).toFixed(1) + "px";
    if (m.id === selected) row.appendChild(img("assets/row_red.png"));
    else if (i === 6) row.appendChild(img("assets/row_blue.png"));   // the panel art has row 7 lit red

    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "row-btn";
    btn.setAttribute("aria-pressed", String(m.id === selected));
    btn.innerHTML = `<span class="row-text"><span class="row-name">${m.name}</span>
      <span class="row-read mono" id="read-${m.id}">${m.live ? "" : "Coming soon"}</span></span>
      <span class="row-num mono">${String(i + 1).padStart(2, "0")}</span>`;
    btn.addEventListener("click", () => { selected = m.id; renderChain(); renderModule(); renderLearn(); });
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

function renderModule() {
  const m = modules.find((x) => x.id === selected);
  const row = $("knob-row");
  row.innerHTML = "";
  $("module-name").textContent = m.name.toUpperCase();
  $("soon-badge").hidden = !!m.live;
  $("btn-bypass").hidden = !m.live;
  if (m.live) {
    row.classList.remove("soon");
    ({ clip: clipperCells, eq: eqCells, max: maximizerCells })[m.id].forEach((c) => row.appendChild(c));
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
function tip(title, text, ok) {
  const d = document.createElement("div");
  d.className = "l-tip" + (ok ? " ok" : "");
  d.appendChild(head(title));
  d.appendChild(para("l-p", text));
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

  if (m.id === "clip") { renderClipperLearn(body); liftWarning(body); return; }
  if (m.id === "eq") { renderEqLearn(body); liftWarning(body); return; }

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
      (P.clipOn.getValue()
        ? "Back Drive off until Short-term stops climbing; for more, lower Clip Level a little."
        : "Back Drive off until Short-term stops climbing, and turn on the Soft Clipper: on spiky drums it gets more loudness than the limiter can.")));
  } else if (avgGr < -6 || peakGr < -9) {
    body.appendChild(tip("FIX IT IN THE MIX",
      `That's heavy limiting (up to ${fmtNum(-peakGr)} dB). If the kick or snare lose punch, back off Drive, or tame the drum peaks in your mix (a clipper on the drum bus). The Soft Clipper (module 06) can take some of the peaks too.`));
  } else if (M.integrated > -9 && M.integrated > -99) {
    body.appendChild(tip("HEADS UP",
      "This is very loud (above −9 LUFS). Spotify and YouTube play music at about −14 LUFS, so they'll turn it down. Extra loudness buys density, not volume."));
  } else if (M.integrated > -99 && M.integrated < -16) {
    body.appendChild(tip("SOUNDS CLEAN", "Plenty of dynamics left. For a streaming release, you can push Drive until Integrated reaches around −14 to −10 LUFS.", true));
  } else {
    body.appendChild(tip("SOUNDS HEALTHY", "Limiting is moderate, so transients should stay intact. When you A/B, remember B is louder: louder always sounds \u201cbetter\u201d, so judge tone, not volume.", true));
  }
  const note = para("l-p", "When Auto-Master arrives, this panel will explain its own choices for every module.");
  note.style.cssText = "font-size:12px;color:var(--dim)";
  body.appendChild(note);
  liftWarning(body);
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
      `${sgn(big.g)} of ${big.band.name} is a big move for a master. If one sound needs it (usually the 808, the kick or the vocal), fix that sound in the mix and keep master EQ moves to 1–3 dB.`));
  } else if (lowBoost > 2 && peakGr < -6) {
    body.appendChild(tip("HEADS UP",
      `The low-end boost is making the Maximizer work harder (up to ${fmtNum(-peakGr)} dB). Low end eats headroom, so each dB of boost there costs loudness. Try a smaller boost, or cut Low Mid instead so the bass reads clearer.`));
  } else if (bite > 4) {
    body.appendChild(tip("HEADS UP", "That much boost between 2.5 and 5 kHz can get harsh, especially on earbuds. Check it at low volume and on cheap speakers."));
  } else if (eqGainAt(6) > 4) {
    body.appendChild(tip("HEADS UP", "Lots of Air can make hi-hats and 's' sounds sizzle. If they poke out, back it off or cut Presence a little."));
  } else if (!moved.length) {
    body.appendChild(tip("TRY THIS", "Look at the white line (your mix). A hump that sits above its neighbours, often around 200–400 Hz, is a good place for a 1–2 dB cut.", true));
  } else {
    body.appendChild(tip("LISTEN FOR TONE", "Click EQ ON/OFF (top right of this panel) to compare. EQ boosts add level, and louder always sounds \u201cbetter\u201d, so judge the tone, not the volume.", true));
  }
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
      `That's heavy clipping (about ${fmtNum(-avgClip)} dB on average). Snares and claps can turn crunchy: raise Clip Level or Softness until it's clean. If your drums need this much, clip or limit the drum bus in the mix instead.`));
  } else if (os === "1x") {
    body.appendChild(tip("HEADS UP", "At 1x you're adding aliasing. Switch to 4x for a cleaner top end."));
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
  if (selected === "eq") {
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
  else P.bypass.setValue(!P.bypass.getValue());
  onParamsChanged();
  userEdited();
});
P.bypass.valueChangedEvent.addListener(onParamsChanged);
P.clipOn.valueChangedEvent.addListener(onParamsChanged);
P.eqOn.valueChangedEvent.addListener(onParamsChanged);

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

function restartMeasurement() { resetMeter(); grWindow = []; clipWindow = []; }
$("btn-reset").addEventListener("click", () => { restartMeasurement(); toast("Loudness measurement reset."); });

// Integrated is an average over everything since it last started. After you change a control,
// start it over (once the control settles) so it describes the settings you're hearing now.
let editTimer = null;
function userEdited() {
  if (!P.resetOnChange.getValue()) return;
  clearTimeout(editTimer);
  editTimer = setTimeout(restartMeasurement, 700);
}

let toastTimer = null;
function toast(text) {
  const t = $("toast");
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 2600);
}
$("btn-automaster").classList.add("waiting");
$("btn-automaster").addEventListener("click", () =>
  toast("Auto-Master arrives with the analysis engine in an upcoming build. For now, set Drive by ear and let Learn explain what it's doing."));

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

function updateLiveReadouts() {
  const gr = M.gr;
  const chip = $("module-chip");
  const m = modules.find((x) => x.id === selected);
  const clipOn = !!P.clipOn.getValue();
  const clipText = M.clip > -0.05 ? "0.0" : fmtNum(M.clip);
  chip.hidden = !m.live;
  chip.textContent = selected === "clip" ? (clipOn ? `CLIP ${clipText} dB` : "OFF")
                   : selected === "eq" ? (!eqEnabled() ? "OFF" : eqIsFlat() ? "FLAT" : "SHAPING")
                   : `GR ${gr > -0.05 ? "0.0" : fmtNum(gr)} dB`;
  const r = $("read-max");
  if (r) r.textContent = P.bypass.getValue() ? "Bypassed" : `GR ${gr > -0.05 ? "0.0" : fmtNum(gr)} dB`;
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
  Object.assign(M, frame);
  if (srChanged) drawEq();
  if (M.momentary > -70 || M.peakL > -80) lastAudioTime = performance.now();
  grWindow.push(M.gr);
  if (grWindow.length > 90) grWindow.shift();
  clipWindow.push(M.clip);
  if (clipWindow.length > 90) clipWindow.shift();
  updateMeters();
  updateOutputMeter();
  updateLiveReadouts();
  $("sp-mix").setAttribute("d", spectrumPath(M.mix));
  const master = spectrumPath(M.master);
  $("sp-master").setAttribute("d", master);
  $("sp-master-glow").setAttribute("d", master);
});
setInterval(() => { if (learnOn && (selected === "max" || selected === "clip")) renderLearn(); }, 1000);

// ---------------------------------------------------------------------------------------------
buildSpectrumGrid();
buildMeters();
renderChain();
renderModule();
setLearn(true);
onParamsChanged();
