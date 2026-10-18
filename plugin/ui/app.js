// Masterology liquid-glass UI.
// Talks to the plug-in through JUCE's web relays (parameters) and the "polisherMeters" event.

import * as Juce from "./juce/index.js";
import { GENRES as STYLE_GENRES, STYLES as SONG_STYLES } from "./mixstyles.js";
import { DAWS, dawOf, fillDaw } from "./daws.js";

// The DAW you work in: found from the host, or picked here / in Mixology / in Voxology (all three follow). Every tip
// says how to do things in it: {daw} / {daw:phrase} tokens (the window's and the engine's text) are filled in here.
const DAW = { key: "cubase", picked: false, detected: "cubase", from: 0 };
const D = (s) => (typeof s === "string" ? fillDaw(s, DAW.key) : s);

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
  inTrim: Juce.getSliderState("inTrim"),
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
  eqMode: [1, 2, 3, 4, 5, 6, 7].map((b) => Juce.getComboBoxState("eqMode" + b)),
  dynOn: Juce.getToggleState("dynOn"),
  maxBassFirst: Juce.getSliderState("maxBassFirst"),
  maxDither: Juce.getComboBoxState("maxDither"),
  dynAdaptive: Juce.getToggleState("dynAdaptive"),
  dynX: [1, 2, 3].map((x) => Juce.getSliderState("dynX" + x)),
  dynThr: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynThr" + b)),
  dynRatio: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynRatio" + b)),
  dynAtk: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynAtk" + b)),
  dynRel: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynRel" + b)),
  dynGain: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynGain" + b)),
  dynImpact: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynImpact" + b)),
  dynSustain: [1, 2, 3, 4].map((b) => Juce.getSliderState("dynSustain" + b)),
  satOn: Juce.getToggleState("satOn"),
  satMode: Juce.getComboBoxState("satMode"),
  satFocus: Juce.getComboBoxState("satFocus"),
  satChannels: Juce.getComboBoxState("satChannels"),
  dynChannels: Juce.getComboBoxState("dynChannels"),
  glueOn: Juce.getToggleState("glueOn"),
  glueChar: Juce.getComboBoxState("glueChar"),
  glueThr: Juce.getSliderState("glueThr"),
  glueRatio: Juce.getSliderState("glueRatio"),
  glueAtk: Juce.getSliderState("glueAtk"),
  glueRel: Juce.getSliderState("glueRel"),
  glueAuto: Juce.getToggleState("glueAuto"),
  glueSc: Juce.getSliderState("glueSc"),
  glueMix: Juce.getSliderState("glueMix"),
  glueGain: Juce.getSliderState("glueGain"),
  satDrive: Juce.getSliderState("satDrive"),
  satMix: Juce.getSliderState("satMix"),
  ulOn: Juce.getToggleState("ulOn"),
  ulAmount: Juce.getSliderState("ulAmount"),
  ulLength: Juce.getSliderState("ulLength"),
  ulFocus: Juce.getComboBoxState("ulFocus"),
  stemOn: Juce.getToggleState("stemOn"),
  stemKeep: Juce.getToggleState("stemKeep"),
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
// JUCE sends every value the page sets straight back. During a drag or a quick run of wheel or arrow steps an
// older value can come back after a newer one was sent: the next step would start from it (a step lost) and the
// pointer would jump back for a moment. So an echo of a value sent in the last second is dropped (the newest one
// too: nothing changed). Any other value is the host's (automation, undo) and is taken.
function guardEchoes(state) {
  const sent = [];   // [value, when] sent in the last second, oldest first
  const prune = (now) => { while (sent.length && (sent.length > 40 || now - sent[0][1] > 1000)) sent.shift(); };
  const set = state.setNormalisedValue.bind(state), handle = state.handleEvent.bind(state);
  state.setNormalisedValue = (n) => { set(n); const now = performance.now(); sent.push([state.getScaledValue(), now]); prune(now); };
  state.handleEvent = (ev) => {
    if (ev && ev.eventType === "valueChanged" && sent.length) {
      prune(performance.now());
      const { start = 0, end = 1, interval = 0 } = state.properties;
      const near = Math.max(interval / 2, Math.abs(end - start) * 1e-6);
      const i = sent.findIndex(([v]) => Math.abs(v - ev.value) <= near);
      if (i >= 0) { sent.splice(0, i + 1); return; }   // ours, coming back
      sent.length = 0;                                  // the host's: it wins
    }
    handle(ev);
  };
}
Object.values(P).flat().filter((st) => typeof st.setNormalisedValue === "function").forEach(guardEchoes);
const resetMeter = Juce.getNativeFunction("resetMeter");
const amStart = Juce.getNativeFunction("startAutoMaster");
const amCancel = Juce.getNativeFunction("cancelAutoMaster");
const amFinish = Juce.getNativeFunction("finishAutoMaster");     // "use what you've heard" (12 s of music on)
const tsClearTarget = Juce.getNativeFunction("clearToneTarget");  // Tone Steady follows the music again (an undo step)
// Undo / redo (the plug-in keeps the history, so it survives closing this window) and snapshots S1..S4.
const histUndo = Juce.getNativeFunction("historyUndo");
const histRedo = Juce.getNativeFunction("historyRedo");
const histGroup = Juce.getNativeFunction("historyGroup");
const snapStore = Juce.getNativeFunction("snapshotStore");
const snapRecall = Juce.getNativeFunction("snapshotRecall");
const amGetReport = Juce.getNativeFunction("getAutoMasterReport");
const aaSet = Juce.getNativeFunction("setAutoAnalyze");     // Auto Analyze on / off (saved with the project)
const aaGet = Juce.getNativeFunction("getAutoAnalyze");     // its status and the edits on offer
const aaApply = Juce.getNativeFunction("applyAutoEdit");    // a module id or "all": one undo step
const aaKeep = Juce.getNativeFunction("keepAutoEdit");      // leave it as it is (the advice doesn't come back)
const refLoad = Juce.getNativeFunction("loadReference");
const refClear = Juce.getNativeFunction("clearReference");
const refGet = Juce.getNativeFunction("getReference");
const genreGet = Juce.getNativeFunction("getGenreCurve");

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
  amState: 0, amProgress: 0, amHearing: false, amReport: 0, matchDb: 0, corr: 1,
  // Whole-song listen (mode 0 Auto-Master, 1 learn Tone Steady's target only) and the stored target (s of song, 0 = none).
  amMode: 0, amHeard: 0, amLoudAt: -1, amLoudEnd: -1, amLoudJump: false, amLoudLufs: -100, amCanFinish: false, amByItself: false, tsTarget: 0, tsLearnFail: 0,
  // How far the music playing sits from that stored tone (dB, -1 unknown), and whether far for long (another song's?).
  tsFar: false, tsDist: -1,
  undo: "", redo: "", snaps: [{}, {}, {}, {}], snapActive: -1, snapModified: false,
  glueGr: 0, dynGr: [0, 0, 0, 0], dynImpact: [0, 0, 0, 0], dynSustain: [0, 0, 0, 0], satHarm: -100, stemShare: [0, 0, 0, 0], stemKeep: 0, rtCut: 0, rtHz: 0, tsDb: 0, tsHz: 0,
  ulBoost: 0, ulSquash: 0, refVersion: 0, refState: 0,
  // The suite link: Mixology's hand-off for this song ({ style, genre, peak, lufs, analysed, mono, limited, tone }) or null.
  fromMixology: null,
  // Auto Analyze: on, why it waits ("" = checked), music heard in this stretch of playback, edits on offer.
  aaOn: false, aaPause: "off", aaHeard: 0, aaEdits: 0, aaVersion: 0,
  listen: 0, mono: false, phone: false, listenNow: 0, listenGain: 0, listenUnder: -100,
  bassCut: 0, playing: true,   // playing: the host's transport (builds before it didn't send it)
};
// Listening aids (Listen / Solo, MONO, PHONE): monitoring only. The plug-in keeps them out of the saved
// parameters, switches them off when this window closes and never puts them in an offline export.
// Listen modes (numbers shared with dsp/include/polisher/Monitoring.h): 1 Saturation added, 2 Clipper
// removed, 3 Resonance Tamer removed, 4 Maximizer removed, 5-8 Dynamics band solo, 9-12 Dynamics removed.
const MON = { listen: 0, mono: false, phone: false, sentAt: 0 };
const setMonitorFn = Juce.getNativeFunction("setMonitor");
const LISTEN_OF_MODULE = { sat: 1, clip: 2, rt: 3, max: 4 };
const isSoloMode = (m) => m >= 5 && m <= 8;
// Match ref: the loaded reference song (its measurements; the plug-in keeps no audio).
let refInfo = { state: 0, message: "", profile: null };
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
    cells: [],
    controls: [["Low End / Bass", "Weight of the kick and 808. Small moves here change how big the record feels."],
               ["Low Mid", "Where mud and boxiness build up. Cutting here often reads as clearer, and frees headroom the Maximizer can turn into loudness."],
               ["Upper Mid / Presence", "Vocal consonants, snare crack and edge. A little goes a long way."],
               ["Air", "Sheen on top. Shelves rather than peaks, so hats get shine instead of bite."]] },
  { id: "stem", name: "Stem EQ", live: true, soon: "",
    what: "Turns the vocals, drums, bass or everything else up or down inside your finished stereo mix, like reaching back into the session and moving one fader.",
    cells: [],
    controls: [["Vocals / Drums / Bass / Other", "Turns that part of the mix up or down (±12 dB). Other is everything else: synths, keys, guitars, pads, effects."],
               ["How it works", "A small AI model listens to the mix and works out, moment by moment and frequency by frequency, how much of the sound is vocals, drums, bass or other. Your four levels are mixed by those shares into one EQ that updates about 180 times a second."],
               ["Keep Loudness", "On: your moves change the balance, not how loud the mix is. When you turn a part up, Stem EQ turns the whole mix down by the same loudness (and the other way round), so A/B is a fair compare and the Maximizer isn't pushed harder. Off: a move also makes the mix louder or quieter."],
               ["Trade-offs", "Adds about 40 ms of delay at any sample rate ({daw} compensates it). Where two parts share the same notes, a move nudges both a little; Other moves touch the drums slightly. Moves up to about ±6 dB stay clean. At 96 kHz or higher, set your audio buffer to 256 samples or more: Stem EQ (with Tone Steady and the Resonance Tamer) does its work in short bursts, and very small buffers there can crackle."]] },
  { id: "ts", name: "Tone Steady", live: true, soon: "",
    what: "Keeps your tone steady as the song plays: when a section drifts muddier, harsher or thinner than the rest of the song, it nudges those frequencies back.",
    cells: [], controls: [] },
  { id: "rt", name: "Resonance Tamer", live: true, soon: "",
    what: "Finds ringing tones that stick out (boxiness, whistly vocal tones, harsh hats) and cuts them, only while they ring. Above about 1 kHz the cuts are narrow; below about 800 Hz each cut is roughly 3/4 octave wide, so it takes some of the sound around the ring with it.",
    cells: [], controls: [] },
  { id: "bass", name: "Bass Control", live: true, soon: "",
    what: "Keeps the low end solid on every system: mono below a crossover, controls 808 level jumps, and adds kick punch.",
    cells: [],
    controls: [["Mono Below", "Stereo sub wastes headroom and collapses on club systems and phones."],
               ["Sub Tame", "Evens out 808 slides so the limiter doesn't grab the whole mix on long notes."],
               ["Punch", "Adds attack to the start of the kick (found by its click) without turning the 808 up."]] },
  { id: "glue", name: "Glue", live: true, soon: "",
    what: "One compressor for the whole mix, in three classic bus-compressor flavours: the parts move together, so the beat and the vocal sit as one.",
    cells: [["Character", "flavour", "Bus", .5], ["Threshold", "starts at", "-20 dB", .5], ["Ratio", "how firm", "off", 0], ["Mix", "parallel", "100 %", 1]],
    controls: [["Character", "Bus = clean, firm glue. Opto = smooth and slow. Tube = the softest."], ["Threshold", "Where it starts holding the mix down."],
               ["Ratio", "How firmly. 2:1 is classic glue; 1:1 = off."], ["Attack", "Slow (10-30 ms) keeps the drums' punch."],
               ["Release", "How fast it lets go; AUTO follows the music."], ["Ignore Lows", "It hears much less of the 808 below this, so the 808 doesn't pump the vocal."],
               ["Mix", "Blend with the uncompressed mix (parallel compression)."], ["Gain", "Level after the Glue."]] },
  { id: "dyn", name: "Dynamics", live: true, soon: "",
    what: "Four-band compression: steadies the low end without ducking the vocal, and evens out section jumps gently.",
    cells: [["Crossovers", "3 splits", "120/1k/6k", .5], ["Low Ratio", "low band", "1:1", 0], ["Mid Attack", "mid band", "30 ms", .45], ["Release", "all bands", "Auto", .5]],
    controls: [["Crossovers", "Split points between the bands."], ["Ratio", "How firmly each band is held down."],
               ["Attack", "Slow lets drum hits through, fast catches vocal peaks."], ["Release", "Auto follows the tempo so it recovers between hits."],
               ["Channels", "STEREO / MID ONLY / SIDE ONLY: work on both channels, only the centre, or only the stereo edges."]] },
  { id: "img", name: "Stereo Imager", live: true, soon: "",
    what: "Controls width per band: keeps the low end centred and opens up hats, reverbs and ad-libs.",
    cells: [["Lows", "< 250 Hz", "0%", .5], ["Mids", "250 Hz–5 kHz", "0%", .5], ["Highs", "> 5 kHz", "0%", .5]],
    controls: [["Lows", "Kept centred. Wide bass smears and costs headroom."], ["Mids", "Small moves only, so the lead vocal stays locked in."],
               ["Highs", "Where width sounds best: hats, reverbs, ad-libs."]] },
  { id: "sat", name: "Saturation", live: true, soon: "",
    what: "Adds harmonics (tape, tube or clip styles) so bass translates on small speakers and the master feels denser.",
    cells: [["Mode", "character", "Tape", .5], ["Focus", "which part", "Full", 0], ["Drive", "input", "0.0 dB", 0], ["Mix", "wet / dry", "0%", 0]],
    controls: [["Mode", "Tape = soft and warm. Tube = richer. Clip = aggressive."], ["Focus", "Full = the whole mix. Bass only = the 808 and kick, so they show up on phone speakers. Highs only = sheen on hats and cymbals. Bass + Highs = both, vocal clean."],
               ["Channels", "Stereo = both alike. Mid = only the centre (vocal, 808). Side = only the stereo edges (width, reverbs)."],
               ["Drive", "How many harmonics are generated."],
               ["Mix", "Blend keeps transients clean while harmonics fill in underneath."]] },
  { id: "clip", name: "Soft Clipper", live: true, soon: "",
    what: "Shaves the tallest drum peaks before the limiter so the limiter works less. This is how loud modern masters stay punchy.",
    cells: [["Threshold", "dBFS", "0.0 dB", 1], ["Softness", "knee", "70%", .7], ["Oversample", "quality", "8×", .8]],
    controls: [["Threshold", "Where clipping starts. Lower = more peaks shaved."], ["Softness", "Rounded knee: heard as punch, not distortion."],
               ["Oversampling", "Cuts down the harsh aliasing that clipping otherwise adds."]] },
  { id: "max", name: "Maximizer", live: true },
];
let selected = "max";
let learnView = "module";      // "module", "report" (the Auto-Master report), "ref" (Match ref) or "history" (loudness history)
let amReport = null;           // last Auto-Master report (parsed), or null

// ---------------------------------------------------------------------------------------------
// Tonal EQ bands. Shapes, widths and ranges match dsp/include/polisher/TonalEQ.h exactly, so the
// curve drawn here is the plug-in's real response.
const EQ_BANDS = [
  { name: "Low End", shape: "low", def: 45, q: 0.707,
    boost: "More sub weight: the 808 and kick tail feel bigger on big speakers. Phones and laptops barely reproduce it. A subsonic filter at 15 Hz switches in with the boost, so rumble below about 12 Hz is turned down instead of up (between 12 and 20 Hz part of the boost remains).",
    cut: "Less sub rumble: a cleaner bottom and more headroom, so the Maximizer can make it louder." },
  { name: "Bass", shape: "bell", def: 100, q: 0.8,
    boost: "More body in the kick and 808: fuller and warmer. This is the low end most speakers actually play.",
    cut: "A tighter low end: less boom, so the kick punches through." },
  { name: "Low Mid", shape: "bell", def: 350, q: 0.9,
    boost: "More warmth and thickness, but a little too much turns muddy and boxy.",
    cut: "Clears mud and boxiness. The mix often sounds cleaner, and the headroom it frees lets the Maximizer make it louder." },
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

// Each band is the RBJ analog prototype realised as a magnitude-matched biquad (Vicanek), exactly as
// TonalEQ::designBand does it, so the drawn curve stays honest near 20 kHz at 44.1 / 48 kHz.
function eqAnalogPower(shape, A, q, x) {
  const x2 = x * x;
  if (shape === "bell") {
    const d = (1 - x2) * (1 - x2);
    return (d + (A * x / q) ** 2) / (d + (x / (A * q)) ** 2);
  }
  const k2 = (A * x2) / (q * q);
  return shape === "low"
    ? (A * A * ((A - x2) ** 2 + k2)) / ((1 - A * x2) ** 2 + k2)
    : (A * A * ((1 - A * x2) ** 2 + k2)) / ((A - x2) ** 2 + k2);
}
function eqMatchedCut(shape, A, q, w0, wTop) {
  let wp = w0, qp = q;
  if (shape === "bell") qp = q * A;
  else if (shape === "low") wp = w0 / Math.sqrt(A);
  else wp = w0 * Math.sqrt(A);
  const zeta = 0.5 / qp, r = Math.exp(-zeta * wp);
  const a1 = zeta <= 1 ? -2 * r * Math.cos(Math.sqrt(1 - zeta * zeta) * wp) : -2 * r * Math.cosh(Math.sqrt(zeta * zeta - 1) * wp);
  const a2 = r * r;
  const A0 = (1 + a1 + a2) ** 2, A1 = (1 - a1 + a2) ** 2, A2 = -4 * a2;
  const phi = (w) => { const s = Math.sin(0.5 * w) ** 2; return [1 - s, s, 4 * (1 - s) * s]; };
  const target = (w, p) => eqAnalogPower(shape, A, q, w / w0) * (A0 * p[0] + A1 * p[1] + A2 * p[2]);
  const B0 = A0 * eqAnalogPower(shape, A, q, 0), pm = phi(w0), Tm = target(w0, pm);
  const G = 12, lo = 0.5 * w0, ratio = Math.pow(wTop / lo, 1 / (G - 1));
  let num = 0, den = 0;
  for (let k = 0, w = lo; k < G; k++, w *= ratio) {
    const p = phi(w), T = target(w, p);
    const c = B0 * p[0] + (p[1] * (Tm - B0 * pm[0])) / pm[1] - T, d = p[2] - (p[1] * pm[2]) / pm[1], wt = 1 / (T * T);
    num += wt * c * d; den += wt * d * d;
  }
  const B2 = den > 0 ? -num / den : (A2 * B0) / A0;
  const B1 = Math.max(0, (Tm - B0 * pm[0] - B2 * pm[2]) / pm[1]);
  const s0 = Math.sqrt(B0), s1 = Math.sqrt(B1), W = 0.5 * (s0 + s1);
  const b0 = 0.5 * (W + Math.sqrt(Math.max(0, W * W + B2)));
  return { b0, b1: 0.5 * (s0 - s1), b2: -B2 / (4 * b0), a1, a2 };
}
function eqBandCoeffs(b, gain, freq, sr) {
  const band = EQ_BANDS[b];
  const f0 = Math.min(freq, 0.45 * sr), w0 = (2 * Math.PI * f0) / sr;
  const wTop = Math.min((2 * Math.PI * 20000) / sr, Math.PI);
  const g = clamp(gain, -EQ_MAX, EQ_MAX);
  if (g <= 0) return eqMatchedCut(band.shape, Math.pow(10, g / 40), band.q, w0, wTop);
  const c = eqMatchedCut(band.shape, Math.pow(10, -g / 40), band.q, w0, wTop);   // a boost = the inverse of the cut
  return { b0: 1 / c.b0, b1: c.a1 / c.b0, b2: c.a2 / c.b0, a1: c.b1 / c.b0, a2: c.b2 / c.b0 };
}
function biquadDb(c, f, sr) {
  const w = (2 * Math.PI * f) / sr, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
  const dr = 1 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}
function eqBandDb(b, gain, freq, f, sr) { return biquadDb(eqBandCoeffs(b, gain, freq, sr), f, sr); }

// Subsonic filter (TonalEQ::designSubsonic): 4th-order Butterworth high-pass at 15 Hz, engaged while
// Low End is boosted, so the shelf doesn't also boost inaudible rumble.
const EQ_SUBSONIC_HZ = 15;
function eqSubsonicDb(f, sr) {
  const w0 = (2 * Math.PI * EQ_SUBSONIC_HZ) / sr, cw = Math.cos(w0), sw = Math.sin(w0);
  let total = 0;
  [0.5 / Math.cos(Math.PI / 8), 0.5 / Math.cos((3 * Math.PI) / 8)].forEach((q) => {
    const alpha = sw / (2 * q), a0 = 1 + alpha;
    total += biquadDb({ b0: (0.5 * (1 + cw)) / a0, b1: -(1 + cw) / a0, b2: (0.5 * (1 + cw)) / a0, a1: (-2 * cw) / a0, a2: (1 - alpha) / a0 }, f, sr);
  });
  return total;
}
const eqSubsonicOn = () => eqGainAt(0) > 0.005;
function eqResponseDb(f, sr) {
  let total = 0;
  EQ_BANDS.forEach((_, b) => { const g = eqGainAt(b); if (Math.abs(g) > 1e-4) total += eqBandDb(b, g, eqFreqAt(b), f, sr); });
  if (eqSubsonicOn()) total += eqSubsonicDb(f, sr);
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
// Mouse wheel and trackpad, as steps: up (or left) = more. Shift+wheel arrives as a sideways scroll (deltaY 0), so
// the direction comes from the larger of the two; a scroll with neither is ignored. A notch of a classic wheel
// (wheelDelta 120) is one step. A trackpad, or a free-spinning or high-resolution wheel, sends many small scrolls
// (and momentum): those add up, one step per ~100 px, so a swipe moves a knob about as far as a notch or two.
function wheelStepper() {
  let acc = 0, at = 0;
  return (e) => {
    const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 100 : 1;   // lines / pages, in pixels
    const dx = e.deltaX * unit, dy = e.deltaY * unit, vertical = Math.abs(dy) >= Math.abs(dx);
    const d = -(vertical ? dy : dx);
    if (!d) return 0;
    const now = performance.now();
    if (now - at > 400 || acc * d < 0) acc = 0;                            // a new gesture, or the other way
    at = now;
    const ticks = Math.abs((vertical ? e.wheelDeltaY : e.wheelDeltaX) || 0) / 120;
    if (e.deltaMode !== 0 || (ticks >= 1 && Number.isInteger(ticks))) { acc = 0; return Math.sign(d) * Math.max(1, ticks); }
    acc += d;
    const n = Math.trunc(acc / 100);
    acc -= n * 100;
    return n;
  };
}

// A typed value -> the control's own units. Accepts "−3", "-3 dB", "1.2k" / "1.2 kHz", "2.5:1", "120 ms",
// "off" (the control's minimum), "1,200" (a comma before exactly three digits groups thousands; any other comma
// is a decimal point, "2,5"); keeps it in range. A control that only goes up from a positive start
// (Tone Steady Range) takes "-3" as 3. Returns null when there is no number.
function parseTyped(text, state) {
  const { start, end } = state.properties;
  const t = String(text).trim().toLowerCase().replace(/\u2212/g, "-").replace(/(\d),(\d{3})(?!\d)/g, "$1$2").replace(",", ".");
  if (t === "off") return start;
  const m = /([-+]?\d*\.?\d+)\s*(k(?!hz)|khz)?/.exec(t);
  if (!m) return null;
  let v = parseFloat(m[1]) * (m[2] ? 1000 : 1);
  if (!Number.isFinite(v)) return null;
  if (start >= 0 && v < 0) v = -v;
  return clamp(v, Math.min(start, end), Math.max(start, end));
}

// One step of a knob, in normalised units (1 %, Shift 0.2 %), but at least far enough that its value changes: near
// the bottom of a skewed range (Ratio at 1:1, short attack and release times) or with whole-number values (Bass
// First, Impact), 1 % rounds back to the same value and the wheel and arrow keys would do nothing.
function stepNorm(state, from, dir, size) {
  const legal = (n) => state.snapToLegalValue(state.normalisedToScaledValue(clamp(n, 0, 1)));
  const was = legal(from);
  let n = from;
  for (let i = 0; i < 1000; i++) {
    n = clamp(n + dir * size, 0, 1);
    if (legal(n) !== was || n <= 0 || n >= 1) break;
  }
  return n;
}

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
    if (e.button !== 0) return;   // drags with the left button only (right-click is the host's menu)
    knob.setPointerCapture(e.pointerId);
    dragStart = { y: e.clientY, n: state.getNormalisedValue() };
    state.sliderDragStarted();
    // No preventDefault: the press gives the knob the keyboard, as a click does, so the arrow keys and Enter work on
    // the knob you just touched (a mouse focus shows no ring: :focus-visible). Nothing else happens by default here:
    // the page selects no text and the knob's image can't be dragged.
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
  knob.addEventListener("lostpointercapture", end);   // e.g. the window lost the mouse mid-drag: the undo step still closes
  knob.addEventListener("dblclick", () => { state.sliderDragStarted(); setNorm(scaledToNorm(state, def)); state.sliderDragEnded(); });
  const wheel = wheelStepper();
  knob.addEventListener("wheel", (e) => {
    e.preventDefault();
    const steps = wheel(e);
    if (!steps) return;
    state.sliderDragStarted();
    let n = state.getNormalisedValue();
    for (let i = 0; i < Math.abs(steps); i++) n = stepNorm(state, n, Math.sign(steps), e.shiftKey ? 0.002 : 0.01);
    setNorm(n);
    state.sliderDragEnded();
  }, { passive: false });
  // Double-click the number (or press Enter on the knob) to type an exact value.
  const typeIn = () => {
    if (cell.querySelector(".value-input")) return;
    const input = document.createElement("input");
    input.className = "value-input mono";
    input.type = "text";
    const shown = value.textContent;
    input.value = shown;
    input.setAttribute("aria-label", `${label}: type a value`);
    value.hidden = true;
    value.after(input);
    input.focus();
    input.select();
    let done = false;
    const finish = (commit) => {
      if (done) return;
      done = true;
      // Left as it opened (a look, then a click away or Enter): nothing changes. The box shows the rounded value,
      // and writing that back would round a finer one (2.37 dB -> 2.40) and add an undo step.
      const v = commit && input.value.trim() !== shown.trim() ? parseTyped(input.value, state) : null;
      input.remove();
      value.hidden = false;
      if (v !== null) { state.sliderDragStarted(); setNorm(scaledToNorm(state, v)); state.sliderDragEnded(); }
      knob.focus();
    };
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") { e.preventDefault(); finish(true); }
      else if (e.key === "Escape") { e.preventDefault(); finish(false); }
      e.stopPropagation();
    });
    input.addEventListener("blur", () => finish(true));
  };
  value.title = "Double-click to type a value";
  value.addEventListener("dblclick", typeIn);
  // Each key press is one gesture (the plug-in's undo merges quick repeats into one step).
  const nudge = (dir, size) => { state.sliderDragStarted(); setNorm(stepNorm(state, state.getNormalisedValue(), dir, size)); state.sliderDragEnded(); };
  knob.addEventListener("keydown", (e) => {
    const size = e.shiftKey ? 0.002 : 0.01;
    if (e.key === "ArrowUp" || e.key === "ArrowRight") nudge(1, size);
    else if (e.key === "ArrowDown" || e.key === "ArrowLeft") nudge(-1, size);
    else if (e.key === "Enter") typeIn();
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
  liveKnobCell({ label: "Input Trim", sub: "before the chain", state: P.inTrim, def: 0, minText: `${MINUS}24`, maxText: "0",
                 fmt: (v) => `${fmtNum(v, 1)} dB` }),
  liveKnobCell({ label: "Bass First", sub: "808 ducks first", state: P.maxBassFirst, def: 0, minText: "off", maxText: "100",
                 fmt: (v) => (v < 0.5 ? "off" : `${Math.round(v)} %`) }),
  segCell({ label: "Dither", sub: "for exports", state: P.maxDither, labels: ["Off", "16-bit", "24-bit"],
            notes: ["float export", "16-bit WAV", "24-bit WAV"] }),
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
  // Which part of the stereo image the band works on: click cycles STEREO -> MID -> SIDE.
  const ms = document.createElement("button");
  ms.type = "button";
  ms.className = "pill mono ms-btn";
  const msRefresh = () => {
    const i = eqModeAt(b);
    ms.textContent = EQ_MODES[i];
    ms.classList.toggle("pill-red", i > 0);
    ms.classList.toggle("pill-blue", i === 0);
    ms.title = ["Stereo: the band works on the whole mix", "Mid: only the centre (vocal, kick, 808, snare)", "Side: only the stereo edges (width, reverbs, wide synths and hats)"][i] + ". Click to change.";
  };
  ms.addEventListener("click", () => { P.eqMode[b].setChoiceIndex((eqModeAt(b) + 1) % 3); msRefresh(); onParamsChanged(); userEdited(); renderLearn(); });
  P.eqMode[b].valueChangedEvent.addListener(msRefresh);
  msRefresh();
  cell.appendChild(ms);
  cell.refresh = () => { knobRefresh(); refresh(); msRefresh(); };   // after the spectrum point moves this band
  return cell;
}
const EQ_MODES = ["STEREO", "MID", "SIDE"];
const eqModeAt = (b) => clamp(Math.round(P.eqMode[b].value * 2), 0, 2);
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
const dynImpactAt = (b) => P.dynImpact[b].getScaledValue();
const dynSustainAt = (b) => P.dynSustain[b].getScaledValue();
const dynBandActive = (b) => dynRatioAt(b) > 1.005 || Math.abs(dynGainAt(b)) >= 0.05 || Math.abs(dynImpactAt(b)) >= 0.5 || Math.abs(dynSustainAt(b)) >= 0.5;
const fmtPct = (v) => `${v > 0.5 ? "+" : ""}${Math.round(v)} %`;
const dynIsNeutral = () => ![0, 1, 2, 3].some(dynBandActive);
function dynRangeText(b) {
  const x = P.dynX.map((s) => s.getScaledValue());
  return b === 0 ? `below ${fmtHz(x[0])}` : b === 3 ? `above ${fmtHz(x[2])}` : `${fmtHz(x[b - 1])}\u2013${fmtHz(x[b])}`;
}
function dynBandCell() {
  const cell = document.createElement("div");
  cell.className = "cell dyn-pick";
  cell.innerHTML = `<span class="cell-label">Band</span><span class="cell-sub mono">pick · S = solo</span><div class="band-pick"></div>`;
  const wrap = cell.querySelector(".band-pick");
  const rows = DYN_BANDS.map((band, b) => {
    const row = document.createElement("div");
    row.className = "band-row";
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "band-btn";
    // Picking another band while listening to what a band's compression removes follows the pick.
    btn.addEventListener("click", () => { dynBand = b; if (MON.listen >= 9) setMonitor(9 + b); renderModule(); renderLearn(); });
    // S: hear only this band (one solo at a time; again = back to the master).
    const solo = document.createElement("button");
    solo.type = "button";
    solo.textContent = "S";
    solo.title = `Solo: hear only the ${band.name} band (${band.holds}), not the master`;
    solo.setAttribute("aria-label", `Solo the ${band.name} band`);
    solo.addEventListener("click", () => {
      const on = MON.listen !== 5 + b;
      dynBand = b;
      setMonitor(on ? 5 + b : 0);
      renderModule();
      toast(on ? `SOLO: only the ${band.name} band of Dynamics, not the master. S again (or BACK TO MASTER) returns.` : "Back to the master.");
    });
    row.append(btn, solo);
    wrap.appendChild(row);
    return { btn, solo };
  });
  // Channels: what Dynamics works on (STEREO / MID / SIDE), for every band.
  const ms = document.createElement("button");
  ms.type = "button";
  ms.className = "pill mono ms-btn dyn-ms";
  ms.addEventListener("click", () => { P.dynChannels.setChoiceIndex((dynChannelsAt() + 1) % 3); cell.refresh(); onParamsChanged(); userEdited(); renderLearn(); });
  P.dynChannels.valueChangedEvent.addListener(() => cell.refresh());
  cell.appendChild(ms);
  const msRefresh = () => {
    const i = dynChannelsAt();
    ms.textContent = ["STEREO", "MID ONLY", "SIDE ONLY"][i];
    ms.classList.toggle("pill-red", i > 0);
    ms.classList.toggle("pill-blue", i === 0);
    ms.title = ["Dynamics works on the whole mix", "Dynamics works only on the centre (vocal, kick, 808, snare); the wide parts pass untouched", "Dynamics works only on the stereo edges (wide synths, reverbs, wide hats); the centre passes untouched"][i] + ". Click to change.";
  };
  cell.refresh = () => { msRefresh(); refreshRows(); };
  const refreshRows = () => rows.forEach(({ btn, solo }, b) => {
    btn.className = "pill mono band-btn " + (b === dynBand ? "pill-red" : "pill-blue");
    btn.setAttribute("aria-pressed", String(b === dynBand));
    const gr = M.dynGr[b] || 0;
    btn.textContent = `${DYN_BANDS[b].short} ${dynBandActive(b) && gr < -0.05 ? fmtNum(gr) : ""}`.trim();
    const soloed = MON.listen === 5 + b;
    solo.className = "pill mono solo " + (soloed ? "pill-red on" : "pill-blue");
    solo.setAttribute("aria-pressed", String(soloed));
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
    liveKnobCell({ label: "Impact", sub: "hit punch", state: P.dynImpact[b], def: 0, minText: `${MINUS}100`, maxText: "+100", fmt: fmtPct }),
    liveKnobCell({ label: "Sustain", sub: "tail length", state: P.dynSustain[b], def: 0, minText: `${MINUS}100`, maxText: "+100", fmt: fmtPct }),
    split,
  ];
}
const dynCellsByBand = [0, 1, 2, 3].map(dynBandCells);
const dynCells = () => [dynPicker, ...dynCellsByBand[dynBand]];
function dynChannelsAt() { return clamp(Math.round(P.dynChannels.value * 2), 0, 2); }

// Glue cells: one bus compressor for the whole mix.
const GLUE_CHARS = ["Bus", "Opto", "Tube"];
const glueCharAt = () => clamp(Math.round(P.glueChar.value * 2), 0, 2);
const glueRatioAt = () => P.glueRatio.getScaledValue();
const glueGainAt = () => P.glueGain.getScaledValue();
const glueIsNeutral = () => glueRatioAt() <= 1.005 && Math.abs(glueGainAt()) < 0.05;
const fmtIgnore = (v) => (v <= 20.5 ? "off" : `${Math.round(v)} Hz`);
function glueReleaseCell() {
  const cell = liveKnobCell({ label: "Release", sub: "let go", state: P.glueRel, def: 300, minText: "50", maxText: "1200", fmt: fmtMs });
  cell.classList.add("glue-rel");
  const pill = document.createElement("button");
  pill.type = "button";
  pill.className = "pill mono ms-btn glue-auto";
  const pillRefresh = () => {
    const on = !!P.glueAuto.getValue();
    pill.textContent = on ? "AUTO" : "FIXED";
    pill.classList.toggle("pill-red", on);
    pill.classList.toggle("pill-blue", !on);
    pill.title = on ? "Auto: lets go quickly after short peaks, at the Release time on long loud passages. Click for Fixed."
                    : "Fixed: always lets go at the Release time. Click for Auto.";
  };
  pill.addEventListener("click", () => { P.glueAuto.setValue(!P.glueAuto.getValue()); pillRefresh(); onParamsChanged(); userEdited(); renderLearn(); });
  P.glueAuto.valueChangedEvent.addListener(pillRefresh);
  pillRefresh();
  cell.appendChild(pill);
  const knobRefresh = cell.refresh;
  cell.refresh = () => { knobRefresh(); pillRefresh(); };
  return cell;
}
const glueCells = [
  segCell({ label: "Character", sub: "flavour", state: P.glueChar, labels: GLUE_CHARS, notes: ["clean · firm", "smooth · slow", "softest · round"] }),
  liveKnobCell({ label: "Threshold", sub: "starts at", state: P.glueThr, def: -20, minText: `${MINUS}40`, maxText: "0", fmt: (v) => `${fmtNum(v, 1)} dB` }),
  liveKnobCell({ label: "Ratio", sub: "how firm", state: P.glueRatio, def: 1, minText: "1:1", maxText: "10:1", fmt: (v) => (v <= 1.005 ? "off" : `${v.toFixed(1)}:1`) }),
  liveKnobCell({ label: "Attack", sub: "grab speed", state: P.glueAtk, def: 10, minText: "0.1", maxText: "30", fmt: fmtMs }),
  glueReleaseCell(),
  liveKnobCell({ label: "Ignore Lows", sub: "keeps 808 out", state: P.glueSc, def: 90, minText: "off", maxText: "250", fmt: fmtIgnore }),
  liveKnobCell({ label: "Mix", sub: "parallel", state: P.glueMix, def: 100, minText: "dry", maxText: "wet", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Gain", sub: "after glue", state: P.glueGain, def: 0, minText: `${MINUS}12`, maxText: "+12", fmt: fmtSigned }),
  barCell({ label: "Reduction", sub: "glue amount", minText: "0", maxText: `${MINUS}12 dB`, id: "gluemeter" }),
];
function satChannelsAt() { return clamp(Math.round(P.satChannels.value * 2), 0, 2); }

// Saturation cells
const SAT_MODES = ["Tape", "Tube", "Clip"];
const satModeIndex = () => clamp(Math.round(P.satMode.value * 2), 0, 2);
const satDriveAt = () => P.satDrive.getScaledValue();
const satMixAt = () => P.satMix.getScaledValue();
const satIsNeutral = () => satDriveAt() < 0.05 || satMixAt() < 0.05;
const SAT_FOCUS = ["Full", "Bass only", "Highs only", "Bass + Highs"];
const satFocusIndex = () => clamp(Math.round(P.satFocus.value * 3), 0, 3);   // four choices: value = index / 3
const satBassOnly = () => satFocusIndex() === 1;
const SAT_FOCUS_TAG = ["", " · BASS", " · HIGHS", " · BASS+HI"];
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
    note.textContent = ["smooth · warm", "rich · warm low end", "firm · bright"][idx];
  }
  P.satMode.valueChangedEvent.addListener(() => { refresh(); onParamsChanged(); });
  P.satMode.propertiesChangedEvent.addListener(refresh);
  cell.refresh = refresh;
  refresh();
  return cell;
}
const satCells = [
  satModeCell(),
  segCell({ label: "Focus", sub: "which part", state: P.satFocus, labels: ["Full", "Bass", "Highs", "Bass+Hi"],
            notes: ["whole mix", "808 · kick → phones", "sheen on hats", "808 + hats, vocal clean"] }),
  segCell({ label: "Channels", sub: "stereo part", state: P.satChannels, labels: ["Stereo", "Mid", "Side"],
            notes: ["both", "centre only", "edges only"] }),
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
  cell.innerHTML = `<span class="cell-label">Model Hears</span><span class="cell-sub mono">loudness share \u00b7 mix in</span>
    <div class="stemhear">${STEMS.map((n, k) => `<div class="sh-row"><span class="sh-name mono">${n[0]}</span>
      <div class="sh-bar screen"><div class="sh-fill" id="sh-fill-${k}"></div></div><span class="sh-val mono" id="sh-val-${k}"></span></div>`).join("")}</div>`;
  return cell;
}
// Keep Loudness: a switch whose line underneath shows how much level it is holding right now.
function stemKeepCell() {
  const cell = toggleCell({ label: "Keep Loudness", sub: "balance, not level", state: P.stemKeep, onText: "ON", offText: "OFF" });
  const v = cell.querySelector(".cell-value");
  v.id = "stemkeep-val";
  v.removeAttribute("style");
  return cell;
}
const stemKeepText = () => !P.stemKeep.getValue() ? "level moves with you"
  : P.bypass.getValue() || !P.stemOn.getValue() || stemIsNeutral() ? "nothing to hold"
  : `Keeping level: ${fmtSigned(Math.abs(M.stemKeep || 0) < 0.05 ? 0 : M.stemKeep)}`;
const stemCells = [
  ...STEMS.map((name, k) => liveKnobCell({ label: name, sub: k === 3 ? "synths · keys · fx" : "level", state: P.stemGain[k], def: 0,
                                           minText: `${MINUS}12`, maxText: "+12", fmt: fmtSigned })),
  stemKeepCell(),
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
// Target: what Tone Steady holds the song to. The song's own tone (learned while it played: LEARN here,
// or Auto-Master) or, without one, the last ~10 s of music. Not a parameter: saved with the song by the plug-in.
const fmtTime = (sec) => `${Math.floor(Math.max(0, sec) / 60)}:${String(Math.floor(Math.max(0, sec) % 60)).padStart(2, "0")}`;
function tsTargetCell() {
  const cell = document.createElement("div");
  cell.className = "cell";
  cell.innerHTML = `<span class="cell-label">Target</span><span class="cell-sub mono">what it holds to</span>
    <div class="seg seg-v"><button type="button" id="ts-learn" title="Play your whole song: Tone Steady learns its overall tone, so playback and export sound the same from any start point">LEARN</button><button type="button" id="ts-clear" title="Forget the song's tone: follow the last ~10 seconds of music instead">CLEAR</button></div>
    <span class="cell-value mono os-note" id="ts-target-val"></span>`;
  const learn = cell.querySelector("#ts-learn"), clear = cell.querySelector("#ts-clear"), val = cell.querySelector("#ts-target-val");
  learn.addEventListener("click", async () => {
    if (M.amState === 1 && M.amMode === 1) { learnView = "listen"; renderLearn(); return; }
    if (M.amState !== 0) { toast("Auto-Master is busy. Let it finish (or cancel it), then press LEARN."); return; }
    const started = await amStart("tone");
    if (started === false) { toast("Masterology isn't ready yet: press play in your DAW once, then press LEARN again."); return; }
    toneRequested = true;
    Object.assign(M, { amState: 1, amMode: 1, amProgress: 0, amHearing: false, amHeard: 0, amLoudAt: -1, amCanFinish: false });
    if (learnOn) learnView = "listen";
    updateAmButton(); renderLearn(); refresh();
    toast("Play your whole song from the start: Tone Steady learns its tone and finishes by itself at the end.");
  });
  clear.addEventListener("click", async () => {
    if (!M.tsTarget) return;
    try { await tsClearTarget(); } catch (e) { /* the next frame shows the state either way */ }
    M.tsTarget = 0;
    userEdited();
    refresh(); renderLearn();
    toast("Tone Steady follows the music again (the last ~10 seconds).");
  });
  function refresh() {
    const learning = M.amState === 1 && M.amMode === 1;
    learn.textContent = learning ? "LEARNING" : "LEARN";
    learn.className = "pill " + (learning ? "pill-red" : "pill-blue");
    learn.setAttribute("aria-pressed", String(learning));
    clear.className = "pill pill-blue" + (M.tsTarget ? "" : " is-disabled");
    clear.disabled = !M.tsTarget;
    val.textContent = !M.tsTarget ? "following the music" : M.tsFar ? `another song? \u00b7 ${fmtTime(M.tsTarget)}` : `your song \u00b7 ${fmtTime(M.tsTarget)}`;
  }
  cell.refresh = refresh;
  refresh();
  return cell;
}
const tsTarget = tsTargetCell();
const tsCells = [
  liveKnobCell({ label: "Amount", sub: "how much drift", state: P.tsAmount, def: 0, minText: "off", maxText: "100", fmt: (v) => `${Math.round(v)} %` }),
  liveKnobCell({ label: "Range", sub: "largest cut", state: P.tsRange, def: 3, minText: "1", maxText: "6 dB", fmt: (v) => `${MINUS}${v.toFixed(1)} / +${(v / 2).toFixed(1)}` }),
  segCell({ label: "Speed", sub: "reaction", state: P.tsSpeed, labels: TS_SPEEDS, notes: ["1 s \u00b7 sections", "0.4 s \u00b7 phrases", "0.15 s \u00b7 notes"] }),
  tsTarget,
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
  barCell({ label: "Restoring", sub: "lift now", minText: "0", maxText: "10 dB", id: "ulmeter" }),
];

function renderModule() {
  const m = modules.find((x) => x.id === selected);
  const row = $("knob-row");
  row.innerHTML = "";
  row.classList.toggle("dyn", m.id === "dyn");
  row.classList.toggle("glue", m.id === "glue");
  $("module-name").textContent = m.name.toUpperCase();
  $("soon-badge").hidden = !!m.live;
  $("btn-bypass").hidden = !m.live;
  if (m.live) {
    row.classList.remove("soon");
    ({ ul: ulCells, clip: clipperCells, eq: eqCells, stem: stemCells, ts: tsCells, rt: rtCells, bass: bassCells, glue: glueCells, dyn: dynCells(), img: imagerCells, sat: satCells, max: maximizerCells })[m.id].forEach((c) => row.appendChild(c));
    $("module-note").textContent = "Live · drag · dbl-click number = type";
    refreshHeadButton();
  } else {
    row.classList.add("soon");
    m.cells.forEach((c) => row.appendChild(staticKnobCell(c)));
    $("module-note").textContent = "Designed · not active in this build";
    $("soon-text").textContent = m.soon;
  }
  refreshListenButton();
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
    if (e.button !== 0) return;
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
  n.addEventListener("lostpointercapture", end);   // e.g. the window lost the mouse mid-drag: the undo step still closes
  // Double-click: back to flat at the band's own frequency, as one undo step (both gestures overlap).
  n.addEventListener("dblclick", () => {
    const reset = [[P.eqGain[b], 0], [P.eqFreq[b], band.def]];
    for (const [s] of reset) s.sliderDragStarted();
    for (const [s, v] of reset) s.setNormalisedValue(scaledToNorm(s, v));
    for (const [s] of reset) s.sliderDragEnded();
    eqCells[b].refresh(); onParamsChanged(); userEdited();
  });
  const wheel = wheelStepper();
  n.addEventListener("wheel", (e) => {
    e.preventDefault();
    const steps = wheel(e);
    if (!steps) return;
    const gs = P.eqGain[b];
    gs.sliderDragStarted();
    gs.setNormalisedValue(scaledToNorm(gs, clamp(eqGainAt(b) + steps * (e.shiftKey ? 0.1 : 0.5), -EQ_MAX, EQ_MAX)));
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
function para(cls, text) { const p = document.createElement("p"); p.className = cls; p.textContent = D(text); return p; }
function head(text) { const s = document.createElement("span"); s.className = "l-h"; s.textContent = text; return s; }
function sep() { const d = document.createElement("div"); d.className = "l-sep"; return d; }
function item(name, value, why) {
  const d = document.createElement("div");
  d.className = "l-item";
  d.innerHTML = `<div class="l-item-head"><span class="l-item-name"></span>${value ? '<span class="pill pill-red mono l-item-val"></span>' : ""}</div><p class="l-item-why"></p>`;
  d.querySelector(".l-item-name").textContent = name;
  if (value) d.querySelector(".l-item-val").textContent = value;
  d.querySelector(".l-item-why").textContent = D(why);
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
    need.appendChild(document.createTextNode(D(fix.need)));
    d.appendChild(need);
    if (fix.steps && fix.steps.length) {
      const how = document.createElement("p");
      how.className = "l-p l-how";
      how.innerHTML = "<b>How:</b>";
      d.appendChild(how);
      const ol = document.createElement("ol");
      ol.className = "l-steps";
      for (const st of fix.steps) { const li = document.createElement("li"); li.textContent = D(st); ol.appendChild(li); }
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

// The panel is built off-screen and swapped in only when it differs from what is shown, so the live numbers
// (refreshed every second and as controls move) don't take away a button you are pressing, the keyboard focus
// or a tooltip. renderLearn(): after something you did (a click, a key), shown at once. refreshLearn(): the
// live numbers, which wait while you press inside the panel or the keyboard is on one of its buttons.
const learnBody = $("learn-body");
const FOCUSABLE = "button:not([disabled]), a[href], input, select, textarea, [tabindex]:not([tabindex='-1'])";
const learnControls = (root) => [...root.querySelectorAll(FOCUSABLE)].map((e) => `${e.tagName}#${e.id}.${e.className}`).join("|");
let learnShown = "";          // the HTML on screen
let learnPending = false;     // a refresh waits for the press or the focus to leave
let learnPress = 0;           // when a press inside the panel started (0: none)
learnBody.addEventListener("pointerdown", () => { learnPress = performance.now(); }, true);
function learnRelease() {
  if (!learnPress) return;
  learnPress = 0;
  if (learnPending) setTimeout(refreshLearn, 0);   // after the click that ends the press
}
for (const t of ["pointerup", "pointercancel"]) window.addEventListener(t, learnRelease, true);
window.addEventListener("blur", learnRelease);
learnBody.addEventListener("focusout", (e) => { if (learnPending && !learnBody.contains(e.relatedTarget)) setTimeout(refreshLearn, 0); });

function renderLearn() { showLearn(true); }
function refreshLearn() { showLearn(false); }
function buildLearn() {
  const body = document.createElement("div");
  renderLearnBody(body);
  if (learnOn && learnView === "module") addMonitorLearn(body);
  if (learnOn && learnView === "module") addMixologyLearn(body);
  if (learnOn && (learnView === "module" || learnView === "report")) addAutoAnalyzeStrip(body);
  if (learnOn && learnView === "module" && M.amState === 1) {
    // Still listening while you look at a module: a way back to the listening view.
    const link = document.createElement("button");
    link.type = "button";
    link.className = "l-link";
    link.textContent = `← ${M.amMode === 1 ? "LEARNING TONE" : "LISTENING"} · ${fmtTime(M.amHeard || 0)} HEARD`;
    link.addEventListener("click", () => { learnView = "listen"; renderLearn(); });
    body.prepend(link);
  }
  return body;
}
function showLearn(now) {
  let next;
  try { next = buildLearn(); } catch (e) { reportOnce("learn", e); return; }
  const html = next.innerHTML;
  if (html === learnShown) { learnPending = false; return; }
  const pressing = learnPress && performance.now() - learnPress < 4000;   // (a press the window never saw end is let go)
  const focused = learnBody.contains(document.activeElement) && document.activeElement !== learnBody ? document.activeElement : null;
  // On a button with the keyboard: new numbers wait; new or changed buttons come in, with the focus kept.
  if (pressing || (!now && focused && learnControls(next) === learnControls(learnBody))) { learnPending = true; return; }
  learnPending = false;
  const index = focused ? [...learnBody.querySelectorAll(FOCUSABLE)].indexOf(focused) : -1;
  learnBody.replaceChildren(...next.childNodes);
  learnShown = html;
  if (focused) {
    const all = [...learnBody.querySelectorAll(FOCUSABLE)];
    const same = (focused.id && all.find((e) => e.id === focused.id))
      || all.find((e) => e.tagName === focused.tagName && e.textContent === focused.textContent) || all[Math.min(index, all.length - 1)];
    if (same) same.focus({ preventScroll: true });
  }
}
// A part of the window that fails says so once in the console instead of stopping everything else.
const reported = new Set();
function reportOnce(part, e) {
  if (reported.has(part)) return;
  reported.add(part);
  console.error(`Masterology UI (${part}):`, e);
}

function renderLearnBody(body) {
  if (!learnOn) {
    const off = document.createElement("div");
    off.className = "l-off";
    off.innerHTML = `<span class="wm">LEARN IS OFF</span><p>Turn it on to see what each control is doing to your track, in plain producer language.</p>`;
    body.appendChild(off);
    return;
  }

  if (learnView === "listen" && M.amState !== 0) { renderListenLearn(body); return; }
  if (learnView === "listen") learnView = "module";
  if (learnView === "report" && amReport) { renderReport(body); return; }
  if (learnView === "auto") { renderAutoLearn(body); return; }
  if (learnView === "ref") { renderRefLearn(body); return; }
  if (learnView === "history") { renderHistoryLearn(body); return; }
  if (learnView === "snap") { renderSnapLearn(body); return; }

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
  if (m.id === "glue") { renderGlueLearn(body); liftWarning(body); prependAmSection(body, "glue"); return; }
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

  if (louderPartDb > 0)
    body.appendChild(tip("LOUDER THAN WHAT AUTO-MASTER HEARD",
      `This part of your mix is about ${fmtNum(louderPartDb, 1)} dB louder than the loudest part Auto-Master heard, so here the master is louder and more limited than it planned. It probably didn't hear this part.`, false,
      { need: "Yes, if this is the chorus or drop: Auto-Master should hear the loudest part.",
        steps: ["Click Auto-Master and play the whole song from the start.", "Let it play to the end: it finds the loudest part by itself and finishes when the song ends."] }));
  body.appendChild(head("WHAT'S HAPPENING"));
  if (bypassed) {
    body.appendChild(para("l-p", "You're on A: the original mix, untouched. Switch to B to hear the Maximizer."));
  } else if (!playing) {
    body.appendChild(para("l-p", "Press play in your DAW. As the music runs I'll describe what the Maximizer is doing to it."));
  } else {
    const st = M.shortTerm > -99 ? `${fmtNum(M.shortTerm)} LUFS` : "—";
    const it = M.integrated > -99 ? `Its Integrated loudness (since the meter last restarted) is ${fmtNum(M.integrated)} LUFS.` : "Its Integrated loudness is still being measured.";
    const gain = louderBy();
    const vsMix = gain === null ? "" : `, ${gain >= 0 ? "+" : MINUS}${Math.abs(gain).toFixed(1)} dB ${gain >= 0 ? "louder" : "quieter"} than your mix`;
    const grText = peakGr < -0.2 ? `The limiter is taking up to ${fmtNum(-peakGr)} dB off the loudest moments.` : "The limiter is barely touching it: the peaks already fit under the ceiling.";
    body.appendChild(para("l-p", `Your master is running at ${st} short-term${vsMix}. ${it} ${grText}`));
  }

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Drive", `+${drive.toFixed(1)} dB`,
    drive < 0.05
      ? "No push yet. Turn Drive up to make the master louder; the limiter catches the peaks so nothing clips."
      : P.clipOn.getValue()
        ? `Pushing the mix ${drive.toFixed(1)} dB into the Soft Clipper and then the limiter. The quiet and medium parts come up by that much; the loud hits are shaved and held at the ceiling.`
        : `Pushing the mix ${drive.toFixed(1)} dB into the limiter. The quiet and medium parts come up by that much; the loud hits are held at the ceiling.`));
  const trim = P.inTrim.getScaledValue();
  body.appendChild(item("Input Trim", `${fmtNum(trim, 1)} dB`,
    trim > -0.05
      ? "Your mix goes into the chain as it is. Auto-Master turns this down only when the master would otherwise land above your target."
      : `Turns your mix down ${fmtNum(-trim, 1)} dB before the whole chain (all 12 modules). Drive can only push up; this is how Masterology masters a mix that would otherwise land above your target. A (bypass) ignores it.`));
  body.appendChild(item("Ceiling", `${fmtNum(ceiling, 1)} dBTP`,
    `Nothing leaves Masterology above ${fmtNum(ceiling, 1)} dBTP on the true-peak meter streaming services use (it counts peaks between samples too). ` +
    (ceiling > -0.95 ? "Above −1 dBTP, the MP3/AAC versions streaming services make are likely to clip. −1.0 is the usual choice; for loud masters (around −9 LUFS and louder) −1.5 (what Auto-Master sets for Loud and Club) to −2 (Spotify's advice) is safer."
                     : "That leaves room for the MP3/AAC conversion streaming services do. The louder the master, the higher its MP3/AAC copy peaks: at −9 LUFS and louder, use −1.5 (Auto-Master's choice for Loud and Club) or −2 (Spotify's advice), and the converted copies stay at or close to 0 dBTP instead of going a dB over.")));
  const bf = P.maxBassFirst.getScaledValue(), bassCut = M.bassCut || 0;
  body.appendChild(item("Bass First", bf < 0.5 ? "off" : `${Math.round(bf)} %`,
    bf < 0.5 ? "Off: when the 808 pushes the master into the limiter, the whole mix is turned down with it, so the vocal and hats dip on every 808 hit."
             : `When the 808 (below 120 Hz) pushes a peak over the ceiling, the low end is turned down first (up to 12 dB) instead of the whole mix, so the vocal and hats stop dipping with it.${playing && bassCut < -0.05 ? ` Right now the low end is taking ${fmtNum(-bassCut, 1)} dB on the loudest 808 hits.` : ""} The trade: on those hits the 808 sits a little lower. 50–100 % for trap with a heavy 808.`));
  const dith = clamp(Math.round(P.maxDither.value * 2), 0, 2);
  body.appendChild(item("Dither", ["Off", "16-bit", "24-bit"][dith],
    dith === 0 ? "Off: right for a 24-bit or 32-bit float export. Turn on 16-bit only when you export a 16-bit WAV (CD, some distributors)."
    : `On for ${dith === 1 ? "16" : "24"}-bit: adds a very quiet hiss (about ${dith === 1 ? "−98" : "−146"} dBFS) so quiet fades don't turn gritty when {daw} cuts the export to ${dith === 1 ? "16" : "24"} bits. Export at exactly that bit depth, with nothing after Masterology changing the level (Stereo Out fader at 0 dB).`));
  body.appendChild(item("Release", `${release} ms`,
    adaptive
      ? `Adaptive around ${release} ms: recovers quickly after single hits (drums) and moves slowly on sustained sound (808s, pads), so it doesn't pump.`
      : `Fixed at ${release} ms. Shorter sounds louder but can distort bass; longer is smoother but can pump on drums.`));

  const gain = louderBy();
  if (bypassed || !playing) {
    body.appendChild(tip("TIP", "Set Drive by ear while watching Short-term in the footer. Most streaming releases land between −14 and −9 LUFS.", true));
  } else if (gain !== null && drive >= 9 && drive + trim - gain > 6) {
    // Input Trim turns the mix down before Drive pushes it, so the push that counts is Drive + Trim.
    const push = drive + trim;
    body.appendChild(tip("DIMINISHING RETURNS",
      `${trim < -0.05 ? `Drive (+${drive.toFixed(1)} dB) after Input Trim (${fmtNum(trim, 1)} dB) pushes the mix ${push >= 0 ? "+" : MINUS}${Math.abs(push).toFixed(1)} dB` : `Drive is at +${drive.toFixed(1)} dB`}, but the master is only ${gain.toFixed(1)} dB louder than your mix. The loud hits already reach the ceiling, so the extra Drive is going into squashing them, not into loudness. ` +
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
  } else if (!refInfo.profile && M.integrated > -99 && M.integrated > [-14, -9, -7][clamp(Math.round(P.amTarget.value * 2), 0, 2)] + 1) {
    const tgt = [-14, -9, -7][clamp(Math.round(P.amTarget.value * 2), 0, 2)];
    body.appendChild(tip("HEADS UP",
      `Your master is at ${fmtNum(M.integrated)} LUFS, louder than your ${["Stream", "Loud", "Club"][clamp(Math.round(P.amTarget.value * 2), 0, 2)]} ${fmtNum(tgt, 0)} target. Spotify and YouTube play everything at about −14 LUFS, so extra loudness is turned down there: it buys density, not volume.`, false,
      { need: "Only if it sounds squashed. Loud is a style choice (common in Trap), not a mistake.",
        steps: ["Turn on MATCH and flip A/B. If B sounds flatter or the drums weaker, it's too loud.",
                "To bring it down: lower Drive (above) until Integrated reads your target; at Drive 0, lower Input Trim. Or click Auto-Master again.",
                "For streaming only, choose Stream −14 or Loud −9 as the Target and run Auto-Master again."] }));
  } else if (M.integrated > -99 && M.integrated < -16) {
    body.appendChild(tip("SOUNDS CLEAN", "Plenty of dynamics left. For a streaming release, you can push Drive until Integrated reaches around −14 to −10 LUFS.", true));
  } else {
    body.appendChild(tip("SOUNDS HEALTHY", "Limiting is moderate, so transients should stay intact. When you A/B, turn on MATCH so B isn't simply louder: louder always sounds \u201cbetter\u201d.", true));
  }
  liftWarning(body);
  prependAmSection(body, "max");
}

// How to read the dotted genre line on the spectrum (EQ Learn).
function genreLineItem() {
  const full = genreCurve ? genreCurve.name : GENRES[comboIndex(P.amGenre)];
  const read = `Where your red master line sits above the dotted line, your master has more of that range than typical ${genreShort(full) === "Trap" ? "trap" : genreShort(full)}; below it, less. Small differences (1–2 dB) are normal, and below about 60 Hz all the lines get rough.`;
  if (refInfo.profile && !genreLineOn())
    return item(`Dotted line · ${genreShort(full)} target`, null, `Hidden while a reference is loaded: Auto-Master aims for the reference (the yellow dashed line) instead. Click "${genreShort(full)} target" under the spectrum to show it too. ${read}`);
  if (!genreLineOn())
    return item(`Dotted line · ${genreShort(full)} target`, null, `The typical tone of ${full} masters, the one Auto-Master's EQ aims for. It's switched off: click "${genreShort(full)} target" under the spectrum to show it.`);
  return item(`Dotted line · ${genreShort(full)} target`, null, `The typical tone of ${full} masters, the one Auto-Master's EQ aims for, shown at the loudness of what's playing. ${read}`);
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
  else body.appendChild(para("l-p", "Shaping the tone: " + moved.map((x) => `${sgn(x.g)} ${x.band.name} at ${fmtHz(x.f)}${eqModeAt(x.b) ? ` (${["", "centre only", "sides only"][eqModeAt(x.b)]})` : ""}`).join(", ") +
    ". The blue curve on the spectrum is the change; the red line is the master after the whole chain."));

  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  const MS_WHY = ["", " MID: only the centre of the mix moves (vocal, kick, 808, snare); the stereo edges stay as they were.",
                  " SIDE: only the stereo edges move (width, reverbs, wide synths and hats); the centre stays as it was."];
  if (moved.length && on) moved.forEach((x) => body.appendChild(item(`${x.band.name} · ${fmtHz(x.f)}${eqModeAt(x.b) ? ` · ${EQ_MODES[eqModeAt(x.b)]}` : ""}`, sgn(x.g), (x.g > 0 ? x.band.boost : x.band.cut) + MS_WHY[eqModeAt(x.b)])));
  else modules.find((m) => m.id === "eq").controls.forEach(([n, w]) => body.appendChild(item(n, null, w)));
  body.appendChild(genreLineItem());

  if (!bypassed && on && !moved.some((x) => eqModeAt(x.b)))
    body.appendChild(tip("TRY MID / SIDE", "The button under each knob picks what the band works on. Air on SIDE (+1 to +2 dB) opens the width without making the vocal brighter; Low Mid on MID (−1 dB) clears mud in the centre while the wide parts keep their body.", true));
  if (bypassed || !on) {
    body.appendChild(tip("TIP", "Cut before you boost: a 1–2 dB Low Mid cut often sounds clearer than boosting the highs. It can end up louder too, but only because the Maximizer turns the headroom the cut frees into level.", true));
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
      : `Listens to the low end's own average level. When an 808 note or kick jumps more than ${(6 - 3 * tame / 100).toFixed(1)} dB above it, that note is turned down, so the Maximizer doesn't grab the whole mix on big bass notes. It listens for about a second before it cuts anything, so the first notes of a song aren't cut harder than the rest.`));
  body.appendChild(item("Punch", `${punch} %`,
    punch < 1 ? "Off: kicks stay as they are."
      : `Adds up to ${(4 * punch / 100).toFixed(1)} dB of thump in the first moments of each kick. It spots a kick by the click at its start, so 808 notes on their own aren't boosted and the sub stays where it was: the kick hits harder without the low end getting boomier.`));

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
  body.appendChild(item("Correlation", `${corr < 0 ? MINUS : "+"}${Math.abs(corr).toFixed(2)}`, "How mono-safe the sound is right after the Imager (the modules after it barely change it): +1 is mono, around +0.3 to +0.7 is a healthy wide mix, below 0 means parts cancel when played on one speaker (phones, club PAs)."));
  if (on && !bypassed && corr < 0.1 && performance.now() - lastAudioTime < 1500)
    body.appendChild(tip("HEADS UP", "Correlation is near or below zero: parts of the mix will cancel on mono speakers (phones, club PAs).", false,
      { need: "Yes.", steps: ["Set Mids and Highs back to 100 %.", "If Correlation is still low, check the mix for stereo wideners or out-of-phase effects: press MONO in Masterology's top bar and listen for parts that disappear."] }));
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
      const im = dynImpactAt(k), imNow = M.dynImpact[k] || 0;
      const impactText = Math.abs(im) < 0.5 ? "" : `, Impact ${fmtPct(im)}${playing ? ` (hits ${(Math.abs(imNow) < 0.05 ? im : imNow) >= 0 ? "up" : "down"} ${fmtNum(Math.abs(imNow), 1)} dB right now)` : ""}`;
      const su = dynSustainAt(k), suNow = M.dynSustain[k] || 0;
      const sustainText = Math.abs(su) < 0.5 ? "" : `, Sustain ${fmtPct(su)}${playing ? ` (tails ${(Math.abs(suNow) < 0.05 ? su : suNow) >= 0 ? "up" : "down"} ${fmtNum(Math.abs(suNow), 1)} dB right now)` : ""}`;
      return `${DYN_BANDS[k].name} (${dynRangeText(k)}): ${r > 1.005 ? `${r.toFixed(1)}:1` : "no compression"}${Math.abs(dynGainAt(k)) >= 0.05 ? `, ${fmtSigned(dynGainAt(k))}` : ""}${r > 1.005 && playing ? `, pulling down ${gr < -0.05 ? fmtNum(-gr) : "0.0"} dB right now` : ""}${impactText}${sustainText}.`;
    });
    body.appendChild(para("l-p", parts.join(" ")));
  }
  const dch = dynChannelsAt();
  if (dch && on && !bypassed) body.appendChild(item("Channels", ["Stereo", "Mid only", "Side only"][dch], dch === 1
    ? "Every band works only on the centre of the mix (vocal, kick, 808, snare). The wide parts (reverbs, wide synths and hats) pass through untouched, so the compression glues the middle without pulling the width in."
    : "Every band works only on the stereo edges (wide synths, reverbs, wide hats). The centre (vocal, kick, 808) passes through untouched: good for calming wide, harsh hats or a reverb that pumps. The edges are quieter than the full mix, so the same Threshold catches less: set it a few dB lower."));
  body.appendChild(sep());
  body.appendChild(head(`${band.name.toUpperCase()} BAND (${dynRangeText(b)})`));
  body.appendChild(para("l-p", `Holds the ${band.holds}.`));
  body.appendChild(item("Threshold", `${fmtNum(thr, 1)} dB`, "Where the band starts being held down. Lower = more of the music is compressed. Set it so the reduction only moves on the loudest hits."));
  body.appendChild(item("Ratio", `${ratio.toFixed(1)}:1`, ratio <= 1.005 ? "1:1: this band isn't compressed." : ratio <= 2.5 ? "A gentle mastering ratio: evens the band out without flattening it." : ratio <= 4 ? "Firm: the band gets noticeably steadier. Check the punch with MATCH." : "Heavy for a master: the band will sound squashed. 1.5:1 to 3:1 is typical."));
  body.appendChild(item("Attack", fmtMs(atk), atk >= 20 ? "Slow: the start of each hit gets through before the band is turned down, so drums keep their punch." : atk >= 5 ? "Medium: catches most of each hit, a little of the snap stays." : b === 0 ? "Fast: the low band still needs about 20 ms to react (any quicker and bass notes would wobble), so a kick's first moment gets through. Smoother but less punchy." : "Fast: catches peaks within a few ms, smoother but less punchy."));
  body.appendChild(item("Release", fmtMs(rel), (rel <= 80 ? "Fast: recovers between hits; too fast can sound like fluttering on bass." : rel <= 300 ? "Medium: recovers between beats, the usual choice for a master." : "Slow: smooth and steady, evens out whole phrases.") + (P.dynAdaptive.getValue() ? " Adaptive release is on: quick after short hits, this setting on sustained notes." : "")));
  body.appendChild(item("Gain", fmtSigned(gain), Math.abs(gain) < 0.05 ? "The band's level after compression is unchanged." : gain > 0 ? "Turns the band up after compression: makes up what it lost, or brings it forward." : "Turns the band down after compression."));
  const im = dynImpactAt(b);
  body.appendChild(item("Impact", fmtPct(im), Math.abs(im) < 0.5 ? "Off: the hits in this band keep their own punch."
    : im > 0 ? `Makes the start of each hit in this band louder (${band.holds}), so it punches harder. The steady sound in between isn't touched, so the loudness barely moves, but the peaks rise: the Maximizer will work a little harder.`
    : `Softens the start of each hit in this band (${band.holds}): smoother, less spiky. Good for hats or a snare that pokes out.`));
  const su = dynSustainAt(b);
  body.appendChild(item("Sustain", fmtPct(su), Math.abs(su) < 0.5 ? "Off: the tails in this band die away as they were mixed."
    : su > 0 ? `Lifts the tail of each sound in this band as it dies away${b === 0 ? ": longer, fuller 808s" : b === 3 ? ": longer cymbals and reverb" : ""}. The band's loudness is kept, so the hits sit a little lower in exchange.`
    : `Pulls the tail of each sound in this band down faster${b === 0 ? ": tighter, shorter 808s that don't blur into the next note" : b === 3 ? ": drier hats and cymbals" : ""}. The band's loudness is kept, so the hits come forward a little.`));
  body.appendChild(item("Split", fmtHz(P.dynX[Math.min(b, 2)].getScaledValue()), b < 3 ? "Where this band ends and the next begins." : "Where this band starts."));
  const worst = Math.min(...[0, 1, 2, 3].map((k) => (dynRatioAt(k) > 1.005 ? M.dynGr[k] || 0 : 0)));
  if (on && !bypassed && playing && worst < -6)
    body.appendChild(tip("HEADS UP", `One band is being pulled down ${fmtNum(-worst)} dB. On a master, 1 to 3 dB of reduction is usually plenty; more starts to sound squashed.`, false,
      { need: "Yes, unless you want an obviously compressed sound.", steps: ["Click the band whose button shows the biggest number.", "Raise its Threshold until that number stays under about 3 dB.", "Or lower its Ratio to 2:1."] }));
  else if (on && [0, 1, 2, 3].some((k) => dynRatioAt(k) > 4))
    body.appendChild(tip("HEADS UP", "A band is above 4:1. That's heavy for a master.", false,
      { need: "Optional. Check with MATCH whether it sounds squashed.", steps: ["Set that band's Ratio to 2:1.", "Lower its Threshold a little if you still want the same control."] }));
  else if (on && [0, 1, 2, 3].some((k) => dynImpactAt(k) > 60))
    body.appendChild(tip("HEADS UP", "Impact above +60 % makes hits a lot spikier. The Maximizer has to pull those peaks down again, which can undo the punch.", false,
      { need: "Optional. Check with MATCH and watch the Maximizer's GR.", steps: ["Try Impact +20 to +40 % first.", "If the Maximizer's GR jumps on every kick, turn Impact down or use the Soft Clipper."] }));
  else if (on && [0, 1, 2, 3].every((k) => Math.abs(dynImpactAt(k)) < 0.5 && Math.abs(dynSustainAt(k)) < 0.5) && [0, 1, 2, 3].some((k) => dynRatioAt(k) > 1.005))
    body.appendChild(tip("TRY THIS", "Kick not hitting? On the LOW band, set Impact to +30 % and compare with MATCH: the kick's thump gets harder without making the beat louder. 808s blurring together? LOW band Sustain −30 % tightens them; +30 % makes them longer and fuller.", true));
  else
    body.appendChild(tip("TRY THIS", "On the LOW band: Ratio 2:1, Attack 30 ms, then lower the Threshold until the band pulls 1 to 2 dB on the kick and 808. The low end gets steadier without ducking the vocal. Compare with MATCH on.", true));
}

function renderGlueLearn(body) {
  const on = !!P.glueOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const ci = glueCharAt(), ratio = glueRatioAt(), thr = P.glueThr.getScaledValue(), atk = P.glueAtk.getScaledValue(), rel = P.glueRel.getScaledValue();
  const auto = !!P.glueAuto.getValue(), sc = P.glueSc.getScaledValue(), mix = P.glueMix.getScaledValue(), gain = glueGainAt(), gr = M.glueGr || 0;
  // The times the character really uses (dsp GlueCompressor kVoices): Opto grabs no faster than 10 ms and lets
  // go 1.5x slower; Tube grabs 1.5x slower (at least 1 ms) and lets go 1.3x slower.
  const atkReal = [Math.max(0.1, atk), Math.max(10, atk), Math.max(1, atk * 1.5)][ci], relReal = rel * [1, 1.5, 1.3][ci];
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Glue is off, so the mix passes through untouched."
    : ratio <= 1.005 ? (Math.abs(gain) >= 0.05 ? `Ratio is 1:1, so Glue only changes the level (${fmtSigned(gain)}). Set Ratio to 2:1 to start gluing.`
                                                : "Ratio is 1:1, so Glue isn't touching the sound. Set Ratio to 2:1, then lower Threshold until the Reduction meter shows 1–2 dB on the loudest parts.")
    : !playing ? "Press play to see how much Glue is holding the mix down."
    : gr > -0.3 ? "The mix is under the threshold right now, so Glue is barely working. Lower Threshold until the meter shows 1–2 dB on the loud parts."
    : `Glue is holding the mix down by ${fmtNum(-gr, 1)} dB${gr < -4 ? ": a lot for glue, the beat may lose its bounce (1–2 dB is the usual amount)" : gr < -2.5 ? ": firm glue, check with MATCH that the kick still punches" : ": gentle glue, the usual amount"}.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Character", GLUE_CHARS[ci], ["Bus: a clean, firm bus compressor, the classic glue on a hip-hop mix. It pulls the drums and the vocal together without colouring them.",
    "Opto: smooth and slow. It never grabs faster than about 10 ms and lets go gently, so it evens out the level and leaves the hits mostly alone.",
    "Tube: the softest. It starts working well under the threshold and firms up gently as the music gets louder: the most transparent way to add a little density."][ci]));
  body.appendChild(item("Threshold", `${fmtNum(thr, 1)} dB`, "Where it starts holding the mix down. Lower = more of the music is compressed."));
  body.appendChild(item("Ratio", ratio <= 1.005 ? "off" : `${ratio.toFixed(1)}:1`, ratio <= 1.005 ? "1:1: no compression."
    : ratio <= 2.5 ? "Classic glue: the mix moves as one without being flattened."
    : ratio <= 4.5 ? "Firm: the mix gets noticeably denser. Keep Attack slow so the drums still punch."
    : "Heavy for a whole mix: it will sound squashed unless Mix is low (parallel compression)."));
  body.appendChild(item("Attack", fmtMs(atk), (Math.abs(atkReal - atk) > 0.05 ? `${GLUE_CHARS[ci]} works this like ${fmtMs(atkReal)}${ci === 1 ? " (it never grabs faster than about 10 ms)" : ""}. ` : "")
    + (atkReal >= 10 ? "Slow: the start of each kick and snare gets through before Glue grabs, so the drums keep their punch."
      : atkReal >= 3 ? "Medium: catches part of each hit." : "Fast: grabs the hits themselves; smoother but less punchy, and it can roughen the 808.")));
  body.appendChild(item("Release", auto ? `${fmtMs(rel)} · auto` : fmtMs(rel), (ci > 0 ? `${GLUE_CHARS[ci]} lets go a little slower, about ${fmtMs(relReal)}. ` : "")
    + (auto ? "Auto: lets go quickly after a short peak, at the Release time on long loud passages. " : "")
    + (relReal <= 150 ? "Fast: recovers between beats; it can pump audibly." : relReal <= 500 ? "Medium: breathes with the beat, the usual glue setting." : "Slow: steady, evens out whole sections.")));
  body.appendChild(item("Ignore Lows", fmtIgnore(sc), sc <= 20.5 ? "Off: the 808 and kick drive the compressor, so every 808 note can pull the vocal and hats down (pumping). Try 90 Hz."
    : sc < 60 ? `Low: only the deepest sub (under ${Math.round(sc)} Hz) is kept out, so a loud 808 can still pump the vocal. 80–120 Hz keeps the 808 out.`
    : `The compressor hears the lows under ${Math.round(sc)} Hz much quieter (24 dB per octave), so the 808 and kick don't pump the rest of the mix. They are still turned down along with everything else.`));
  body.appendChild(item("Mix", `${Math.round(mix)} %`, mix >= 99.5 ? "Fully compressed sound."
    : `Parallel compression: ${Math.round(mix)} % compressed blended with the uncompressed mix, so the hits stay sharp while the quieter parts come up.`));
  body.appendChild(item("Gain", fmtSigned(gain), Math.abs(gain) < 0.05 ? "No level change. Compressing makes the mix a little quieter: raise the Maximizer's Drive by about the reduction to keep your loudness."
    : gain > 0 ? "Turns the mix up after Glue. Compare with MATCH: louder always sounds better." : "Turns the mix down after Glue."));
  if (on && !bypassed && ratio > 1.005)
    body.appendChild(tip("TRY THIS", "Bus, Ratio 2:1, Attack 10 ms, Release AUTO, Ignore Lows 90 Hz: lower Threshold until the meter shows 1–2 dB on the hook. Compare with MATCH: B should sound more together, not smaller.", true));
}

function renderSatLearn(body) {
  const on = !!P.satOn.getValue(), bypassed = !!P.bypass.getValue();
  const playing = performance.now() - lastAudioTime < 1500;
  const mode = SAT_MODES[satModeIndex()], drive = satDriveAt(), mix = satMixAt(), harm = M.satHarm, bass = satBassOnly();
  const focusNow = satFocusIndex(), highs = focusNow === 2, both = focusNow === 3;
  const chNow = satChannelsAt();
  const tags = [focusNow ? SAT_FOCUS[focusNow] : "", ["", "Mid", "Side"][chNow]].filter(Boolean);
  const what = tags.length ? `${mode} (${tags.join(", ")})` : mode;
  body.appendChild(head("WHAT'S HAPPENING"));
  body.appendChild(para("l-p", bypassed ? "You're on A: the original mix, untouched. Switch to B to hear the chain."
    : !on ? "Saturation is off, so nothing is added."
    : drive < 0.05 ? "Drive is at 0, so Saturation passes the sound untouched. Turn Drive up to add harmonics."
    : mix < 0.05 ? "Mix is at 0 %, so you only hear the dry signal."
    : !playing ? "Press play to see how much Saturation is adding."
    : harm <= -60 && chNow === 2 ? `${what} is on, but the stereo edges carry almost nothing here (a centred or mono mix), so Side has little to work on.`
    : harm <= -60 ? `${what} is on, but ${bass ? "the low end" : highs ? "the top of the mix" : both ? "the low end and the top" : "the mix"} is too quiet for it to add much yet.`
    : highs ? `${what} is adding sheen about ${fmtNum(-harm, 0)} dB below the music${harm > -35 ? ": strong, check the hats don't turn fizzy" : harm > -52 ? ": a clear sheen on the top" : ": very subtle"}.`
    : `${what} is adding harmonics about ${fmtNum(-harm, 0)} dB below the music${harm > -25 ? (bass || both ? ": clearly audible, the 808 is getting louder on small speakers" : ": clearly audible, the master is getting denser") : harm > -40 ? ": felt more than heard, the usual sweet spot" : ": very subtle"}.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Mode", mode, mode === "Tape" ? "Smooth and warm: rounds off the loudest moments and adds odd harmonics. The safest choice on a full mix."
    : mode === "Tube" ? (highs ? "Rich: leans on the 2nd harmonic, which gives the hats a sweeter, airier sheen."
        : `Rich: leans on the 2nd harmonic at light Drive (at +18 the 3rd takes over), which ${bass || both ? "makes the 808 sound fuller and helps it" : "makes bass and vocals sound fuller and helps an 808"} show up on phone speakers.`)
    : "Firm and bright: a harder knee with more upper harmonics. Adds edge and bite; use less Drive."));
  const fi = satFocusIndex();
  body.appendChild(item("Focus", SAT_FOCUS[fi], [
    "The whole mix gets harmonics: more density and glue, but a lot of Drive also colours vocals and hats. For an 808 that disappears on phones, try Bass only; for duller hats, Highs only.",
    "Only the low end (below about 150 Hz: 808 and kick) gets harmonics. Phone and laptop speakers can't play an 808's low notes, but they do play its harmonics, so you hear the 808 there. Vocals, hats and snares stay clean, and the deep sub stays as it was.",
    "Only the top (above about 4 kHz: hats, cymbals, the air on the vocal) gets harmonics: sheen and sparkle. The vocal's body, the snare and the 808 stay clean. The top is much quieter than the whole mix, so the Harmonics meter reads lower here: around −40 to −50 dB is already a clear sheen.",
    "Two jobs at once: the 808 gets harmonics for phones and the hats get sheen, each on its own, while the middle, where the vocal lives, stays clean. The usual trap choice when you want both."][fi]));
  const sch = satChannelsAt();
  body.appendChild(item("Channels", ["Stereo", "Mid", "Side"][sch], sch === 0 ? "Both channels get the harmonics alike."
    : sch === 1 ? "Only the centre gets harmonics (vocal, kick, 808, snare): the wide parts stay clean, so the stereo image stays as clean and wide as it was."
    : "Only the stereo edges get harmonics (wide synths, reverbs, wide hats): adds width and air around a clean centre. Check mono (MONO button) stays fine."));
  body.appendChild(item("Drive", `+${drive.toFixed(1)} dB`, drive < 0.05 ? "Off." : drive <= 6 ? (bass ? "Light: only the loudest 808 notes get harmonics." : highs ? "Light: a touch of sheen on the loudest hats." : "Light: only the loudest moments are shaped.") : drive <= 12 ? `Medium: ${bass ? "the 808 gets noticeably richer" : highs ? "the hats get a clear sheen" : both ? "the 808 gets richer and the hats get sheen" : "the master gets noticeably denser"}.` : "Heavy: obvious color. Pull Mix back if it gets harsh."));
  body.appendChild(item("Mix", `${Math.round(mix)} %`, mix >= 99.5 ? "Fully saturated signal." : `Blends ${Math.round(mix)} % of the harmonics in and keeps the rest clean, so transients stay sharp while the harmonics fill in underneath.`));
  body.appendChild(item("Level", "", bass ? "The 808's level stays put: only its harmonics are added, so on big speakers and headphones it sounds about the same, and on small speakers it comes through."
    : highs || both ? "The top keeps about its level (the loudest hats are rounded off a touch, as Full does to the whole mix); what you hear is the added sheen. The vocal's body is not touched."
    : "Quiet parts pass at the same level, so turning Drive up changes color, not volume. It reacts to how hot your mix is: louder mixes saturate more at the same Drive."));
  if (on && !bypassed && playing && harm > -18)
    body.appendChild(tip("HEADS UP", "The harmonics are strong. That's a sound, not a mistake, but on most masters it reads as distortion.", false,
      { need: "Only if it sounds harsh or crunchy to you.", steps: ["Turn on MATCH and flip A/B.", "If B sounds distorted: lower Drive by 3 dB, or Mix to about 30 %.", "Aim for the Harmonics meter around −25 to −35 dB."] }));
  else if (fi === 2)
    body.appendChild(tip("TRY THIS", "Highs only, Tape, Drive +9 to +12: dull or lifeless hats get sheen without the vocal getting harsher. Compare with MATCH: B should sound more open on top, not brighter on the vocal. If the hats turn fizzy, lower Drive or Mix.", true));
  else if (fi === 3)
    body.appendChild(tip("TRY THIS", "Bass + Highs, Tube, Drive +9: the 808 comes through on phones and the hats get a little sheen, with the vocal left alone. Check both on your phone and on headphones with MATCH.", true));
  else if (bass)
    body.appendChild(tip("TRY THIS", "Bass only, Tube, Drive +9: play the song on your phone or laptop speakers and compare the 808 with MATCH (A/B). On B it should come through; on headphones it should sound about the same.", true));
  else
    body.appendChild(tip("TRY THIS", "Tape, Drive +6 dB, Mix 50 %: a little density you feel more than hear (the Harmonics meter around −35 to −30 dB). If your 808 disappears on phones, try Focus Bass only with Tube instead. Always compare with MATCH on.", true));
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
    histGroup(true, "Apply Stem EQ suggestion");   // one undo step for all of it
    try {
      P.stemOn.setValue(true);
      s.forEach((v, k) => { const st = P.stemGain[k]; st.sliderDragStarted(); st.setNormalisedValue(scaledToNorm(st, v)); st.sliderDragEnded(); });
    } finally { histGroup(false); }
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
  const keep = !!P.stemKeep.getValue(), keepDb = Math.abs(M.stemKeep || 0) < 0.05 ? 0 : M.stemKeep || 0;
  const parts = STEMS.map((n, k) => `${n.toLowerCase()} ${share[k]} %`);
  const heard = `the loudness coming in as ${parts.slice(0, -1).join(", ")} and ${parts[parts.length - 1]}`;   // "... as vocals 38 %, ... and other 21 %"
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
    (v) => v > 5.95 ? (keep ? "More bass and 808. In a trap mix the 808 is a big part of the loudness, so to keep the same loudness the rest of the mix comes down: Keep Loudness does that here, cleanly, before the Maximizer."
                            : "More bass and 808, but at this much the mix gets much louder and the Maximizer has to turn everything down to fit it: at +12 the 808 ends up only about 4 dB louder while everything from 300 Hz up drops about 6 dB, with a lot more limiting.")
      : v > 0 ? (keep ? "More bass and 808. The 808 carries a lot of a trap mix's loudness, so Keep Loudness turns the whole mix down to hold the level: the 808 steps forward and the rest steps back (see Keeping level under the switch)."
                      : "More bass and 808. Check on small speakers and keep an eye on the Maximizer: bass is what limiters grab first, so it turns the rest down a little to make room.")
      : "Less bass and 808. Useful when the low end swamps the mix.",
    (v) => v > 0 ? "More of everything else: synths, keys, guitars, pads and effects." : "Less of everything else, so vocals and drums stand out.",
  ];
  STEMS.forEach((n, k) => {
    const v = stemGainAt(k);
    body.appendChild(item(n, fmtSigned(v), Math.abs(v) < 0.05 ? "Untouched." : why[k](v)));
  });
  body.appendChild(item("Keep Loudness", keep ? "ON" : "OFF", !keep ? "Off: every move also changes how loud the mix is. Turning something up makes it louder, which always sounds better in a quick A/B."
    : !moves.length || !on || bypassed ? "On: when you move a part, Stem EQ turns the whole mix up or down so the loudness stays the same. Your moves change the balance, not the level."
    : keepDb < 0 ? `On: your moves would make the mix louder, so Stem EQ turns all of it down ${fmtNum(-keepDb, 1)} dB. The balance changes; the loudness stays where it was.`
    : keepDb > 0 ? `On: your moves would make the mix quieter, so Stem EQ turns all of it up ${fmtNum(keepDb, 1)} dB. The balance changes; the loudness stays where it was.`
    : "On: your moves hardly change the loudness, so there is nothing to make up right now."));
  body.appendChild(item("Model Hears", "", "How the model divides the mix coming in (before your moves), averaged over about a second: each part's share of the loudness, the way a loudness meter weighs it. So the share tells you what a move does: with Keep Loudness off, turning up a part with a 70 % share by 6 dB makes the mix about 5 dB louder, a part with a 10 % share about 1 dB. In trap the 808 is often most of the loudness, even when it doesn't sound like most of the mix."));
  const big = moves.filter((k) => Math.abs(stemGainAt(k)) > 6.05);
  if (on && !bypassed && stemGainAt(2) > 5.95 && keep)
    body.appendChild(tip("HEADS UP", `Bass is up 6 dB or more. Keep Loudness holds the level by turning the whole mix down${keepDb < -0.05 ? ` (${fmtNum(-keepDb, 1)} dB right now)` : ""}, so the 808 gets louder and everything above the bass sits lower. The Maximizer doesn't have to work harder. At the same loudness, more 808 always means less of the rest, with or without this switch.`, false,
      { need: "Only if the vocal and drums sound smaller than you want.", steps: ["Back Bass off a little: +3 to +4 dB is often enough.", "Turn on MATCH and flip A/B to compare at equal loudness."] }));
  else if (on && !bypassed && stemGainAt(2) > 5.95)
    body.appendChild(tip("HEADS UP", "Bass is up 6 dB or more and Keep Loudness is off. The extra bass makes the mix much louder, so the Maximizer turns the whole master down to fit it and limits a lot harder: everything above the bass gets quieter (at +12: 808 about +4 dB, 300 Hz to 16 kHz about \u22126 dB).", false,
      { need: "Yes, if the vocal and drums sound smaller or the low end sounds squashed.", steps: ["Turn Keep Loudness on, so the move no longer pushes the Maximizer harder.", "Or run Auto-Master again so the chain is set for the new low end.", "Turn on MATCH and flip A/B to compare at equal loudness."] }));
  else if (on && !bypassed && !keep && moves.some((k) => Math.abs(stemGainAt(k)) > 3.95))
    body.appendChild(tip("HEADS UP", `Keep Loudness is off, so this move also makes the mix ${moves.every((k) => stemGainAt(k) < 0) ? "quieter" : "louder"}. Louder always sounds better in a quick A/B, so it's hard to tell whether the new balance is really better.`, false,
      { need: "Only if you're judging the move by ear.", steps: ["Turn Keep Loudness on (in Stem EQ).", "Or turn on MATCH and flip A/B to compare at equal loudness."] }));
  else if (on && !bypassed && big.length)
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
  body.appendChild(item("Squash", sq ? sqText : "–", "How far the peaks stand above the average level, and whether they press flat against one ceiling the way a limiter leaves them. 12.5 dB or more is an open, unmastered mix; 8.5 dB or less is brickwalled (limited hard). A loud 808 can sit close to the peaks of a mix nobody limited; then the peaks don't press against a ceiling and it still reads open. After Auto-Master it holds the reading Auto-Master took, so playback and export get the same lift from the first bar; before that it measures the last 3 seconds. The more squashed, the harder the Unlimiter works; on an open mix it does nothing."));
  body.appendChild(item("Amount", `${Math.round(amount)} %`, amount < 0.5 ? "Off." : `Sets how far each hit is lifted: ${sq ? `up to ${fmtNum(10 * amount / 100 * scale)} dB on your audio` : "up to 10 dB"} (10 dB at 100 % on brickwalled audio, less the less squashed it measures). On a brickwalled beat, 50 % wins back about a third of the punch the limiter took and 100 % about half, before the Maximizer. How much survives the Maximizer depends on your target: the louder the target, the less.`));
  body.appendChild(item("Length", fmtMs(len), `Each lift is held for about ${fmtMs(len)}, then lets go. ` + (len < 12 ? "Short: just the click and snap of each hit comes back. Cleanest." : len > 30 ? "Long: the body of each hit comes back too. Bigger, but the music around the hits pumps a little." : "A middle setting: the attack and a little body of each hit.")));
  body.appendChild(item("Focus", focus, focus === "Full" ? "Listens for every hit and lifts the whole sound, like the limiter it undoes." : focus === "Kick" ? "Listens for the kick and 808 (below 150 Hz) and lifts only that band: hats, snare and vocal above it are left alone." : "Listens for snares, claps and hats (above 1 kHz) and lifts only that band: the kick and 808 below it are left alone."));
  body.appendChild(item("Good to know", "", "It can't rebuild a waveform a clipper cut flat; it brings back the contrast between the hits and the rest. The Maximizer then makes the song loud again and presses part of that contrast back down: the closer your target is to how loud the audio already arrives, the less is left. Judge it at the final loudness, with MATCH on."));
  if (on && !bypassed && playing && amount > 0.5 && sq && scale === 0)
    body.appendChild(tip("GOOD TO KNOW", "Your audio isn't squashed, so the Unlimiter has nothing to undo and stays out of the way.", true,
      { need: "No. It is resting on purpose.", steps: ["You can leave it as it is, or set Amount to 0."] }));
  else if (on && !bypassed && playing && lift > 6 && focus === "Full")
    body.appendChild(tip("HEADS UP", `It's lifting the whole sound by up to ${fmtNum(lift)} dB at each hit. That much can make the vocal or pads jump with the drums.`, false,
      { need: "Only if you hear the vocal or pads jump on each drum hit.", steps: ["Turn on MATCH and flip A/B.", "If B pumps: try Focus Kick (or Snap), which lifts only the drums' band.", "Or lower Amount by 20 %, or Length to about 10 ms."] }));
  else
    body.appendChild(tip("TRY THIS", "Got a beat that's already loud and flat? Amount 75 %, Length 15 ms, Focus Full, then compare at the final loudness with MATCH on: the drums should hit harder while the level between them stays the same. If your target is about as loud as the beat already is (Loud \u22129 or Club \u22127 on a beat at \u22128 LUFS), the Maximizer presses the hits back down: it pays off on quieter targets like Stream \u221214.", true));
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
    : !playing ? (M.tsTarget ? "Press play: Tone Steady keeps each part close to your song's own overall tone."
                             : "Press play: Tone Steady learns your song's own balance over the first seconds, then keeps each section close to it.")
    : Math.abs(move) < 0.3 ? "This part of the song matches its own overall balance, so Tone Steady is resting."
    : `Right now it's ${move < 0 ? "pulling down" : "lifting"} the area around ${where} by ${fmtNum(Math.abs(move))} dB: this part drifted ${move < 0 ? "fuller" : "thinner"} there than the rest of the song.`));
  body.appendChild(sep());
  body.appendChild(head("WHAT EACH CONTROL IS DOING"));
  body.appendChild(item("Amount", `${Math.round(amount)} %`, amount < 0.5 ? "Off." : `Corrects about ${Math.round(amount)} % of each drift, beyond a 1 dB dead zone (3 dB in the highs, where hats and cymbals come and go), so small natural changes are left alone.`));
  body.appendChild(item("Range", `${MINUS}${range.toFixed(1)} / +${(range / 2).toFixed(1)} dB`, `Cuts go at most ${range.toFixed(1)} dB${M.tsTarget ? ` (above the bass ${(range / 2).toFixed(1)} dB, while it holds your song's tone)` : ""}, boosts at most ${(range / 2).toFixed(1)} dB: the most it can move any part of the spectrum. A section meant to sound different (a drop with more bass) can be pulled by up to this much, so lower Range if a big moment sounds smaller.`));
  body.appendChild(item("Speed", speed, speed === "Slow" ? "Reacts over about a second: the gentlest, best for most masters."
    : speed === "Medium" ? "Reacts within about half a second: catches a phrase that gets muddier or a hook that gets harsher." : "Reacts within a few notes (0.15 s): strongest, can make the mix feel controlled."));
  if (M.tsTarget)
    body.appendChild(item("Target", `your song · ${fmtTime(M.tsTarget)}`, `Tone Steady now holds your song to its own overall tone, learned when it listened to ${fmtTime(M.tsTarget)} of music. So what you hear is what you export, from any start point. Parts that come and go in the low end are left alone: an 808 that enters after the intro isn't cut, and the intro without it isn't lifted. A part that isn't playing isn't faked back. A part that comes in above the bass (a lead in the chorus) is treated like a drift: it's pulled down, by at most half the Range, for as long as it plays. It follows your Tonal EQ moves. It's saved with your project, and also in presets and copies of Masterology, so on a new song press LEARN again. CLEAR goes back to following the music.`));
  else
    body.appendChild(item("Target", "following the music", "Your song's own balance over the last ~10 seconds, so the Tonal EQ sets the tone and Tone Steady only keeps it. Quiet breakdowns are left alone. When a bass or 808 comes in (the low end jumps more than 4 dB against the rest), it re-learns the balance within about a second instead of cutting the new low end. Because it follows the music, what you hear can depend a little on where playback starts. LEARN holds it to your whole song's tone instead."));
  if (on && !bypassed && amount >= 0.5 && M.tsTarget && M.tsFar)
    body.appendChild(tip("HEADS UP", `Your song sits far from the tone Tone Steady is holding it to (about ${fmtNum(Math.max(0, M.tsDist || 0))} dB off, for 20 seconds or more of music). That happens when the tone was learned on another song: presets, copies of Masterology and older projects carry it along. Held to another song's tone, parts of your mix get cut or lifted by up to the Range.`, false,
      { need: "Yes, if this tone came from another song (a preset, a copied Masterology, another project). If you learned it on this song, a section that sounds very different (a beat switch) can show this too: then you can leave it.",
        steps: ["Press LEARN in this panel.", "Play your whole song from the start. It finishes by itself when the song ends.", "Or press CLEAR to follow the music instead."] }));
  else if (on && !bypassed && amount >= 0.5 && M.tsTarget && M.tsTarget < 30)
    body.appendChild(tip("HEADS UP", `Tone Steady learned your song's tone from only ${fmtTime(M.tsTarget)} of music, so it may not describe the whole song.`, false,
      { need: "Only if your song's sections sound quite different from each other.", steps: ["Press LEARN in this panel.", "Play the whole song from the start. It finishes by itself when the song ends."] }));
  else if (on && !bypassed && amount > 70 && speed === "Fast")
    body.appendChild(tip("HEADS UP", "High Amount at Fast speed follows single notes. That can flatten moves you made on purpose, like a drop that's meant to hit with more bass.", false,
      { need: "Only if the big moments sound smaller with it on.", steps: ["Turn on MATCH and flip A/B through the drop or chorus.", "If the impact shrinks: lower Range to about 1.5 dB.", "Or lower Amount to about 40 %, or Speed to Slow."] }));
  else if (!M.tsTarget)
    body.appendChild(tip("TRY THIS", "Press LEARN and play your whole song from the start: Tone Steady learns its overall tone, so what you hear from any start point is what you export. Then Amount 40 %, Range 3 dB, Speed Slow, and compare with MATCH on.", true));
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

// AutoMasterController: after kAutoFinishSeconds of music, kSongEndSilenceSeconds without music end the listen.
const AM_AUTO_FINISH = 30, AM_END_SILENCE = 4;
// How long choosing the settings takes, said honestly: it runs the whole chain many times on the 12 s it
// kept, so it scales with the sample rate (measured on a desktop CPU: about 6 s at 44.1 / 48 kHz, 12 s at
// 96 kHz, 24 s at 192 kHz; slower computers take longer). Shows the time so far once it has started.
let amThinkSince = 0;
function amThinkTime() {
  const sr = M.sr > 0 ? M.sr : 48000;
  const est = sr <= 50000 ? "usually 5–15 seconds" : sr <= 100000 ? "usually 10–30 seconds" : "usually 20–60 seconds";
  const secs = amThinkSince ? Math.floor((performance.now() - amThinkSince) / 1000) : 0;
  return secs >= 1 ? `${est}; ${secs} s so far` : est;
}
// While Auto-Master listens (or Tone Steady learns): what it has heard, the loudest part so far, FINISH NOW.
function renderListenLearn(body) {
  const tone = M.amMode === 1, heard = M.amHeard || 0;
  body.appendChild(head(tone ? "LEARNING YOUR SONG'S TONE" : "LISTENING TO YOUR SONG"));
  if (M.amState === 2) {
    body.appendChild(para("l-p", `Heard ${fmtTime(heard)} of your song. Now choosing every setting for its loudest part (${amThinkTime()})…`));
    const stop = document.createElement("button");
    stop.type = "button";
    stop.id = "am-cancel";
    stop.className = "pill pill-blue stem-apply";
    stop.textContent = "CANCEL";
    stop.addEventListener("click", () => {
      amCancel(); amRequested = false;
      M.amState = 0; learnView = "module";
      updateAmButton(); renderLearn();
      toast("Auto-Master stopped. Nothing was changed.");
    });
    const row = document.createElement("div");
    row.className = "l-actions";
    row.append(stop);
    body.appendChild(row);
    return;
  }
  body.appendChild(para("l-p", !heard && !M.amHearing ? "Press play in your DAW, ideally from the very start of the song."
    : M.amHearing ? "Keep the song playing."
    : heard >= AM_AUTO_FINISH ? `No music for a moment. After ${AM_AUTO_FINISH} seconds of music, ${AM_END_SILENCE} seconds without music count as the end of the song, and it finishes by itself. To keep listening, press play again right away.`
    : "Paused. Press play to go on, or finish with what it has heard."));
  body.appendChild(para("l-p", (tone ? "Masterology listens to the whole song and learns its overall tone for Tone Steady. Nothing else changes."
                                     : "Auto-Master listens to the whole song, finds its loudest part by itself and sets every module for it. It also learns your song's tone for Tone Steady.")
    + ` It finishes by itself ${AM_END_SILENCE} seconds after the song ends (once it has heard ${AM_AUTO_FINISH} seconds of music). Or press FINISH NOW any time after 12 seconds.`));
  body.appendChild(sep());
  body.appendChild(item("Heard", fmtTime(heard), "of music so far. Silence and stops are skipped."));
  if (!tone)
    body.appendChild(item("Loudest part so far", M.amLoudAt < 0 ? "–" : M.amLoudJump ? `from ${fmtTime(M.amLoudAt)}`
                                                  : `${fmtTime(M.amLoudAt)}–${fmtTime(M.amLoudEnd > M.amLoudAt ? M.amLoudEnd : M.amLoudAt + 12)}`,
      M.amLoudAt < 0 ? "Found once 12 seconds of music are in."
      : `${fmtNum(M.amLoudLufs)} LUFS. Auto-Master sets the levels from this part (usually the hook or drop).`
        + (M.amLoudJump ? " Playback jumped inside it (a cycle loop, or the playhead moved), so it joins two places on your timeline." : "")));
  const row = document.createElement("div");
  row.className = "l-actions";
  const finish = document.createElement("button");
  finish.type = "button";
  finish.id = "am-finish";
  finish.className = "pill pill-red stem-apply" + (M.amCanFinish ? "" : " is-disabled");
  finish.disabled = !M.amCanFinish;
  finish.textContent = M.amCanFinish ? "FINISH NOW" : `FINISH NOW (in ${fmtTime(Math.max(0, 12 - heard) + 0.999)})`;
  finish.title = tone ? "Learn the tone from what it has heard" : "Use what it has heard: set every module from the loudest part so far";
  finish.addEventListener("click", finishListening);
  const cancel = document.createElement("button");
  cancel.type = "button";
  cancel.id = "am-cancel";
  cancel.className = "pill pill-blue stem-apply";
  cancel.textContent = "CANCEL";
  cancel.addEventListener("click", () => {
    amCancel(); amRequested = false; toneRequested = false;
    M.amState = 0; learnView = "module";
    updateAmButton(); renderLearn();
    toast(tone ? "Stopped learning. Tone Steady keeps what it had." : "Auto-Master stopped listening. Nothing was changed.");
  });
  row.append(finish, cancel);
  body.appendChild(row);
  body.appendChild(tip("TRY THIS", tone ? "Play the whole song in one go, from the start to the end. The tone is learned from every part, so preview and export sound the same from any start point."
                                        : "Play the whole song in one go, from the start to the end. Auto-Master picks the loudest part itself, and the report tells you where it was.", true));
}
let lastListenDraw = "";
async function finishListening() {
  if (!M.amCanFinish) { toast("Let it hear at least 12 seconds of music first."); return; }
  amFinishClicked = true;
  let ok = true;
  try { ok = await amFinish(); } catch (e) { ok = false; }
  if (ok === false) { amFinishClicked = false; toast("It needs at least 12 seconds of music first."); return; }
  toast(M.amMode === 1 ? "Learning the tone from what it heard." : `Using what it heard (${fmtTime(M.amHeard)}). Choosing every setting…`);
}

// Warnings go right under "What's happening" so they're seen without scrolling; friendly tips stay at the end.
// Auto-Master plans from the loudest part it heard. When the incoming mix stays clearly louder than that for a few
// seconds (playback started in the verse, the chorus came later), the Maximizer Learn says so.
let louderPartDb = 0, louderSince = 0, louderReport = null;
function watchLouderPart() {
  const heard = amReport && amReport.ok ? amReport.capturedLufs : undefined;
  if (amReport !== louderReport) { louderReport = amReport; louderPartDb = 0; louderSince = 0; }
  if (heard === undefined || heard < -70 || M.inShort < -70 || M.amState === 1) { louderSince = 0; return; }
  const over = M.inShort - heard;
  if (over < 2) { louderSince = 0; return; }
  const now = performance.now();
  if (!louderSince) louderSince = now;
  if (now - louderSince > 3000 && over > louderPartDb + 0.5) {
    if (louderPartDb === 0) toast("This part is louder than what Auto-Master heard. See Learn (Maximizer).");
    louderPartDb = over;
  }
}

// The whole-song listen as the plug-in reports it: says when the song's end finished it, announces a learned
// tone, keeps the LISTENING view in Learn current.
function watchListening(wasAm, wasTarget, wasFail, wasFar) {
  if (wasAm === 1 && M.amState === 2 && !amFinishClicked && M.amByItself)
    toast(`The song ended: Auto-Master heard ${fmtTime(M.amHeard)} and is choosing every setting…`);
  if (wasAm !== 0 && M.amState === 0) amFinishClicked = false;
  if (M.tsTarget !== wasTarget || M.tsLearnFail !== wasFail || !!M.tsFar !== !!wasFar) {
    if (toneRequested && M.amState === 0) {
      toneRequested = false;
      if (M.tsLearnFail !== wasFail)
        toast("Tone Steady heard too little music at your song's level to learn its tone. Play more of the song, then press LEARN again.");
      else if (M.tsTarget) {
        userEdited();
        toast(`Tone Steady learned your song's tone from ${fmtTime(M.tsTarget)} of music.`);
        if (learnView === "listen") learnView = "module";
      }
    }
    tsTarget.refresh();
    if (learnOn && learnView === "module" && selected === "ts") refreshLearn();
  }
  if (learnOn && learnView === "listen") {
    const key = [M.amState, Math.floor(M.amHeard || 0), M.amCanFinish, M.amHearing, M.amLoudAt, M.amLoudEnd, M.amLoudJump,
                 M.amState === 2 && amThinkSince ? Math.floor((performance.now() - amThinkSince) / 1000) : 0].join("|");
    if (key !== lastListenDraw) { lastListenDraw = key; refreshLearn(); }
  }
}

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
        ? `The clip level sits right at the ${fmtNum(ceiling, 1)} dBTP ceiling (Softness starts rounding peaks a few dB below it): the clipper takes the drum spikes and the Maximizer just polishes what's left.`
        : `Clipping starts ${fmtNum(-level, 1)} dB below the ceiling, then the level comes back up by ${fmtNum(-level, 1)} dB so peaks still reach ${fmtNum(ceiling, 1)} dBTP. Lower = denser and louder, but more distortion.`));
  body.appendChild(item("Softness", `${soft} %`,
    soft < 25 ? "Hard knee: the most transparent on short drum hits, but it can crunch on sustained bass."
              : soft > 75 ? "Very soft knee: starts bending up to 6 dB before the clip point. Warm and smooth, but it softens the drum attack."
                          : "Medium knee: rounds the peaks gently, so you hear punch rather than distortion."));
  body.appendChild(item("Oversampling", os,
    os === "1x" ? "No oversampling: clipping creates inharmonic 'aliasing' tones that sound harsh and cheap. Use 4x or 8x for masters."
                : `Runs the clipper at ${os} the sample rate so far fewer of the new harmonics fold back as harsh tones. ${os === "8x" ? "Cleanest, uses the most CPU." : "A good balance of clean and CPU."}`));

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

// ---------------------------------------------------------------------------------------------
// Listening aids: Listen / Solo (hear only what a module changes), MONO and PHONE checks. Monitoring only:
// a red banner shows while any is on, so it is never forgotten.
const listenModeFor = (id) => (id === "dyn" ? 9 + dynBand : LISTEN_OF_MODULE[id] || 0);
function listenWhat(m) {
  if (m === 1) return "what Saturation adds";
  if (m === 2) return "what the Soft Clipper shaves off";
  if (m === 3) return "what Resonance Tamer cuts";
  if (m === 4) return "what the Maximizer takes off";
  if (isSoloMode(m)) return `only the ${DYN_BANDS[m - 5].short} band of Dynamics`;
  if (m >= 9 && m <= 12) return `what Dynamics takes off the ${DYN_BANDS[m - 9].short} band`;
  return "";
}
function setMonitor(listen, mono = MON.mono, phone = MON.phone) {
  MON.listen = listen; MON.mono = mono; MON.phone = phone; MON.sentAt = performance.now();
  setMonitorFn(listen, mono, phone);
  refreshMonitor();
}
function refreshListenButton() {
  const b = $("btn-listen"), mode = listenModeFor(selected);
  b.hidden = !mode;
  if (!mode) return;
  const on = MON.listen === mode;
  b.className = "pill listen " + (on ? "pill-red on" : "pill-blue");
  b.setAttribute("aria-pressed", String(on));
  $("btn-listen-text").textContent = "LISTEN";
  b.title = `Listen: hear only ${listenWhat(mode)}. A listening aid: a normal export never contains it (switch it off before a Realtime Export)`;
}
$("btn-listen").addEventListener("click", () => {
  const mode = listenModeFor(selected), on = MON.listen !== mode;
  setMonitor(on ? mode : 0);
  toast(on ? `LISTENING: ${listenWhat(mode)}, not the master. Press LISTEN again (or BACK TO MASTER) to return.` : "Back to the master.");
});
// The banner's second part: how loud what you hear is, compared with how it sits in the master.
function monitorLevelText() {
  const m = MON.listen;
  if (!m || M.listenNow !== m) return m ? "starting…" : "";
  if (P.bypass.getValue() && !isSoloMode(m)) return "on A: nothing is processed";
  const g = M.listenGain || 0;
  if (!isSoloMode(m) && (M.listenUnder ?? -100) <= -99) return "silent: nothing changed";
  if (g > 0.4) return `+${g.toFixed(0)} dB so you can hear it`;
  if (g < -0.4) return `${fmtNum(g, 0)} dB, kept under the ceiling`;
  return "at its real level";
}
function refreshBanner() {
  const check = MON.phone ? "PHONE" : MON.mono ? "MONO" : "";
  const on = !!(MON.listen || check);
  $("monitor-banner").hidden = !on;
  if (!on) return;
  $("monitor-text").textContent = MON.listen
    ? `${isSoloMode(MON.listen) ? "SOLO" : "LISTENING"}: ${listenWhat(MON.listen)}${check ? `, on ${check}` : ""} — not the master`
    : MON.phone ? "PHONE CHECK: your master on a phone speaker — not the master's real sound"
    : "MONO CHECK: left + right together — not the master's real sound";
  $("monitor-level").textContent = monitorLevelText();
}
function refreshMonitor() {
  for (const [id, on] of [["btn-mono", MON.mono], ["btn-phone", MON.phone]]) {
    const b = $(id);
    b.className = "pill check-pill " + (on ? "pill-red" : "pill-blue");
    b.setAttribute("aria-pressed", String(on));
  }
  refreshListenButton();
  dynPicker.refresh();
  refreshBanner();
  scheduleLearn();
}
$("btn-mono").addEventListener("click", () => {
  setMonitor(MON.listen, !MON.mono, MON.phone);
  toast(MON.mono ? "MONO check on: both speakers play left and right mixed together. Listen for parts that disappear." : "MONO check off.");
});
$("btn-phone").addEventListener("click", () => {
  setMonitor(MON.listen, MON.mono, !MON.phone);
  toast(MON.phone ? "PHONE check on: your master on a phone speaker. Does the 808 still come through?" : "PHONE check off.");
});
$("monitor-stop").addEventListener("click", () => { setMonitor(0, false, false); toast("Back to the master."); });

// Learn: what you're hearing and what to listen for (at the top while a listening aid is on), or a pointer
// to the Listen button on the modules that have one.
function listenLevelSentence(m) {
  if (M.listenNow !== m || (M.listenUnder ?? -100) <= -99) return "";
  const under = -(M.listenUnder || 0), g = M.listenGain || 0;
  if (isSoloMode(m)) return g < -0.4 ? ` It's turned down ${fmtNum(-g, 0)} dB here so it can't clip.` : "";
  return ` In your master it sits about ${under.toFixed(0)} dB under the music` +
    (g > 0.4 ? `; it plays ${g.toFixed(0)} dB louder here so you can hear it.` : g < -0.4 ? `; it's turned down ${fmtNum(-g, 0)} dB here so it can't clip.` : ", and plays at that level here.");
}
function listenTip(m) {
  const lvl = listenLevelSentence(m);
  if (m === 1)
    return tip("LISTEN: WHAT SATURATION ADDS", "Only the harmonics Saturation adds (its output minus what went in, lined up to the sample), not the master." + lvl, true,
      { need: "Only if you hear clear crackle or fizz on the loud hits. A soft, buzzy shadow of the 808 and the vocal is normal: that's the warmth, and the harmonics that let the 808 come through on phones.",
        steps: ["Lower Drive by 3 dB (or Mix to about 30 %) until the crackle goes.", "Press LISTEN again (or BACK TO MASTER) and compare with MATCH on."] });
  if (m === 2)
    return tip("LISTEN: WHAT THE CLIPPER SHAVES OFF", "Only what the Soft Clipper takes off the peaks (its input at the clipper's level, minus its output), not the master." + lvl, true,
      { need: "Only if you hear more than short ticks on the kick and snare. Ticks are the clipping doing its job; a buzz on the 808, the snare's body or anything like a note means it clips too hard.",
        steps: ["Raise Clip Level by 1 dB, or raise Softness.", "Listen again: you want only short ticks on the hits."] });
  if (m === 3)
    return tip("LISTEN: WHAT THE TAMER CUTS", "Only what Resonance Tamer cuts: the ringing tones it pulls down, not the master." + lvl, true,
      { need: "Only if you hear whole notes of the vocal, the snare or the melody. Thin whistles, rings or a boxy honk coming and going are what it should catch.",
        steps: ["Lower Amount by 20 %, or Sensitivity to about 40 %.", "Or set Focus to Harshness or Boxiness so it only looks where the rings are."] });
  if (m === 4)
    return tip("LISTEN: WHAT THE MAXIMIZER TAKES OFF", "Only what the limiter takes off the peaks, not the master." + lvl, true,
      { need: "Only if you hear the kick and snare clearly, or the 808 pumping. Faint ticks on the hits are normal.",
        steps: ["Lower Drive by 1–2 dB.", "Or turn on the Soft Clipper (chain, left): it takes the sharpest peaks so the limiter does less."] });
  if (isSoloMode(m)) {
    const b = m - 5;
    return tip(`SOLO: THE ${DYN_BANDS[b].name.toUpperCase()} BAND`, `Only the ${DYN_BANDS[b].name} band (${dynRangeText(b)}: ${DYN_BANDS[b].holds}) after its compression, at its real level in the master.` + lvl, true,
      { need: "No. It's a listening tool: hear what lives in each band.",
        steps: ["Move this band's Split until the kick and 808 sit in LOW and the vocal's body in LO-MID.", "Press S again (or BACK TO MASTER) to hear the master."] });
  }
  const b = m - 9;
  return tip(`LISTEN: WHAT DYNAMICS TAKES OFF ${DYN_BANDS[b].short}`, `Only what Dynamics takes off the ${DYN_BANDS[b].name} band (${DYN_BANDS[b].holds}), not the master.` + lvl, true,
    { need: "Only if you hear the band most of the time, or the 808 pumping. Short bumps on the loudest hits are normal.",
      steps: ["Raise this band's Threshold, or lower its Ratio to 2:1.", "Aim for 1–3 dB on the band's button."] });
}
// The suite link: what Mixology (mixing this song in the same project) hands over. At the top when there's
// something to do (another genre, a mono mix, Mixology's limiter on), else at the bottom, for information.
function mixologyKey() {
  const h = M.fromMixology;
  return h ? [h.style, h.genre, h.analysed, h.mono, h.limited, comboIndex(P.amGenre)].join("|") : "";
}
function addMixologyLearn(body) {
  const h = M.fromMixology;
  if (!h) return;
  const g = comboIndex(P.amGenre);
  const otherGenre = h.genre >= 0 && h.genre !== g;
  const frag = document.createDocumentFragment();
  const peak = h.peak > -99 ? `peaks at ${fmtNum(h.peak)} dBTP` : "no peaks yet";
  const room = h.peak <= -99 ? "" : h.peak > -1 ? ": almost no room left, so turn the mix down in Mixology (or Input Trim here) first" : h.peak <= -3 ? ": good room for mastering" : "";
  const level = h.lufs > -99 ? `${fmtNum(h.lufs)} LUFS with ` : "";
  let text = `Mixology mixed this song as ${h.style || "your style"}. Your mix comes in at ${level}${peak}${room}.`;
  if (h.analysed && h.tone) {
    const names = ["low end", "low mids", "presence", "air"];
    const off = h.tone.map((d, i) => [d, names[i]]).filter(([d]) => Math.abs(d) >= 2);
    text += off.length
      ? ` Against that style, Mixology heard ${off.map(([d, n]) => `${d > 0 ? "more" : "less"} ${n} (${fmtSigned(d)})`).join(" and ")}: Auto-Master's EQ evens out what's left.`
      : " Its tone sits close to that style.";
  }
  frag.appendChild(tip("FROM MIXOLOGY", text, true));
  if (otherGenre) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "pill pill-red stem-apply";
    b.textContent = `USE ${GENRES[h.genre].toUpperCase()} (MIXOLOGY'S STYLE)`;
    b.title = `Auto-Master compares your mix with ${GENRES[g]} now; Mixology mixed it as ${h.style}.`;
    b.addEventListener("click", () => {
      P.amGenre.setChoiceIndex(h.genre);
      refreshAmChoices();
      toast(`Genre set to ${GENRES[h.genre]}, the style Mixology mixed in. Run Auto-Master to master for it.`);
      renderLearn();
    });
    frag.appendChild(b);
  }
  if (h.mono)
    frag.appendChild(tip("YOUR MIX IS MONO", "Mixology heard the same thing on the left and the right at the {daw:master} channel: every pan and stereo effect is lost.", false,
      { need: "Yes, unless the song is meant to be mono.", steps: ["{daw:monoMeter}: a mono group or output folds everything to the middle.", "Mixology's Stereo page shows when it's fixed."] }));
  if (h.limited)
    frag.appendChild(tip("MIXOLOGY'S LIMITER IS ON", "Two limiters in a row squash the mix twice: Masterology's Maximizer is the one that should set the final loudness.", false,
      { need: "Usually yes.", steps: ["Open Mixology on the {daw:master} channel, go to the Dynamics page and switch the Limiter off (or keep it only as a safety that never touches the music)."] }));
  if (otherGenre || h.mono || h.limited) body.prepend(frag);
  else { body.appendChild(sep()); body.appendChild(frag); }
}

function addMonitorLearn(body) {
  if (MON.listen || MON.mono || MON.phone) {
    const frag = document.createDocumentFragment();
    frag.appendChild(head("WHAT YOU'RE HEARING"));
    if (MON.listen) frag.appendChild(listenTip(MON.listen));
    if (MON.phone)
      frag.appendChild(tip("PHONE CHECK", "A phone speaker, roughly: mono, thin below 300 Hz (200 Hz is about 13 dB down, and almost nothing under 150 Hz comes through, so no 808 sub), soft above 8 kHz, at about your master's loudness. A loud, bright master is turned down a few dB here so the check can't clip.", true,
        { need: "Only if the 808 disappears. You'll hear its upper harmonics, not its sub; that's normal, and it's what Saturation (Tube is the fullest) and an 808's own distortion are for.",
          steps: ["Turn Saturation's Drive up a little, or add 1–2 dB of Bass on the Tonal EQ.", "Best fix, in the mix: a little distortion or saturation on the 808 track itself.", "Press PHONE again to go back."] }));
    else if (MON.mono)
      frag.appendChild(tip("MONO CHECK", "Both speakers play left and right mixed together, like a club PA or a phone speaker.", true,
        { need: "Only if a part gets much quieter or disappears: it's out of phase, and it will vanish on mono speakers.",
          steps: ["Find which part (often wide synths, reverbs or doubled vocals).", "Bring the Stereo Imager's Mids and Highs back toward 100 %, or narrow that part in the mix.", "Press MONO again to go back to stereo."] }));
    frag.appendChild(sep());
    body.prepend(frag);
  }
  const mode = listenModeFor(selected);
  if (mode && MON.listen !== mode && !(selected === "dyn" && isSoloMode(MON.listen)))
    body.appendChild(tip("HEAR WHAT IT DOES", selected === "dyn"
      ? "S next to a band plays only that band. LISTEN (headphones, top right of this panel) plays only what the picked band's compression takes off. Listening aids only: a normal export never contains them (switch them off before a Realtime Export)."
      : `LISTEN (headphones, top right of this panel) plays only ${listenWhat(mode)}, so you can learn its sound. Quiet changes are turned up so you can hear them. A listening aid: a normal export never contains it (switch it off before a Realtime Export).`, true));
}

function refreshAB() {
  const bypassed = !!P.bypass.getValue();
  $("btn-a").classList.toggle("on", bypassed);
  $("btn-b").classList.toggle("on", !bypassed);
  $("btn-a").setAttribute("aria-pressed", String(bypassed));
  $("btn-b").setAttribute("aria-pressed", String(!bypassed));
  refreshHeadButton();
}
// The button in the module header: each module's own on/off switch; on the Maximizer it is the global
// bypass (A). Every live module except the Maximizer must be listed here.
const HEAD_POWER = { ul: [P.ulOn, "UNLIMITER"], eq: [P.eqOn, "EQ"], stem: [P.stemOn, "STEM EQ"], ts: [P.tsOn, "TONE STEADY"],
                     rt: [P.rtOn, "TAMER"], bass: [P.bassOn, "BASS"], dyn: [P.dynOn, "DYNAMICS"], img: [P.imgOn, "IMAGER"],
                     sat: [P.satOn, "SAT"], clip: [P.clipOn, "CLIPPER"], glue: [P.glueOn, "GLUE"] };
function refreshHeadButton() {
  const bp = $("btn-bypass");
  const power = HEAD_POWER[selected];
  if (power) {
    const on = !!power[0].getValue();
    bp.className = "pill bypass " + (on ? "pill-red" : "pill-blue");
    bp.textContent = power[1] + (on ? " ON" : " OFF");
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
  const state = HEAD_POWER[selected] ? HEAD_POWER[selected][0] : P.bypass;
  state.setValue(!state.getValue());
  onParamsChanged();
  userEdited();
});
P.bypass.valueChangedEvent.addListener(onParamsChanged);
for (const [state] of Object.values(HEAD_POWER)) state.valueChangedEvent.addListener(onParamsChanged);
for (const s of [...P.dynX, ...P.dynRatio, ...P.dynGain, ...P.dynImpact, ...P.dynSustain]) s.valueChangedEvent.addListener(() => { dynPicker.refresh(); updateLiveReadouts(); });

function togglePopover(id) {
  for (const p of ["pop-settings", "pop-help"]) $(p).hidden = p === id ? !$(p).hidden : true;
}
$("btn-settings").addEventListener("click", (e) => { e.stopPropagation(); togglePopover("pop-settings"); });
$("btn-help").addEventListener("click", (e) => { e.stopPropagation(); togglePopover("pop-help"); });
document.addEventListener("click", (e) => {
  if (!e.target.closest(".popover")) togglePopover(null);
});

// ---------------------------------------------------------------------------------------------
// User manual: docs/manual/polisher-manual.md, embedded in the plug-in and rendered here (offline).
// A small Markdown reader for what the manual uses: headings, paragraphs, lists, tables, code,
// bold and links (shown as text: the plug-in window stays on the manual).
const escHtml = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
function mdInline(t) {
  return escHtml(t)
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/https?:\/\/\S+/g, '<span class="muted">$&</span>');
}
function renderMarkdown(md) {
  const lines = md.replace(/\r\n?/g, "\n").split("\n");
  const out = [], toc = [];
  const isTableRow = (l) => /^\s*\|.*\|\s*$/.test(l);
  const cells = (l) => l.trim().replace(/^\||\|$/g, "").split("|").map((c) => c.trim());
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }
    if (line.startsWith("```")) {
      const code = [];
      for (i++; i < lines.length && !lines[i].startsWith("```"); i++) code.push(lines[i]);
      i++;
      out.push(`<pre>${escHtml(code.join("\n"))}</pre>`);
      continue;
    }
    const h = /^(#{1,3})\s+(.*)$/.exec(line);
    if (h) {
      const level = h[1].length, id = `man-${out.length}`;
      if (level === 2) toc.push({ id, text: h[2] });
      out.push(`<h${level} id="${id}">${mdInline(h[2])}</h${level}>`);
      i++;
      continue;
    }
    if (isTableRow(line) && i + 1 < lines.length && /^\s*\|[\s:|-]+\|\s*$/.test(lines[i + 1])) {
      const head = cells(line);
      const rows = [];
      for (i += 2; i < lines.length && isTableRow(lines[i]); i++) rows.push(cells(lines[i]));
      out.push(`<table><thead><tr>${head.map((c) => `<th>${mdInline(c)}</th>`).join("")}</tr></thead><tbody>` +
               rows.map((r) => `<tr>${r.map((c) => `<td>${mdInline(c)}</td>`).join("")}</tr>`).join("") + "</tbody></table>");
      continue;
    }
    const li = /^\s*([-*]|\d+\.)\s+/;
    if (li.test(line)) {
      const tag = /^\s*\d+\./.test(line) ? "ol" : "ul";
      const items = [];
      for (; i < lines.length && li.test(lines[i]); i++) items.push(`<li>${mdInline(lines[i].replace(li, ""))}</li>`);
      out.push(`<${tag}>${items.join("")}</${tag}>`);
      continue;
    }
    const para = [];
    for (; i < lines.length && lines[i].trim() && !/^(#{1,3}\s|```|\s*([-*]|\d+\.)\s)/.test(lines[i]) && !isTableRow(lines[i]); i++) para.push(lines[i].trim());
    out.push(`<p>${mdInline(para.join(" "))}</p>`);
  }
  return { html: out.join("\n"), toc };
}
let manualLoaded = false;
async function loadManual() {
  // Inside the plug-in the manual is served next to index.html; the browser preview reads the repo copy.
  for (const url of ["polisher-manual.md", "../../docs/manual/polisher-manual.md"]) {
    try {
      const res = await fetch(url);
      if (res.ok) return await res.text();
    } catch (_) {}
  }
  return null;
}
async function openManual() {
  togglePopover(null);
  $("manual").hidden = false;
  if (manualLoaded) return;
  const md = await loadManual();
  const body = $("manual-body"), toc = $("manual-toc");
  if (!md) { body.innerHTML = "<p>The manual couldn't be loaded. Quick Help (the ? button) has the essentials.</p>"; return; }
  const doc = renderMarkdown(md);
  body.innerHTML = doc.html;
  toc.innerHTML = "";
  for (const t of doc.toc) {
    const a = document.createElement("a");
    a.textContent = t.text;
    a.addEventListener("click", () => { const el = document.getElementById(t.id); if (el) body.scrollTo({ top: el.offsetTop - 8, behavior: "smooth" }); });
    toc.appendChild(a);
  }
  manualLoaded = true;
}
$("btn-manual").addEventListener("click", (e) => { e.stopPropagation(); openManual(); });
$("btn-manual-close").addEventListener("click", () => { $("manual").hidden = true; });
document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !$("manual").hidden) $("manual").hidden = true; });

// Easter egg: clicking the logo swaps the colour scheme (blue chrome <-> black & white glass). Look only.
function setMono(on) {
  document.body.classList.toggle("theme-mono", on);
  try { localStorage.setItem("polisher.mono", on ? "1" : "0"); } catch (_) {}
}
try { if (localStorage.getItem("polisher.mono") === "1") document.body.classList.add("theme-mono"); } catch (_) {}
document.querySelector(".logo").addEventListener("click", () => setMono(!document.body.classList.contains("theme-mono")));

const chkAuto = $("chk-autoreset"), chkAdaptive = $("chk-adaptive");
chkAuto.addEventListener("change", () => P.autoreset.setValue(chkAuto.checked));
chkAdaptive.addEventListener("change", () => { P.adaptive.setValue(chkAdaptive.checked); onParamsChanged(); userEdited(); });
P.autoreset.valueChangedEvent.addListener(() => (chkAuto.checked = !!P.autoreset.getValue()));
const chkChange = $("chk-resetchange");
chkChange.addEventListener("change", () => P.resetOnChange.setValue(chkChange.checked));
P.resetOnChange.valueChangedEvent.addListener(() => (chkChange.checked = !!P.resetOnChange.getValue()));

function restartMeasurement() { resetMeter(); grWindow = []; clipWindow = []; tameWindow = []; punchWindow = []; histReset(); }
$("btn-reset").addEventListener("click", () => { restartMeasurement(); toast("Loudness measurement reset. MATCH learns the level afresh."); });
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
// The song's style: Mixology's list of 75 (mixstyles.js, the same file in all three plug-ins), shared through the
// suite link: pick it here, in Mixology or in Voxology, and all three follow. It sets Auto-Master's genre (the six
// above are what the styles belong to); nothing else changes until you run Auto-Master.
const SONG = { key: "", name: "", version: -1 };
const AM_FIRST_STYLE = ["hiphop.modern", "rnb.smooth", "pop.modern", "electronic.edm", "rock.alternative", "folk.songwriter"];
const pickStyleNative = Juce.getNativeFunction("pickStyle");
// The Auto-Master genre a style belongs to (the same as MixHandoff.h's genreForMixStyle, which the plug-in uses).
const AM_GENRE_OF = { hiphop: 0, trap: 0, rnb: 1, gospel: 1, pop: 2, worship: 2, latin: 2, afro: 2, electronic: 3, rock: 4, folk: 5, country: 5, jazz: 5, cinematic: 5 };
const amGenreFor = (key) => AM_GENRE_OF[String(key).split(".")[0]] ?? comboIndex(P.amGenre);
function pickSongStyle(key) {
  const st = SONG_STYLES.find((s) => s.key === key);
  if (!st) return;
  SONG.key = st.key;
  SONG.name = st.fullName;
  pickStyleNative(st.key, st.fullName);
  refreshAmChoices();
}
function refreshAmChoices() {
  const g = comboIndex(P.amGenre);
  const genreName = (P.amGenre.properties.choices[g]) || GENRES[g];
  const st = SONG.key ? SONG_STYLES.find((s) => s.key === SONG.key) : null;
  const nameEl = $("genre-name");
  if (st) {
    nameEl.innerHTML = `<small>${st.genre.name}</small>${st.name}`;
    nameEl.title = `${st.fullName}: ${st.about} Auto-Master masters it as ${genreName}. Click for every style (the same in Mixology and Voxology).`;
  } else {
    nameEl.textContent = genreName;
    nameEl.title = "Click to pick your song's style (the same list as Mixology and Voxology)";
  }
  for (const [rowId, st] of [["row-intensity", P.amIntensity], ["row-target", P.amTarget]]) {
    const idx = comboIndex(st);
    const refSets = rowId === "row-target" && !!refInfo.profile;   // a reference sets the loudness instead
    $(rowId).querySelectorAll("button[data-i]").forEach((b) => {
      const on = Number(b.dataset.i) === idx && !refSets;
      b.className = "pill " + (on ? "pill-red" : "pill-blue");
      b.setAttribute("aria-pressed", String(on));
    });
  }
  fetchGenreCurve();   // the dotted line follows the genre (no-op when it already shows this one)
}
function stepGenre(d) {
  // The arrows step through the styles of the song's genre (as in Mixology); from the genre's first style when
  // none is chosen yet.
  const st = SONG_STYLES.find((s) => s.key === (SONG.key || AM_FIRST_STYLE[comboIndex(P.amGenre)])) || SONG_STYLES[0];
  const list = st.genre.styles;
  const i = list.findIndex((s) => s.id === st.id);
  pickSongStyle(`${st.genre.id}.${list[(i + d + list.length) % list.length].id}`);
}
// The picker: every genre, then its styles.
function openStylePicker() {
  let box = $("style-picker");
  if (!box) {
    box = document.createElement("div");
    box.id = "style-picker";
    box.className = "style-picker";
    box.setAttribute("role", "dialog");
    box.innerHTML = `<div class="manual-head"><span class="wm">SONG STYLE</span>
        <span class="stp-note">The same list as Mixology and Voxology: pick it in any of them and all three follow.</span>
        <button type="button" class="manual-close" aria-label="Close" title="Close (Esc)">&times;</button></div>
      <div class="stp-cols"><nav class="stp-genres"></nav><div class="stp-styles"></div></div>`;
    $("stage").appendChild(box);
    box.querySelector(".manual-close").addEventListener("click", () => (box.hidden = true));
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !box.hidden) box.hidden = true; });
  }
  const now = SONG_STYLES.find((s) => s.key === (SONG.key || AM_FIRST_STYLE[comboIndex(P.amGenre)])) || SONG_STYLES[0];
  let shownGenre = now.genre;
  const genres = box.querySelector(".stp-genres"), styles = box.querySelector(".stp-styles");
  const draw = () => {
    genres.replaceChildren(...STYLE_GENRES.map((gn) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "stp-genre" + (gn === shownGenre ? " sel" : "") + (gn === now.genre ? " cur" : "");
      b.innerHTML = `<i style="background:${gn.colour}"></i>${gn.name}`;
      b.addEventListener("click", () => { shownGenre = gn; draw(); });
      return b;
    }));
    styles.replaceChildren(...shownGenre.styles.map((s) => {
      const key = `${shownGenre.id}.${s.id}`;
      const b = document.createElement("button");
      b.type = "button";
      b.className = "stp-style" + (key === SONG.key ? " sel" : "");
      b.innerHTML = `<b>${s.name}</b><span>${s.about}</span>`;
      b.addEventListener("click", () => {
        pickSongStyle(key);
        box.hidden = true;
        toast(`Style: ${shownGenre.name} - ${s.name}. Auto-Master masters it as ${GENRES[amGenreFor(key)]}; Mixology and Voxology follow.`);
      });
      return b;
    }));
  };
  draw();
  box.hidden = false;
}
$("genre-name").addEventListener("click", openStylePicker);

// Your DAW: Settings' pick ("Found: ..." = the program Masterology runs in).
const pickDawNative = Juce.getNativeFunction("pickDaw");
function fillDawSelect() {
  const sel = $("daw-select");
  const found = dawOf(DAW.detected);
  sel.replaceChildren(...[["", `Found: ${found.key === "other" ? "not known" : found.name}`], ...DAWS.map((d) => [d.key, d.name])].map(([v, t]) => {
    const o = document.createElement("option");
    o.value = v;
    o.textContent = t;
    return o;
  }));
  sel.value = DAW.picked ? DAW.key : "";
}
$("daw-select").addEventListener("change", (e) => {
  const v = e.target.value;
  pickDawNative(v);
  DAW.key = v || dawOf(DAW.detected).key;
  DAW.picked = !!v;
  renderLearn();
});
fillDawSelect();
$("genre-prev").addEventListener("click", () => stepGenre(-1));
$("genre-next").addEventListener("click", () => stepGenre(1));
for (const [rowId, st] of [["row-intensity", P.amIntensity], ["row-target", P.amTarget]])
  $(rowId).querySelectorAll("button[data-i]").forEach((b) =>
    b.addEventListener("click", () => {
      if (rowId === "row-target" && refInfo.profile) {
        toast(`Auto-Master is matching \u201c${refInfo.profile.name}\u201d, so its loudness is the target. Press \u00d7 on the reference to use ${b.textContent} instead.`);
        return;
      }
      st.setChoiceIndex(Number(b.dataset.i)); refreshAmChoices();
    }));
for (const st of [P.amGenre, P.amIntensity, P.amTarget]) {
  st.valueChangedEvent.addListener(refreshAmChoices);
  st.propertiesChangedEvent.addListener(refreshAmChoices);
}

let amRequested = false;       // this session pressed Auto-Master (so a new report opens in Learn)
let toneRequested = false;     // this session pressed Tone Steady's LEARN (so its result is announced)
let amFinishClicked = false;   // the listen was finished by hand (not by the song ending)
let amSeenVersion = -1;
let amOpenedTime = null;       // the report already opened for the user (by its timestamp)
$("btn-automaster").addEventListener("click", async () => {
  if (M.amState === 1) {
    // Listening: from 12 s of music on, a click uses what it heard; before that it stops.
    if (M.amCanFinish) { finishListening(); return; }
    amCancel(); amRequested = false; toneRequested = false; M.amState = 0; updateAmButton(); renderLearn();
    toast(M.amMode === 1 ? "Stopped learning. Tone Steady keeps what it had." : "Auto-Master stopped listening. Nothing was changed.");
    return;
  }
  if (M.amState === 2) return;
  const started = await amStart();
  if (started === false) {
    toast("Masterology isn't ready yet: press play in your DAW once, then click Auto-Master again.");
    return;
  }
  amRequested = true;
  amFinishClicked = false;
  Object.assign(M, { amState: 1, amMode: 0, amProgress: 0, amHearing: false, amHeard: 0, amLoudAt: -1, amCanFinish: false });
  if (learnOn) learnView = "listen";
  updateAmButton();
  renderLearn();
  toast("Play your whole song from the start. Auto-Master finishes by itself when it ends (or click again after 12 seconds).");
});
// What the Auto-Master button says while it listens.
function amListenText() {
  const heard = M.amHeard || 0;
  if (!heard) return M.amHearing ? "Listening…" : "Press play · from the start of your song";
  if (!M.amCanFinish) return `${fmtTime(heard)} heard · keep playing · click to stop`;
  if (!M.amHearing) return heard >= AM_AUTO_FINISH ? `No music · finishing unless you play on` : `Paused at ${fmtTime(heard)} · play on, or click to finish`;
  const loud = M.amMode !== 1 && M.amLoudAt >= 0 ? ` · loudest at ${fmtTime(M.amLoudAt)}` : "";
  return `${fmtTime(heard)} heard${loud} · click to finish`;
}
function updateAmButton() {
  const btn = $("btn-automaster"), title = $("am-title"), sub = $("am-sub");
  const busy = M.amState === 1 || M.amState === 2;
  btn.classList.toggle("busy", busy);
  $("am-progress").hidden = M.amState !== 1;
  $("am-progress").classList.toggle("ready", !!M.amCanFinish);
  $("am-progress-fill").style.width = (clamp(M.amProgress, 0, 1) * 100).toFixed(0) + "%";
  if (M.amState === 1) {
    title.textContent = M.amMode === 1 ? "LEARNING TONE" : "LISTENING";
    sub.textContent = amListenText();
  } else if (M.amState === 2) {
    title.textContent = "THINKING";
    sub.textContent = `Choosing every setting (${amThinkTime()})…`;
  } else {
    title.textContent = "AUTO-MASTER";
    sub.textContent = !(amReport && amReport.ok) ? "Click, then play your whole song"
      : M.aaOn && M.aaEdits ? `Auto Analyze: ${M.aaEdits} new edit${M.aaEdits === 1 ? "" : "s"} in Learn`
      : M.aaOn ? "Done · Auto Analyze listening" : "Done · click to run again";
  }
  if (tsTarget.refresh) tsTarget.refresh();
}

// ---------------------------------------------------------------------------------------------
// Undo / redo: the plug-in records every change made here (one knob move = one step), each Auto-Master
// run, Apply and snapshot recall. The arrows say what they will undo / redo.
const IS_MAC = /Mac/.test(navigator.platform || "");
const KEY = IS_MAC ? "⌘" : "Ctrl+";
function refreshHistory() {
  for (const [id, label, verb, keys] of [["btn-undo", M.undo, "Undo", `${KEY}Z`], ["btn-redo", M.redo, "Redo", IS_MAC ? "⇧⌘Z" : "Ctrl+Shift+Z or Ctrl+Y"]]) {
    const b = $(id), on = !!label;
    const title = on ? `${verb}: ${label} (${keys})` : `Nothing to ${verb.toLowerCase()}`;
    if (b.title === title && b.disabled === !on) continue;
    b.disabled = !on;
    b.classList.toggle("is-disabled", !on);
    b.title = title;
    b.setAttribute("aria-label", title);
  }
}
async function stepHistory(redo) {
  if (!(redo ? M.redo : M.undo)) return;
  let r = null;
  try { r = await (redo ? histRedo() : histUndo()); } catch (e) { r = null; }
  if (r && typeof r === "object") { M.undo = r.undo || ""; M.redo = r.redo || ""; }
  refreshHistory();
  if (r && r.done) { toast(`${redo ? "Redone" : "Undone"}: ${r.done}`); userEdited(); }   // the sound changed: Integrated starts over
  refreshAllCells();
}
// Space and Enter. In Cubase, Space is play / stop: after clicking a button (Auto-Master, MATCH, a snapshot...)
// pressing Space to start the song must not press that button again (it would cancel Auto-Master's listen,
// turn MATCH off, recall the snapshot again). So a button clicked with the mouse doesn't keep the keyboard,
// and Space / Enter only press buttons for someone moving around with Tab. A box you type a value into,
// Enter on a focused knob (type a value) and the arrow keys work as always.
let keyboardNav = false;   // Tab was pressed since the last mouse click
document.addEventListener("pointerdown", () => { keyboardNav = false; }, true);
document.addEventListener("click", (e) => {
  if (e.detail === 0 || keyboardNav) return;   // a click made with Space / Enter keeps its focus ring
  const el = document.activeElement;
  if (el && el !== document.body && el.matches && el.matches("button, input[type=checkbox]")) el.blur();
}, true);
const isTyping = (t) => !!(t && t.closest && t.closest("input:not([type=checkbox]), textarea, [contenteditable]"));
function swallowKey(e) {
  if (e.key === "Tab") { if (e.type === "keydown") keyboardNav = true; return; }
  if (e.key !== " " && e.key !== "Spacebar" && e.key !== "Enter") return;
  if (isTyping(e.target) || keyboardNav) return;
  if (e.key === "Enter" && e.target && e.target.closest && e.target.closest("[role=slider]")) return;   // Enter on a knob: type a value
  e.preventDefault();
  e.stopPropagation();
}
for (const type of ["keydown", "keypress", "keyup"]) document.addEventListener(type, swallowKey, true);

// The web view is a browser underneath. Its own right-click menu (Back, Reload, Save as, Print) has no place in a
// plug-in: it stays shut, except over the manual's text and a value box (to copy, paste). On Windows, WebView2 also
// takes browser keys while Masterology has the keyboard: F3 opens a Find bar (F3 is Cubase's MixConsole key),
// Ctrl+P Print (Cubase's Pool), F5 / Ctrl+R reload the page (the module, Learn and the history start over), F7
// caret browsing, Ctrl + / - / 0 and Ctrl+wheel zoom. Those are switched off. (On a Mac the web view has no such
// keys, and Cmd-keys must reach the host: Cmd+S saves the project in Logic.)
document.addEventListener("contextmenu", (e) => {
  if (!(e.target.closest && e.target.closest("#manual-body, input, textarea"))) e.preventDefault();
});
const BROWSER_KEYS = new Set(["F3", "F5", "F7", "BrowserBack", "BrowserForward", "BrowserRefresh", "BrowserSearch"]);
const BROWSER_CTRL_KEYS = new Set(["f", "g", "p", "r", "s", "u", "o", "j", "h", "+", "=", "-", "_", "0"]);
if (!IS_MAC) {
  document.addEventListener("keydown", (e) => {
    const ctrl = e.ctrlKey && !e.altKey && !e.metaKey && BROWSER_CTRL_KEYS.has((e.key || "").toLowerCase());
    const back = e.altKey && (e.key === "ArrowLeft" || e.key === "ArrowRight") && !isTyping(e.target);
    if (BROWSER_KEYS.has(e.key) || ctrl || back) e.preventDefault();
  }, true);
  document.addEventListener("wheel", (e) => { if (e.ctrlKey) e.preventDefault(); }, { capture: true, passive: false });
}

$("btn-undo").addEventListener("click", () => stepHistory(false));
$("btn-redo").addEventListener("click", () => stepHistory(true));
// Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on a Mac) while Masterology's window has the keyboard. A box you are
// typing a value into keeps its own undo.
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && storeArmed) { storeArmed = false; refreshSnapshots(); return; }
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  if (e.target && e.target.closest && e.target.closest("input, textarea, [contenteditable]")) return;
  const k = (e.key || "").toLowerCase();
  if (k === "z") { e.preventDefault(); stepHistory(e.shiftKey); }
  else if (k === "y") { e.preventDefault(); stepHistory(true); }
});

// ---------------------------------------------------------------------------------------------
// Snapshots S1..S4 (not A/B: A/B is the original vs the master). An empty slot stores what you hear;
// a stored one switches to it (one undo step); STORE then a slot (or Shift-click) replaces a slot.
let storeArmed = false;
let snapLearnShown = false;   // the snapshot Learn page opens by itself once, the first time a slot is stored
const snapTime = (iso) => {
  const d = iso ? new Date(iso) : null;
  return d && !isNaN(d) ? d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) : "";
};
let snapSig = "";
function refreshSnapshots() {
  const snaps = M.snaps || [];
  const sig = JSON.stringify([snaps, M.snapActive, M.snapModified, storeArmed]);
  if (sig === snapSig) return;
  snapSig = sig;
  document.querySelectorAll("#snaps .snap").forEach((b) => {
    const k = Number(b.dataset.slot), s = snaps[k] || {}, filled = !!s.filled;
    const active = filled && M.snapActive === k, changed = active && !!M.snapModified;
    const text = `S${k + 1}${changed ? "•" : ""}`;
    if (b.textContent !== text) b.textContent = text;
    b.className = "pill snap " + (active ? "pill-red" : "pill-blue") + (filled ? "" : " empty") + (storeArmed ? " armed" : "");
    b.setAttribute("aria-pressed", String(active));
    const from = s.note ? ` after “${s.note}”` : "";
    b.title = !filled ? `Snapshot ${k + 1} is empty: click to keep the settings you hear now`
      : storeArmed ? `Replace Snapshot ${k + 1} with the settings you hear now`
      : `Snapshot ${k + 1}${active ? (changed ? ", playing, changed since (•)" : ", playing") : ""}: stored ${snapTime(s.time)}${from}. `
        + "Click to switch to it (undo goes back). Shift-click to replace it.";
    b.setAttribute("aria-label", b.title);
  });
  const st = $("btn-store");
  st.className = "pill snap-store " + (storeArmed ? "pill-red" : "pill-blue");
  st.textContent = storeArmed ? "PICK SLOT" : "STORE";
  st.setAttribute("aria-pressed", String(storeArmed));
}
function storeIn(k) {
  const replaced = !!((M.snaps || [])[k] || {}).filled;
  snapStore(k);
  storeArmed = false;
  M.snaps = (M.snaps || []).slice();
  M.snaps[k] = { filled: true, time: new Date().toISOString(), note: M.undo };
  M.snapActive = k; M.snapModified = false;
  refreshSnapshots();
  toast(replaced ? `Snapshot ${k + 1} replaced with the settings you hear now.`
                 : `Kept in S${k + 1}. Click S${k + 1} any time to come back to these settings.`);
  if (!snapLearnShown) { snapLearnShown = true; learnView = "snap"; renderLearn(); }
}
document.querySelectorAll("#snaps .snap").forEach((b) => b.addEventListener("click", async (e) => {
  const k = Number(b.dataset.slot), filled = !!((M.snaps || [])[k] || {}).filled;
  if (storeArmed || !filled || e.shiftKey) { storeIn(k); return; }
  const ok = await snapRecall(k);
  if (ok === false) return;
  M.snapActive = k; M.snapModified = false;
  refreshSnapshots();
  refreshAllCells();
  userEdited();
  toast(`Snapshot ${k + 1}: its settings glide in. The undo arrow goes back.`);
}));
$("btn-store").addEventListener("click", () => {
  storeArmed = !storeArmed;
  refreshSnapshots();
  if (storeArmed) { toast("Pick a slot, S1 to S4: it keeps the settings you hear now."); learnView = "snap"; renderLearn(); }
});

async function fetchReport(openIt) {
  let text = "";
  try { text = await amGetReport(); } catch (e) { text = ""; }
  try { amReport = text ? JSON.parse(text) : null; } catch (e) { amReport = null; }
  // Open a report only once, and only when it's a new one from a run the user started here.
  if (openIt && amRequested && amReport && amReport.time !== amOpenedTime) {
    amOpenedTime = amReport.time;
    amRequested = false;
    if (amReport.ok) userEdited();   // Auto-Master changed the sound: Integrated starts over
    learnView = "report";
    toast(amReport.ok ? "Auto-Master set every module. Learn explains each choice."
      : amReport.notAFullMix ? "That sounds like one track, not a full mix. Nothing was changed. See Learn."
      : "Auto-Master couldn't hear enough music. See Learn.");
  }
  if (!amReport && learnView === "report") learnView = "module";
  refreshAllCells();
  updateAmButton();
  refreshLearn();
}
// Knobs show values pushed by the plug-in; after Auto-Master or Undo, redraw them all.
function refreshAllCells() {
  for (const c of [...maximizerCells, ...clipperCells, ...eqCells, ...bassCells, ...imagerCells, dynPicker, ...dynCellsByBand.flat(), ...satCells, ...glueCells]) if (c.refresh) c.refresh();
  onParamsChanged();
}

// ---------------------------------------------------------------------------------------------
// Match ref: load a reference song, show its tone on the spectrum, compare it in Learn.
let refSeenVersion = -1;
let refRequested = false;      // a load is running (so its result is announced when it ends)
let refClicked = 0;            // when the pill was last clicked (the dialog may still be open, or was cancelled)
let refOffset = null;          // dB the reference curve is moved by: loudness playing now minus the reference's
const mmss = (sec) => `${Math.floor(sec / 60)}:${String(Math.floor(sec % 60)).padStart(2, "0")}`;
const signed = (v, digits = 1) => `${v > 0.049 ? "+" : ""}${fmtNum(v, digits)}`;

async function fetchReference() {
  let text = "";
  try { text = await refGet(); } catch (e) { text = ""; }
  let info = null;
  try { info = text ? JSON.parse(text) : null; } catch (e) { info = null; }
  const before = refInfo.profile && refInfo.profile.name;
  refInfo = { state: info ? info.state : 0, message: (info && info.message) || "", profile: (info && info.profile) || null };
  if (!refInfo.profile || refInfo.profile.name !== before) refOffset = null;
  if (refInfo.state === 1) refRequested = true;
  // A load that ended before we saw it start still counts if the user picked a file a moment ago.
  const justPicked = refClicked && performance.now() - refClicked < 600000 && (refInfo.message || (refInfo.profile && refInfo.profile.name !== before));
  if ((refRequested || justPicked) && refInfo.state !== 1) {
    refRequested = false;
    refClicked = 0;
    if (refInfo.message) toast(refInfo.message);
    else if (refInfo.profile) {
      learnView = "ref";
      toast(`Reference loaded: “${refInfo.profile.name}”. Press Auto-Master and play your song from the start.`);
    }
  }
  refreshRefPill();
  refreshAmChoices();
  drawRefCurve();
  refreshLearn();
}
// What Auto-Master aims for with a reference: its loudest 12 s (compared with the loudest 12 s of your song).
// References saved before that was measured only have their loudest 30 s.
const refTargetLufs = (p) => (p && isFinite(p.hookLufs) && p.hookLufs > -99 ? p.hookLufs : p ? p.lufs : NaN);
// The display reads the same sound higher at a higher sample rate (its 4096 bins get wider), so a reference
// measured at its file's rate is moved by the difference (the engine's displayRateShiftDb).
const refRateShift = (p) => (p && p.sampleRate > 0 && M.sr > 0 ? 10 * Math.log10(M.sr / p.sampleRate) : 0);
function refreshRefPill() {
  const btn = $("btn-ref"), p = refInfo.profile;
  const loading = M.refState === 1 || refInfo.state === 1;
  btn.textContent = loading ? "Loading…" : p ? p.name : "Match ref";
  btn.className = "pill ref-pill " + (p && !loading ? "pill-red" : "pill-blue");
  btn.setAttribute("aria-pressed", String(!!p));
  btn.title = loading ? "Reading the reference song…"
    : p ? `Matching “${p.name}” (${fmtNum(refTargetLufs(p))} LUFS in its loudest 12 seconds). Click to load a different song; × goes back to the genre targets.`
        : "Match a reference: load a released song you want your master to sound like";
  $("btn-ref-clear").hidden = !p || loading;
  $("row-target").classList.toggle("ref-active", !!p);
}
// The reference's average tone, moved to the loudness of what's playing now, so the two compare like for like.
function drawRefCurve() {
  const p = refInfo.profile, path = $("sp-ref");
  const has = !!(p && p.displayDb && p.displayDb.length);
  $("legend-ref").hidden = !has;
  if (!has) { path.setAttribute("d", ""); return; }
  if (M.shortTerm > -50) {
    const want = M.shortTerm - p.lufs + refRateShift(p);
    refOffset = refOffset === null ? want : refOffset + 0.05 * (want - refOffset);   // ~1 s glide
  }
  const off = refOffset === null ? 0 : refOffset;
  path.setAttribute("d", spectrumPath(p.displayDb.map((v) => v + off)));
}

// ---------------------------------------------------------------------------------------------
// The genre's typical tone on the spectrum (dotted): the balance Auto-Master's EQ aims for, computed by
// the engine (getGenreCurve) for the chosen genre at the plug-in's sample rate, and moved to the
// loudness of what's playing like the reference's curve. With a reference loaded Auto-Master aims for
// the reference instead, so the genre line is hidden unless the user clicks its legend entry.
let genreCurve = null;         // { genre, name, lufs, sr, displayDb } from the plug-in
let genreOffset = null;        // dB the genre line is moved by: loudness playing now minus genreCurve.lufs
let genreAsked = "";           // genre and sample rate of the last request (so a stale answer is dropped)
const genreLinePref = { noRef: true, withRef: false };
try { Object.assign(genreLinePref, JSON.parse(localStorage.getItem("polisher.genreLine") || "{}")); } catch (_) {}
const genreShort = (name) => (name || "").split(" /")[0];   // "Trap / Hip-Hop" -> "Trap"
async function fetchGenreCurve() {
  const g = comboIndex(P.amGenre), sr = M.sr > 0 ? M.sr : 48000, key = `${g}@${sr}`;
  if (key === genreAsked) return;
  genreAsked = key;
  let curve = null;
  try { curve = JSON.parse(await genreGet(g)); } catch (e) { curve = null; }
  if (genreAsked !== key) return;      // the genre changed again while this one was being computed
  if (!curve || !curve.displayDb || !curve.displayDb.length) { genreAsked = ""; return; }
  if (!genreCurve || genreCurve.lufs !== curve.lufs) genreOffset = null;
  genreCurve = curve;
  drawGenreCurve();
  if (learnOn && learnView === "module" && selected === "eq") scheduleLearn();
}
function genreLineOn() { return refInfo.profile ? genreLinePref.withRef : genreLinePref.noRef; }
let genreLegendKey = "";
function drawGenreCurve() {
  const path = $("sp-genre"), btn = $("legend-genre");
  const full = genreCurve ? genreCurve.name : GENRES[comboIndex(P.amGenre)];
  const on = genreLineOn(), key = `${full}|${on}|${!!refInfo.profile}`;
  if (key !== genreLegendKey) {
    genreLegendKey = key;
    $("legend-genre-name").textContent = `${genreShort(full)} target`;
    btn.setAttribute("aria-pressed", String(on));
    btn.title = (refInfo.profile && !on ? "Hidden while a reference is loaded: Auto-Master aims for the reference's tone instead. " : "")
      + `The tone typical ${full} masters have (what Auto-Master's EQ aims for), at the loudness of what's playing. Click to ${on ? "hide" : "show"} it.`;
  }
  if (!genreCurve || !on) { path.setAttribute("d", ""); return; }
  if (M.shortTerm > -50) {
    const want = M.shortTerm - genreCurve.lufs;
    genreOffset = genreOffset === null ? want : genreOffset + 0.05 * (want - genreOffset);   // ~1 s glide
  }
  const off = genreOffset === null ? 0 : genreOffset;
  path.setAttribute("d", spectrumPath(genreCurve.displayDb.map((v) => v + off)));
}
$("legend-genre").addEventListener("click", () => {
  genreLinePref[refInfo.profile ? "withRef" : "noRef"] = !genreLineOn();
  try { localStorage.setItem("polisher.genreLine", JSON.stringify(genreLinePref)); } catch (_) {}
  drawGenreCurve();
  if (learnOn && learnView === "module" && selected === "eq") scheduleLearn();
});
P.amGenre.valueChangedEvent.addListener(fetchGenreCurve);
P.amGenre.propertiesChangedEvent.addListener(fetchGenreCurve);

// ---------------------------------------------------------------------------------------------
// Loudness history: the master's short-term loudness (red), the mix's (white) and the Maximizer's gain
// reduction (blue, hanging from the top) over the last 2 minutes of playback, with the target line.
// Built from the meter frames (30 a second), one point per half second. Time only runs while the song
// plays: stopping leaves a gap and the graph waits; RESET clears it.
const HIST_STEP_MS = 500, HIST_POINTS = 240;                 // 240 x 0.5 s = 2 minutes
const HIST_TOP = -4, HIST_BOTTOM = -28;                      // LUFS at the top and bottom of the screen
const HIST_GR_DEPTH = 12, HIST_GR_SPAN = 0.5;                // 12 dB of gain reduction fills the top half
const HIST_SETTLE_MS = 3000;                                 // Short-term needs 3 s of the song after play starts
const hist = { pts: [], acc: null, gapNext: true, playingSince: null, lastFrame: 0, dirty: true };
let histView = false;
try { histView = localStorage.getItem("polisher.view") === "history"; } catch (_) {}

function histReset() { hist.pts = []; hist.acc = null; hist.gapNext = true; hist.dirty = true; drawHistory(); }
function histFlush() {
  const a = hist.acc;
  hist.acc = null;
  if (!a || !a.n) return;
  hist.pts.push({ st: a.st / a.n, inSt: a.inN ? a.in / a.inN : null, gr: a.gr, gap: hist.gapNext });
  hist.gapNext = false;
  if (hist.pts.length > HIST_POINTS) hist.pts.splice(0, hist.pts.length - HIST_POINTS);
  hist.dirty = true;
}
function histBreak() { histFlush(); if (hist.pts.length) hist.gapNext = true; hist.playingSince = null; }
// One meter frame. `playing` comes from the plug-in (the host's transport); older builds don't send it.
function histFrame(now) {
  const stalled = hist.lastFrame && now - hist.lastFrame > 1000;   // the window was hidden, or the host stopped calling us
  hist.lastFrame = now;
  if (stalled || M.playing === false) { histBreak(); return; }
  if (hist.playingSince === null) hist.playingSince = now;
  if (now - hist.playingSince < HIST_SETTLE_MS) return;          // still filling with the time before play
  if (M.shortTerm <= -70) { histBreak(); hist.playingSince = now - HIST_SETTLE_MS; return; }   // silence in the song
  if (!hist.acc) hist.acc = { t0: now, n: 0, st: 0, in: 0, inN: 0, gr: 0 };
  const a = hist.acc;
  a.n++; a.st += M.shortTerm;
  if (M.inShort > -70) { a.inN++; a.in += M.inShort; }
  a.gr = Math.min(a.gr, M.gr || 0);                             // the deepest moment of the half second
  if (now - a.t0 >= HIST_STEP_MS) histFlush();
}
const histX = (i, n) => W - (n - 1 - i) * (W / (HIST_POINTS - 1));
const histY = (lufs) => clamp(((HIST_TOP - lufs) / (HIST_TOP - HIST_BOTTOM)) * H, 0, H);
const histGrY = (gr) => clamp(-gr / HIST_GR_DEPTH, 0, 1) * H * HIST_GR_SPAN;
// What Auto-Master aims for: the Target pill, or a loaded reference's loudness (kept within -16..-6 LUFS
// by Auto-Master, see kRefMinLufs / kRefMaxLufs).
function histTargetLufs() {
  if (refInfo.profile && isFinite(refTargetLufs(refInfo.profile))) return { lufs: clamp(refTargetLufs(refInfo.profile), -16, -6), ref: true };
  return { lufs: [-14, -9, -7][comboIndex(P.amTarget)], ref: false };
}
function buildHistoryGrid() {
  let lines = "";
  for (let i = 1; i < 4; i++) lines += `<line x1="0" y1="${(H * i) / 4}" x2="${W}" y2="${(H * i) / 4}"/>`;
  const labels = [];
  for (let s = 0; s <= 120; s += 30) {
    const x = histX(HIST_POINTS - 1 - (s * 1000) / HIST_STEP_MS, HIST_POINTS);
    if (s > 0 && s < 120) lines += `<line x1="${x.toFixed(1)}" y1="0" x2="${x.toFixed(1)}" y2="${H}"/>`;
    labels.push(`<span style="left:${((x / W) * 100).toFixed(2)}%">${s === 0 ? "now" : `${MINUS}${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`}</span>`);
  }
  $("hist-grid").innerHTML = lines;
  $("time-labels").innerHTML = labels.reverse().join("");
  $("hist-scale").innerHTML = [0, 1, 2, 3, 4].map((i) => `<span>${fmtNum(HIST_TOP + ((HIST_BOTTOM - HIST_TOP) * i) / 4, 0)}</span>`).join("");
  const y6 = histGrY(-6);
  $("hist-gr6").setAttribute("y1", y6); $("hist-gr6").setAttribute("y2", y6);
  $("hist-gr6-label").style.top = ((y6 / H) * 100).toFixed(2) + "%";
  $("hist-deep-rect").setAttribute("y", y6); $("hist-deep-rect").setAttribute("height", H - y6);   // past -6 dB: orange
}
// Loudest / quietest stretch and deepest limiting of what's on screen (Learn reads the same numbers).
function histStats() {
  const pts = hist.pts;
  const st = pts.map((p) => p.st).filter((v) => v > -40).sort((a, b) => a - b);
  const inSt = pts.map((p) => p.inSt).filter((v) => v !== null && v > -40).sort((a, b) => a - b);
  const pct = (arr, q) => arr[clamp(Math.round(q * (arr.length - 1)), 0, arr.length - 1)];
  const grs = pts.map((p) => p.gr);
  return {
    seconds: (pts.length * HIST_STEP_MS) / 1000,
    loud: st.length ? pct(st, 0.95) : null, quiet: st.length ? pct(st, 0.1) : null,
    mixLoud: inSt.length ? pct(inSt, 0.95) : null, mixQuiet: inSt.length ? pct(inSt, 0.1) : null,
    grDeepest: grs.length ? Math.min(...grs) : 0,
    grTypical: grs.length ? grs.reduce((a, b) => a + b, 0) / grs.length : 0,
  };
}
function drawHistory() {
  if (!histView) return;
  const pts = hist.pts, n = pts.length;
  const tgt = histTargetLufs();
  const ty = histY(tgt.lufs);
  $("hist-target").setAttribute("y1", ty); $("hist-target").setAttribute("y2", ty);
  $("hist-target-label").style.top = ((ty / H) * 100).toFixed(2) + "%";
  $("hist-target-label").textContent = `${tgt.ref ? "REF" : "TARGET"} ${fmtNum(tgt.lufs)}`;
  $("legend-target").lastChild.textContent = `${tgt.ref ? "Reference" : "Target"} ${fmtNum(tgt.lufs)} LUFS`;
  $("hist-empty").hidden = n > 0;
  if (!hist.dirty) return;
  hist.dirty = false;
  let master = "", mix = "", gr = "", gaps = "";
  let runStart = -1;
  const closeRun = (end) => {
    if (runStart < 0) return;
    let d = `M${histX(runStart, n).toFixed(1)},0`;
    for (let i = runStart; i <= end; i++) d += `L${histX(i, n).toFixed(1)},${histGrY(pts[i].gr).toFixed(1)}`;
    gr += d + `L${histX(end, n).toFixed(1)},0Z`;
    runStart = -1;
  };
  let mixOpen = false;
  for (let i = 0; i < n; i++) {
    const p = pts[i], x = histX(i, n).toFixed(1);
    if (p.gap && i > 0) {
      closeRun(i - 1);
      const gx = (histX(i, n) - W / (HIST_POINTS - 1) / 2).toFixed(1);
      gaps += `<line x1="${gx}" y1="0" x2="${gx}" y2="${H}"/>`;
    }
    if (runStart < 0) runStart = i;
    master += (p.gap || i === 0 ? "M" : "L") + x + "," + histY(p.st).toFixed(1);
    if (p.inSt === null) mixOpen = false;
    else { mix += (p.gap || !mixOpen ? "M" : "L") + x + "," + histY(p.inSt).toFixed(1); mixOpen = true; }
  }
  closeRun(n - 1);
  $("hist-master").setAttribute("d", master);
  $("hist-master-glow").setAttribute("d", master);
  $("hist-in").setAttribute("d", mix);
  $("hist-gr").setAttribute("d", gr);
  $("hist-gr-deep").setAttribute("d", gr);
  $("hist-gaps").innerHTML = gaps;
  const s = histStats();
  $("hist-stats").textContent = s.loud === null ? "" :
    `LOUD ${fmtNum(s.loud)} · QUIET ${fmtNum(s.quiet)} LUFS · GR UP TO ${s.grDeepest > -0.05 ? "0.0" : fmtNum(s.grDeepest)} dB`;
}
function setHistView(on) {
  const was = histView;
  histView = on;
  $("spectrum-screen").classList.toggle("view-history", on);
  $("view-spectrum").setAttribute("aria-pressed", String(!on));
  $("view-history").setAttribute("aria-pressed", String(on));
  $("spectrum-svg").setAttribute("aria-label", on ? "Loudness history of the master and your mix, with the Maximizer's gain reduction"
                                                  : "Live spectrum of your mix and the master");
  try { localStorage.setItem("polisher.view", on ? "history" : "spectrum"); } catch (_) {}
  hist.dirty = true;
  drawHistory();
  if (on !== was) {
    if (on) learnView = "history";
    else if (learnView === "history") learnView = "module";
    renderLearn();
  }
}
$("view-spectrum").addEventListener("click", () => setHistView(false));
$("view-history").addEventListener("click", () => { if (histView) { learnView = "history"; renderLearn(); } else setHistView(true); });

function renderHistoryLearn(body) {
  const tgt = histTargetLufs(), s = histStats();
  body.appendChild(head("LOUDNESS HISTORY"));
  body.appendChild(para("l-p", `The red line is your master's loudness (Short-term: the last 3 seconds) over the last 2 minutes of playback; the white line is your mix as it comes in. The dashed line is ${tgt.ref ? "the reference's loudness" : "your target"}, ${fmtNum(tgt.lufs)} LUFS.`));
  body.appendChild(item("Verse and chorus", null, "A chorus or drop usually sits 1–3 dB above the verse, so the red line steps up and down with your song. If the white line steps but the red one stays flat, the Maximizer is squashing the difference."));
  body.appendChild(item("Blue shading (Maximizer GR)", null, "How much the Maximizer turns the loudest moments down: the deeper it hangs from the top, the harder it works. Short dips of 1–3 dB on the hits are normal. Past 6 dB it turns orange: if it stays orange, or dips past 9 dB, that is heavy limiting and the kick and snare lose punch."));
  body.appendChild(item("Gaps", null, "Time only runs while your song plays: a gap is where you stopped. RESET (under the Output meter) clears the graph."));
  body.appendChild(sep());
  body.appendChild(head("WHAT IT SHOWS NOW"));
  if (s.seconds < 20 || s.loud === null) {
    body.appendChild(para("l-p", "Play at least 20 seconds of your song, ideally a verse going into the chorus. The numbers appear here."));
    return;
  }
  const lift = s.loud - s.quiet, mixLift = s.mixLoud !== null ? s.mixLoud - s.mixQuiet : null;
  body.appendChild(para("l-p", `Over ${Math.round(s.seconds)} s of playback the master runs from about ${fmtNum(s.quiet)} LUFS in its quieter parts to ${fmtNum(s.loud)} LUFS in its loudest (${lift.toFixed(1)} dB apart${mixLift !== null ? `; your mix ${mixLift.toFixed(1)} dB` : ""}). The Maximizer takes up to ${fmtNum(-Math.min(0, s.grDeepest))} dB off, about ${fmtNum(-Math.min(0, s.grTypical))} dB on average.`));
  if (s.grTypical < -6 || s.grDeepest < -9) {
    body.appendChild(tip("HEAVY LIMITING", `The shading goes down to ${fmtNum(-s.grDeepest)} dB${s.grTypical < -6 ? ` and stays around ${fmtNum(-s.grTypical)} dB` : ""}: the Maximizer is squashing the loud parts hard.`, false,
      { need: "Only if the kick or snare sound weaker than in your mix.",
        steps: ["Turn on MATCH and flip A/B, listening only to the kick and snare.",
                "If they lost punch: turn Drive down 1–2 dB (Maximizer), or turn on the Soft Clipper (chain, left) so it takes some of the peaks.",
                "Play the same part again and check that the shading stays blue (less than 6 dB) most of the time."] }));
  } else if (mixLift !== null && mixLift >= 3 && lift < mixLift - 2) {
    body.appendChild(tip("SECTIONS FLATTENED", `Your mix rises ${mixLift.toFixed(1)} dB from its quieter to its louder parts; the master only ${lift.toFixed(1)} dB. The loud parts are being held down more than the quiet ones.`, false,
      { need: "Only if the chorus or drop no longer hits harder than the verse.",
        steps: ["Turn on MATCH and play the change from verse into chorus on A, then on B.",
                "If the chorus lifts less on B, lower Drive 1–2 dB and play it again.",
                "Check here that the red line steps up at the chorus again."] }));
  } else {
    body.appendChild(tip("LOOKS HEALTHY", "Your song keeps its shape and the limiting is moderate. To check a different part, play it: the graph keeps the last 2 minutes.", true));
  }
}
$("btn-ref").addEventListener("click", async () => {
  if (M.refState === 1) return;
  refClicked = performance.now();
  learnView = "ref";
  renderLearn();
  try { await refLoad(); } catch (e) { refClicked = 0; }
});
$("btn-ref-clear").addEventListener("click", async (e) => {
  e.stopPropagation();
  const name = refInfo.profile ? refInfo.profile.name : "";
  try { await refClear(); } catch (err) { /* the next meter frame shows the state either way */ }
  toast(`Cleared “${name}”. Auto-Master aims for the genre and the target again.`);
});

const CMP_BANDS = ["Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air"];
// A compact table: the reference, your mix (as Auto-Master heard it) and the master.
function refCompare(r) {
  const ref = r.reference, mix = r.mixStats, mas = r.masterStats;
  const wrap = document.createElement("div");
  const table = document.createElement("table");
  table.className = "l-cmp";
  const add = (cells, header) => {
    const tr = document.createElement("tr");
    cells.forEach(([text, cls], i) => {
      const c = document.createElement(header ? "th" : "td");
      c.textContent = text;
      if (cls) c.className = cls;
      if (header && i === 0) c.style.textAlign = "left";
      tr.appendChild(c);
    });
    table.appendChild(tr);
  };
  // Master column: green when it's within reach of the reference, red when it's still far off.
  const closeness = (v, refV, near, far) => (Math.abs(v - refV) <= near ? "near" : Math.abs(v - refV) >= far ? "far" : "");
  const side = (db) => (db <= -40 ? "mono" : fmtNum(db));
  add([["", ""], ["REF", ""], ["MIX", ""], ["MASTER", ""]], true);
  // Loudest 12 s against loudest 12 s: the reference's hook, your mix as it arrives (before Input Trim and
  // Stem EQ), and the master.
  const refLoud = refTargetLufs(ref);
  add([["Loudness, loudest 12 s (LUFS)"], [fmtNum(refLoud), "ref"], [fmtNum(r.inputLufs)], [fmtNum(r.outputLufs), closeness(r.outputLufs, refLoud, 1, 2.5)]]);
  add([["Deep sub vs bass (dB)"], [signed(ref.subVsBass), "ref"], [signed(mix.subVsBass)], [signed(mas.subVsBass), closeness(mas.subVsBass, ref.subVsBass, 1.5, 3)]]);
  add([["Bass stereo"], [`${Math.round(ref.bassWidth)} %`, "ref"], [`${Math.round(mix.bassWidth)} %`], [`${Math.round(mas.bassWidth)} %`, ""]]);
  add([["Mids width (dB)"], [side(ref.midSide), "ref"], [side(mix.midSide)], [side(mas.midSide), closeness(mas.midSide, ref.midSide, 1.5, 4)]]);
  add([["Highs width (dB)"], [side(ref.highSide), "ref"], [side(mix.highSide)], [side(mas.highSide), closeness(mas.highSide, ref.highSide, 1.5, 4)]]);
  wrap.appendChild(table);
  const tone = document.createElement("table");
  tone.className = "l-cmp";
  tone.style.marginTop = "8px";
  const tr0 = document.createElement("tr");
  for (const [i, t] of ["TONE vs REF (dB)", "MIX", "MASTER"].entries()) { const th = document.createElement("th"); th.textContent = t; if (i === 0) th.style.textAlign = "left"; tr0.appendChild(th); }
  tone.appendChild(tr0);
  CMP_BANDS.forEach((name, b) => {
    const dm = mix.bandDb[b] - ref.bandDb[b], dM = mas.bandDb[b] - ref.bandDb[b];
    const tr = document.createElement("tr");
    for (const [text, cls] of [[name, ""], [signed(dm), ""], [signed(dM), Math.abs(dM) <= 1.5 ? "near" : Math.abs(dM) >= 4 ? "far" : ""]]) {
      const td = document.createElement("td"); td.textContent = text; if (cls) td.className = cls; tr.appendChild(td);
    }
    tone.appendChild(tr);
  });
  wrap.appendChild(tone);
  wrap.appendChild(para("l-cmp-note", "Tone: how much more (+) or less (−) of each range than the reference, all at the same loudness. "
    + "Width: the stereo part against the center (closer to 0 = wider). Deep sub vs bass: 30–60 Hz against 60–120 Hz (higher = the 808 reaches lower). "
    + "Auto-Master closes part of each gap on purpose: copying a reference exactly would fight your own mix."));
  return wrap;
}

function howToUseRef() {
  const d = tip("HOW TO USE IT", "A reference is a released song you want your master to sound like.", true);
  const ol = document.createElement("ol");
  ol.className = "l-steps";
  for (const st of ["Pick a commercial trap song with the sound you want: the released WAV, FLAC or MP3 (a good-quality file, not a phone recording).",
                    "Click Match ref (next to the targets) and choose the file. Masterology measures its loudest part in a few seconds.",
                    "Press Auto-Master and play YOUR song from the start: it finds the loudest part by itself.",
                    "Turn on MATCH and A/B. The dashed line on the spectrum is the reference's tone; the × on the pill goes back to the genre targets."]) {
    const li = document.createElement("li"); li.textContent = st; ol.appendChild(li);
  }
  d.appendChild(ol);
  return d;
}

function renderRefLearn(body) {
  const p = refInfo.profile;
  const link = document.createElement("button");
  link.type = "button";
  link.className = "l-link";
  const toReport = !!(amReport && amReport.ok);
  link.textContent = toReport ? "← AUTO-MASTER REPORT" : "← BACK";
  link.addEventListener("click", () => { learnView = toReport ? "report" : "module"; renderLearn(); });
  body.appendChild(link);
  body.appendChild(head("MATCH A REFERENCE"));
  if (M.refState === 1 || refInfo.state === 1) {
    body.appendChild(para("l-p", "Reading the song and measuring its loudest part… This takes a few seconds (longer for a long MP3). Your music keeps playing."));
    body.appendChild(howToUseRef());
    return;
  }
  if (!p) {
    body.appendChild(para("l-p", "Load a released song you like the sound of, and Auto-Master aims for its tone, loudness, stereo width and low end instead of the typical balance of your genre."));
    body.appendChild(howToUseRef());
    return;
  }
  const hookNote = isFinite(p.hookLufs) && p.hookLufs > -99 ? ` Its loudest 12 seconds measure ${fmtNum(p.hookLufs)} LUFS: that's what Auto-Master aims for, compared with the loudest 12 seconds of your song.` : "";
  body.appendChild(para("l-p", `Matching “${p.name}”. Masterology measured its loudest ${Math.round(p.partSeconds)} s (from ${mmss(p.startSeconds)}): ${fmtNum(p.lufs)} LUFS, the whole song ${fmtNum(p.trackLufs)} LUFS.${hookNote} Only these measurements are saved with your project, not the song.`));
  body.appendChild(item("What Auto-Master takes from it", null,
    "The tone (all seven EQ bands), the loudness (kept between −16 and −6 LUFS), the stereo width of the mids and highs, and the low end: how stereo the bass is and how deep the 808 reaches. Your genre still decides compression, saturation, clipping and punch, which a reference can't show."));
  const r = amReport;
  if (r && r.ok && r.reference && r.reference.name === p.name && r.mixStats && r.masterStats) {
    body.appendChild(sep());
    body.appendChild(head("REFERENCE vs YOUR MIX vs MASTER"));
    body.appendChild(refCompare(r));
  } else {
    body.appendChild(tip("COMPARE IT", "Press Auto-Master and play your song from the start. It aims for this reference, and this panel then compares the reference, your mix and the master side by side.", true));
  }
  body.appendChild(howToUseRef());
}

// Learn for snapshots and undo (opens once by itself, the first time a slot is stored, and with STORE).
function renderSnapLearn(body) {
  const link = document.createElement("button");
  link.type = "button";
  link.className = "l-link";
  link.textContent = "← BACK";
  link.addEventListener("click", () => { learnView = "module"; renderLearn(); });
  body.appendChild(link);
  body.appendChild(head("SNAPSHOTS S1–S4"));
  body.appendChild(para("l-p", "Each slot keeps a whole sound: every knob and switch in every module, plus the Auto-Master report that explains it and the song's tone Tone Steady learned (if it learned one). They're saved with your {daw} project."));
  body.appendChild(item("Empty slot", null, "Click it to keep the settings you hear now."));
  body.appendChild(item("Stored slot", null, "Click it to switch to those settings. Knobs glide there and switches fade over, so nothing clicks or drops out, and the undo arrow takes you back."));
  body.appendChild(item("Red slot", null, "The one playing. A dot (S1•) means you've changed something since. To keep the change, click STORE, then the slot (or Shift-click the slot)."));
  body.appendChild(item("Not in a snapshot", null, "A / B, MATCH, MONO, PHONE and Listen, and Auto-Master's genre, intensity and target. Switching snapshots never changes how you listen. S1–S4 are not A / B: A is always your original mix, B the master."));
  body.appendChild(sep());
  body.appendChild(head("A FAIR COMPARE WITH MATCH"));
  body.appendChild(para("l-p", "The first time you hear a snapshot, MATCH learns how loud it plays compared with your mix: roughly after half a second of B, exactly after about 3 seconds. Play it for about 15 seconds and MATCH remembers its level, so when you flip back later it's matched at once (it holds that level for 3 seconds while the loudness reading catches up)."));
  body.appendChild(tip("TRY THIS", "Balanced vs Punchy: pick Balanced, click Auto-Master and play your whole song from the start (it finishes by itself), then click S1. Pick Punchy, run Auto-Master the same way and click S2. Turn on MATCH, play your chorus and listen to S1 for 15 seconds, then S2 for 15 seconds. Now flip S1 / S2 every few seconds: you hear punch and weight, not volume. Click the one you like and keep going.", true));
  body.appendChild(sep());
  body.appendChild(head("UNDO AND REDO"));
  body.appendChild(para("l-p", `The arrows at the top step back and forward through your changes, one at a time: a knob move, a switch, a typed value, an Auto-Master run, Apply, a snapshot. Point at an arrow to see what it will undo. ${IS_MAC ? "⌘Z undoes, ⇧⌘Z redoes" : "Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes"} (click inside Masterology first, or {daw} takes the keys). The last 100 steps are kept while Masterology's window is closed, but not when you close the project. Automation from {daw} is never in the list.`));
}

const AM_MODULES = [["ul", "UNLIMITER"], ["eq", "TONAL EQ"], ["stem", "STEM EQ"], ["ts", "TONE STEADY"], ["rt", "RESONANCE TAMER"], ["bass", "BASS CONTROL"], ["glue", "GLUE"], ["dyn", "DYNAMICS"], ["img", "STEREO IMAGER"], ["sat", "SATURATION"], ["clip", "SOFT CLIPPER"], ["max", "MAXIMIZER"]];
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
    body.appendChild(tip(r.tipTitle || "TRY AGAIN", r.tip || "Press Auto-Master, then play your song from the start. It counts only music, and needs at least 12 seconds of it.", true));
    return;
  }
  body.appendChild(head("AUTO-MASTER REPORT"));
  const meta = para("l-meta", `${r.genre} · ${r.intensity} · ${r.target}`);
  body.appendChild(meta);
  if (r.listened) body.appendChild(para("l-p l-listened", r.listened));
  body.appendChild(para("l-p", r.summary));
  const L = r.listen;
  const jumped = !!(L && L.jumps > 0);
  if (L && L.heard > 0 && L.songLufs > -70)
    body.appendChild(item(jumped ? "What it heard" : "Whole song", `${fmtNum(L.songLufs)} LUFS`,
      (jumped ? `Everything it heard as it arrived, repeats included (loudness range ${fmtNum(L.songLra)} LU): playback jumped or looped while it listened, so this isn't your song played once through.`
              : `Your whole song as it arrives (loudness range ${fmtNum(L.songLra)} LU).`)
      + ` Its loudest part measures ${fmtNum(L.loudLufs)} LUFS: the master is set for that part, so the rest of the song sits under it as you mixed it.`
      + (!r.toneTarget ? "" : Math.abs(r.toneTarget - L.heard) < 1 ? ` Tone Steady learned your song's overall tone from these ${fmtTime(r.toneTarget)}.`
                                                                   : ` Tone Steady keeps the overall tone it learned from ${fmtTime(r.toneTarget)} of your song.`)));
  if (jumped)
    body.appendChild(tip("PLAYBACK JUMPED WHILE IT LISTENED", L.loudPieces > 1
        ? "The loudest 12 seconds it heard join two or more places on your timeline (a cycle loop, or the playhead moved), so the levels were set from audio that isn't one stretch of your song."
        : "A cycle loop or a jump of the playhead happened while it listened, so the whole-song figures count what it heard, not your song played once through.", !(L.loudPieces > 1),
      { need: L.loudPieces > 1 ? "Only if the joined parts weren't your loudest part (the hook or drop). Otherwise the settings fit." : "No: the loudest part was one stretch of your song, so the settings fit. Only the whole-song figures are off.",
        steps: ["In {daw}, {daw:loopOff}, so the song plays straight through.", "Click Auto-Master and play the song from the start to the end."] }));
  if (L && L.heard > 0 && L.heard < 30)
    body.appendChild(tip("PLAY THE WHOLE SONG NEXT TIME", `Auto-Master heard ${fmtTime(L.heard)} of your song, so it set the levels from that part.`, true,
      { need: "No, if that was the loudest part (the hook or drop). Otherwise the louder part will be limited harder than planned.",
        steps: ["Click Auto-Master and play the song from the start.", "Let it play to the end: it finds the loudest part by itself."] }));
  if (r.reference && r.mixStats && r.masterStats) {
    body.appendChild(head(`REFERENCE vs YOUR MIX vs MASTER`));
    body.appendChild(refCompare(r));
  }
  notesAsTips(body);
  for (const [id, label] of AM_MODULES) {
    const list = (r.reasons || []).filter((x) => x.module === id);
    if (!list.length) continue;
    body.appendChild(sep());
    body.appendChild(head(label));
    list.forEach((x) => body.appendChild(item(x.control, x.value, x.why)));
    if (id === "stem") { const b = stemSuggestButton(); if (b) body.appendChild(b); }
  }
  body.appendChild(tip("COMPARE IT", "Turn on MATCH, then press A for your original mix and B for the master: both play at the same loudness, so you hear only what the master changed. The undo arrow at the top puts every setting back. To compare two Auto-Master runs, keep each in a snapshot (S1, S2 at the top).", true));
}

// ---------------------------------------------------------------------------------------------
// Auto Analyze: after Auto-Master, Masterology keeps listening while the song plays. When it hears the hook
// again and the mix has changed, it works out what Auto-Master would change now and offers each module's
// edit here (APPLY or KEEP MINE, one undo step each). It never changes anything by itself.
let aa = null;                 // the plug-in's advice (getAutoAnalyze), or null
let aaSeenVersion = -1;
let aaSeenUpdate = "";         // the advice last announced with a toast
const aaReady = () => !!(amReport && amReport.ok);
async function fetchAutoAnalyze() {
  let text = "";
  try { text = await aaGet(); } catch (e) { text = ""; }
  try { aa = text ? JSON.parse(text) : null; } catch (e) { aa = null; }
  const edits = aa && aa.edits ? aa.edits.length : 0;
  if (aa && aa.updated && aa.updated !== aaSeenUpdate) {
    const first = aaSeenUpdate === "";
    aaSeenUpdate = aa.updated;
    if (!first && edits) toast(`Auto Analyze: your mix changed. ${edits === 1 ? "1 edit is" : `${edits} edits are`} ready in Learn.`);
  }
  updateAmButton();
  // The auto page follows every change; the module pages and the report only when the strip changes.
  const sig = `${M.aaOn}|${edits}`;
  if (learnOn && (learnView === "auto" || (["module", "report"].includes(learnView) && sig !== aaStripSig))) refreshLearn();
  aaStripSig = sig;
}
let aaStripSig = "";
function aaTime(iso) { return snapTime(iso); }
function aaHookText() {
  const L = amReport && amReport.listen;
  const spans = L && Array.isArray(L.hookSpans) ? L.hookSpans : [];
  if (spans.length) return spans.slice(0, 3).map(([a, b]) => `${fmtTime(a)}–${fmtTime(b)}`).join(", ");
  return L && L.fromHost && L.loudStart >= 0 ? `${fmtTime(L.loudStart)}–${fmtTime(L.loudEnd)}` : "";
}
// One line: what Auto Analyze is doing right now.
function aaStatusText() {
  const p = M.aaPause, heard = M.aaHeard || 0, hook = aaHookText();
  if (!M.aaOn) return "Off. Turn it on and Masterology keeps listening while your song plays: when your mix changes, it works out what Auto-Master would change and shows it here.";
  switch (p) {
    case "busy": return "Paused while Auto-Master listens and thinks.";
    case "settings": return "Paused: you changed the genre, intensity, target or reference since Auto-Master ran, so its plan doesn't apply any more.";
    case "stopped": return `Waiting for the song (it starts listening afresh after every stop). Press play: it checks the hook or drop${hook ? ` (${hook})` : ""}, the part Auto-Master set your master from.`;
    case "listening": return `Listening · ${fmtTime(heard)} heard. It checks once it has heard 20 seconds, including the hook or drop${hook ? ` (${hook})` : ""}.`;
    case "hook": return `Listening · ${fmtTime(heard)} heard. Play the hook or drop${hook ? ` (${hook})` : ""}: Auto-Master set your master from that part, so that's where Auto Analyze compares, like with like.`;
    case "checking": return "Checking the mix you just played (usually 5–15 seconds; longer at 96 kHz and up)…";
    case "cantJudge": return "It couldn't judge the last part it heard: it didn't sound like a full mix. Play the song with every track on.";
    default: return aa && aa.checked ? `Listening · last checked at ${aaTime(aa.checked)}.` : "Listening.";
  }
}
function aaToggleButton() {
  const b = document.createElement("button");
  b.type = "button";
  b.id = "btn-autoanalyze";
  const live = M.aaOn && ["listening", "hook", "checking", ""].includes(M.aaPause);
  b.className = "pill aa-toggle " + (M.aaOn ? "pill-red" : "pill-blue") + (live ? " live" : "");
  b.setAttribute("aria-pressed", String(!!M.aaOn));
  b.innerHTML = `<i class="aa-lamp"></i><span>AUTO ANALYZE${M.aaOn ? " ON" : ""}</span>`;
  b.title = M.aaOn ? "Auto Analyze is on: click to stop it listening" : "Keep listening while the song plays and offer edits when the mix changes";
  b.addEventListener("click", async () => {
    const on = !M.aaOn;
    M.aaOn = on;
    try { await aaSet(on); } catch (e) { /* the next frame says what the plug-in has */ }
    toast(on ? "Auto Analyze is on. Play your song: when your mix changes, the edits it needs show up in Learn." : "Auto Analyze is off.");
    if (on) learnView = "auto";
    renderLearn();
    updateAmButton();
  });
  return b;
}
// At the top of Learn (module pages and the report), once Auto-Master has run: the switch, and the edits on offer.
function addAutoAnalyzeStrip(body) {
  if (!aaReady()) return;
  const strip = document.createElement("div");
  strip.className = "aa-strip";
  strip.appendChild(aaToggleButton());
  const n = M.aaOn && aa && aa.edits ? aa.edits.length : 0;
  const link = document.createElement("button");
  link.type = "button";
  link.className = "l-link aa-see" + (n ? " has" : "");
  link.textContent = n ? `${n} NEW EDIT${n === 1 ? "" : "S"} · SEE` : M.aaOn ? "WHAT IT HEARS →" : "HOW IT WORKS →";
  link.addEventListener("click", () => { learnView = "auto"; renderLearn(); });
  strip.appendChild(link);
  body.prepend(strip);
}
// The mix since Auto-Master listened, in words.
function aaMixText(mix) {
  if (!mix) return "";
  const parts = [];
  if (typeof mix.loudDb === "number" && Math.abs(mix.loudDb) >= 0.5)
    parts.push(`arrives ${fmtNum(Math.abs(mix.loudDb), 1)} dB ${mix.loudDb > 0 ? "louder" : "quieter"} on the hook`);
  if (Array.isArray(mix.bandDb)) {
    const moved = mix.bandDb.map((d, b) => [d, EQ_BANDS[b].name.toLowerCase()]).filter(([d]) => Math.abs(d) >= 1);
    if (moved.length) parts.push("its tone moved: " + moved.map(([d, n]) => `${d > 0 ? "more" : "less"} ${n} (${fmtSigned(d)})`).join(", "));
  }
  return parts.length ? `Since Auto-Master listened, your mix ${parts.join(", and ")}.` : "Since Auto-Master listened, your mix's level and tone on the hook have stayed about the same.";
}
function aaRun(fn, module, done) {
  return async () => {
    let ok = false;
    try { ok = await fn(module); } catch (e) { ok = false; }
    if (ok) { toast(done); refreshAllCells(); userEdited(); }
    // The plug-in sends a new version with the frame; fetch at once so the card goes away now.
    fetchAutoAnalyze();
  };
}
function aaEditCard(e, reasons) {
  const d = document.createElement("div");
  d.className = "l-tip ok aa-card";
  d.appendChild(head(e.title.toUpperCase()));
  for (const c of e.changes) {
    const row = document.createElement("div");
    row.className = "aa-change";
    const name = document.createElement("span");
    name.className = "aa-name";
    name.textContent = c.name;
    const val = document.createElement("span");
    val.className = "mono aa-val";
    val.textContent = `${c.fromText.replace(/^-/, MINUS)} → ${c.toText.replace(/^-/, MINUS)}`;
    row.append(name, val);
    d.appendChild(row);
  }
  for (const r of reasons.slice(0, 2)) d.appendChild(para("l-p aa-why", `${r.control} ${r.value}: ${r.why}`));
  const row = document.createElement("div");
  row.className = "l-actions";
  const apply = document.createElement("button");
  apply.type = "button";
  apply.className = "pill pill-red stem-apply";
  apply.textContent = "APPLY";
  apply.addEventListener("click", aaRun(aaApply, e.module, `${e.title}: Auto Analyze's edit applied. Compare with MATCH; the undo arrow puts it back.`));
  const keep = document.createElement("button");
  keep.type = "button";
  keep.className = "pill pill-blue stem-apply";
  keep.textContent = "KEEP MINE";
  keep.title = "Leave this module as it is: the same advice won't come back for this mix";
  keep.addEventListener("click", aaRun(aaKeep, e.module, `${e.title} stays as it is.`));
  row.append(apply, keep);
  d.appendChild(row);
  return d;
}
function renderAutoLearn(body) {
  if (aaReady() || amReport) {
    const link = document.createElement("button");
    link.type = "button";
    link.className = "l-link";
    link.textContent = "← AUTO-MASTER REPORT";
    link.addEventListener("click", () => { learnView = amReport ? "report" : "module"; renderLearn(); });
    body.appendChild(link);
  }
  body.appendChild(head("AUTO ANALYZE"));
  if (!aaReady()) {
    body.appendChild(para("l-p", "Run Auto-Master first: Auto Analyze watches your mix against the plan Auto-Master made."));
    return;
  }
  const top = document.createElement("div");
  top.className = "aa-strip";
  top.appendChild(aaToggleButton());
  body.appendChild(top);
  body.appendChild(para("l-p aa-status", aaStatusText()));
  if (M.aaOn && M.aaPause === "settings")
    body.appendChild(tip("RUN AUTO-MASTER AGAIN", "The genre, intensity, target or reference changed since Auto-Master ran. Its plan was made for the old choice, so Auto Analyze waits.", true,
      { need: "Yes, if you want the master to follow the new choice.",
        steps: ["Click Auto-Master and play your whole song from the start.", "Auto Analyze carries on with the new plan by itself."] }));

  const live = !["settings", "noReport", "busy"].includes(M.aaPause);
  const edits = (M.aaOn && live && aa && aa.edits) || [];
  const mix = aa && aa.mix;
  if (M.aaOn && live && aa && aa.hasCheck) {
    body.appendChild(para("l-p", aaMixText(mix)));
    if (edits.length) {
      body.appendChild(sep());
      body.appendChild(head(`NEW EDITS${aa.updated ? ` · ${aaTime(aa.updated)}` : ""}`));
      body.appendChild(para("l-p", "What Auto-Master would change for your mix now. Your own knob moves are kept. Nothing changes until you press APPLY."));
      const reasons = (mix && mix.reasons) || [];
      for (const e of edits) body.appendChild(aaEditCard(e, reasons.filter((r) => r.module === e.module)));
      if (edits.length > 1) {
        const row = document.createElement("div");
        row.className = "l-actions";
        const all = document.createElement("button");
        all.type = "button";
        all.className = "pill pill-red stem-apply";
        all.textContent = "APPLY ALL";
        all.addEventListener("click", aaRun(aaApply, "all", "Every Auto Analyze edit applied, as one undo step. Compare with MATCH."));
        const keep = document.createElement("button");
        keep.type = "button";
        keep.className = "pill pill-blue stem-apply";
        keep.textContent = "KEEP ALL MINE";
        keep.addEventListener("click", aaRun(aaKeep, "all", "Your settings stay as they are."));
        row.append(all, keep);
        body.appendChild(row);
      }
    } else {
      body.appendChild(tip("YOUR SETTINGS STILL FIT", `Checked at ${aaTime(aa.checked)}: for your mix as it is now, Auto-Master would make the same choices${amReport.values ? " (or ones you already applied or kept)" : ""}. Nothing to do.`, true));
    }
    for (const raw of (mix && mix.notes) || []) {
      const lines = raw.split("\n"), n = lines[0], cut = n.indexOf(":");
      const title = cut > 0 && cut < 24 ? n.slice(0, cut) : "HEADS UP";
      const text = cut > 0 && cut < 24 ? n.slice(cut + 1).trim() : n;
      const need = lines.find((l) => l.startsWith("NEED:"));
      const steps = lines.filter((l) => l.startsWith("STEP:")).map((l) => l.slice(5).trim());
      body.appendChild(tip(title, text.charAt(0).toUpperCase() + text.slice(1), false, need ? { need: need.slice(5).trim(), steps } : undefined));
    }
  }
  body.appendChild(tip("HOW IT WORKS", "While your song plays, Auto Analyze listens to the mix as it reaches Masterology. When it hears the hook or drop again (the part Auto-Master set your master from), it runs Auto-Master's thinking on the mix as it is now. If you changed the mix (louder, more low end, a new vocal level) it shows what Auto-Master would change, module by module. It starts listening again after every stop, so it always judges the mix as it is now. It only listens while this window is open, and it never moves a control by itself.", true));
}

// Current value of a parameter by id, in the same units the report stores.
function paramValue(id) {
  if (id.startsWith("eqGain")) return eqGainAt(Number(id.slice(6)) - 1);
  if (id.startsWith("eqFreq")) return eqFreqAt(Number(id.slice(6)) - 1);
  if (id.startsWith("eqMode")) return eqModeAt(Number(id.slice(6)) - 1);
  const map = { drive: P.drive, inTrim: P.inTrim, ceiling: P.ceiling, release: P.release, clipLevel: P.clipLevel, clipSoftness: P.clipSoftness,
                bassMono: P.bassMono, bassTame: P.bassTame, bassPunch: P.bassPunch,
                imgLow: P.imgLow, imgMid: P.imgMid, imgHigh: P.imgHigh, satDrive: P.satDrive, satMix: P.satMix, maxBassFirst: P.maxBassFirst,
                glueThr: P.glueThr, glueRatio: P.glueRatio, glueAtk: P.glueAtk, glueRel: P.glueRel, glueSc: P.glueSc, glueMix: P.glueMix, glueGain: P.glueGain,
                ulAmount: P.ulAmount, ulLength: P.ulLength, tsAmount: P.tsAmount, tsRange: P.tsRange, rtAmount: P.rtAmount, rtSens: P.rtSens, rtRelease: P.rtRelease,
                stemVoc: P.stemGain[0], stemDrums: P.stemGain[1], stemBass: P.stemGain[2], stemOther: P.stemGain[3] };
  const band = /^dyn(X|Thr|Ratio|Atk|Rel|Gain|Impact|Sustain)(\d)$/.exec(id);
  if (band) return P["dyn" + band[1]][Number(band[2]) - 1].getScaledValue();
  if (id === "satMode") return satModeIndex();
  if (id === "satFocus") return satFocusIndex();
  if (id === "satChannels") return satChannelsAt();
  if (id === "glueChar") return glueCharAt();
  if (id === "dynChannels") return dynChannelsAt();
  if (id === "tsSpeed") return comboIndex(P.tsSpeed);
  if (id === "ulFocus") return comboIndex(P.ulFocus);
  if (id === "rtFocus") return comboIndex(P.rtFocus);
  if (map[id]) return map[id].getScaledValue();
  const toggles = { eqOn: P.eqOn, bassOn: P.bassOn, clipOn: P.clipOn, adaptive: P.adaptive, bypass: P.bypass,
                    dynOn: P.dynOn, dynAdaptive: P.dynAdaptive, imgOn: P.imgOn, satOn: P.satOn, tsOn: P.tsOn, rtOn: P.rtOn, ulOn: P.ulOn,
                    glueOn: P.glueOn, glueAuto: P.glueAuto };
  if (toggles[id]) return toggles[id].getValue() ? 1 : 0;
  if (id === "clipOversampling") return osIndex();
  return null;
}
const MODULE_PARAMS = { ul: /^ul/, stem: /^stem/, ts: /^ts/, rt: /^rt/, eq: /^eq/, bass: /^bass/, glue: /^glue/, dyn: /^dyn/, img: /^img/, sat: /^sat/, clip: /^clip/, max: /^(drive|ceiling|release|adaptive|inTrim|maxBassFirst)$/ };
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
    frag.appendChild(para("l-changed", "You've changed this module since Auto-Master set it, so its reasons no longer describe what you hear. The undo arrow (top) steps back through your changes to Auto-Master's settings."));
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
// Live changes (a knob turning, the host's automation): Learn follows at most 10 times a second.
let learnQueued = false, learnSoftAt = 0;
function scheduleLearn() {
  if (learnQueued) return;
  learnQueued = true;
  setTimeout(() => requestAnimationFrame(() => { learnQueued = false; learnSoftAt = performance.now(); refreshLearn(); }),
             Math.max(0, 100 - (performance.now() - learnSoftAt)));
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
                   : selected === "glue" ? (!P.glueOn.getValue() ? "OFF" : glueIsNeutral() ? "1:1" : glueRatioAt() <= 1.005 ? `GAIN ${fmtSigned(glueGainAt())}`
                                            : `GR ${(M.glueGr || 0) > -0.05 ? "0.0" : fmtNum(M.glueGr)} dB`)
                   : selected === "sat" ? (!P.satOn.getValue() ? "OFF" : satIsNeutral() ? "CLEAN" : SAT_MODES[satModeIndex()].toUpperCase() + SAT_FOCUS_TAG[satFocusIndex()])
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
    : `${[0, 1, 2, 3].filter(dynBandActive).length} band${[0, 1, 2, 3].filter(dynBandActive).length > 1 ? "s" : ""} \u00b7 ${dynWorst() > -0.05 ? "0.0" : fmtNum(dynWorst())} dB${["", " \u00b7 mid", " \u00b7 side"][dynChannelsAt()]}`;
  if (selected === "dyn") dynPicker.refresh();
  const rg = $("read-glue");
  if (rg) rg.textContent = P.bypass.getValue() ? "Bypassed" : !P.glueOn.getValue() ? "Off" : glueIsNeutral() ? neutralText("glue")
    : glueRatioAt() <= 1.005 ? `Level ${fmtSigned(glueGainAt())}` : `${GLUE_CHARS[glueCharAt()]} ${glueRatioAt().toFixed(1)}:1 \u00b7 ${(M.glueGr || 0) > -0.05 ? "0.0" : fmtNum(M.glueGr)} dB`;
  const gf = $("gluemeter-fill");
  if (gf) {
    const active = P.glueOn.getValue() && !glueIsNeutral() && !P.bypass.getValue();
    const g = active ? Math.min(0, M.glueGr || 0) : 0;
    gf.style.width = (clamp(-g / 12, 0, 1) * 100).toFixed(1) + "%";
    $("gluemeter-val").textContent = !active ? "off" : g > -0.05 ? "0.0 dB" : `${fmtNum(g, 1)} dB`;
  }
  const rul = $("read-ul");
  if (rul) rul.textContent = P.bypass.getValue() ? "Bypassed" : !P.ulOn.getValue() ? "Off" : ulIsNeutral() ? neutralText("ul")
    : `${Math.round(ulAmountAt())} % \u00b7 ${UL_FOCUS[comboIndex(P.ulFocus)]}`;
  const usf = $("ulsquash-fill");
  if (usf) {
    const sq = M.ulSquash || 0;
    usf.style.width = (sq ? clamp((16 - sq) / 10, 0, 1) * 100 : 0).toFixed(1) + "%";
    $("ulsquash-val").textContent = !sq ? "listening" : `${fmtNum(sq)} dB \u00b7 ${squashWord(sq)}`;
    const v = ulIsNeutral() || P.bypass.getValue() ? 0 : M.ulBoost || 0;
    $("ulmeter-fill").style.width = (clamp(v / 10, 0, 1) * 100).toFixed(1) + "%";
    $("ulmeter-val").textContent = v < 0.3 ? "resting" : `+${v.toFixed(1)} dB`;
  }
  const rts = $("read-ts");
  if (rts) rts.textContent = P.bypass.getValue() ? "Bypassed" : !P.tsOn.getValue() ? "Off" : tsIsNeutral() ? neutralText("ts")
    : `${Math.round(tsAmountAt())} % \u00b7 ${MINUS}${P.tsRange.getScaledValue().toFixed(1)} dB${M.tsTarget ? " \u00b7 song" : ""}`;
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
  const skv = $("stemkeep-val");
  if (skv) skv.textContent = stemKeepText();
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
  if (rs) rs.textContent = P.bypass.getValue() ? "Bypassed" : !P.satOn.getValue() ? "Off" : satIsNeutral() ? neutralText("sat") : `${SAT_MODES[satModeIndex()]} +${satDriveAt().toFixed(1)} dB${SAT_FOCUS_TAG[satFocusIndex()].toLowerCase()}${["", " \u00b7 mid", " \u00b7 side"][satChannelsAt()]}`;
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
// A frame made safe to use. JUCE sends a NaN or infinite number as null, and one odd field mustn't stop the
// whole window: a number that isn't finite keeps its last value, a list keeps its length (a spectrum point that
// isn't finite sits at the bottom), and the shared choices and Mixology's hand-off are checked field by field.
const str = (v, d = "") => (typeof v === "string" ? v : d);
const num = (v, d) => (Number.isFinite(v) ? v : d);
function cleanFrame(f) {
  const out = {};
  if (!f || typeof f !== "object") return out;
  for (const [k, v] of Object.entries(f)) {
    const was = M[k];
    if (typeof was === "number") { if (Number.isFinite(v)) out[k] = v; }
    else if (typeof was === "boolean") out[k] = !!v;
    else if (typeof was === "string") { if (typeof v === "string") out[k] = v; }
    else if (Array.isArray(was) && typeof was[0] === "number") { if (Array.isArray(v)) out[k] = was.map((old, n) => num(v[n], old)); }
  }
  if (!(out.sr > 0)) delete out.sr;
  if (Array.isArray(f.snaps)) out.snaps = f.snaps.map((x) => (x && typeof x === "object" ? { filled: !!x.filled, time: str(x.time), note: str(x.note) } : { filled: false, time: "", note: "" }));
  for (const k of ["mix", "master"]) if (Array.isArray(f[k])) out[k] = f[k].map((v) => num(v, -120));
  if (f.songStyle && typeof f.songStyle === "object")
    out.songStyle = { key: str(f.songStyle.key), name: str(f.songStyle.name), from: num(f.songStyle.from, 0), version: num(f.songStyle.version, 0) };
  if (f.daw && typeof f.daw === "object")
    out.daw = { key: str(f.daw.key), picked: !!f.daw.picked, detected: str(f.daw.detected), from: num(f.daw.from, 0) };
  if ("fromMixology" in f) {
    const h = f.fromMixology;
    out.fromMixology = h && typeof h === "object" ? {
      style: str(h.style), genre: Number.isInteger(h.genre) && h.genre >= 0 && h.genre < GENRES.length ? h.genre : -1,
      peak: num(h.peak, -100), lufs: num(h.lufs, -100), analysed: !!h.analysed, mono: !!h.mono, limited: !!h.limited,
      tone: [0, 1, 2, 3].map((n) => num(Array.isArray(h.tone) ? h.tone[n] : 0, 0)),
    } : null;
  }
  return out;
}
// Each part of the handler on its own: one that throws is reported once (see reportOnce) and the rest carry on.
function part(name, fn) { try { fn(); } catch (e) { reportOnce(name, e); } }
window.__JUCE__.backend.addEventListener("polisherMeters", (raw) => {
  const frame = cleanFrame(raw);
  const srChanged = frame.sr && frame.sr !== M.sr;
  const wasAm = M.amState, wasTarget = M.tsTarget, wasFail = M.tsLearnFail, wasFar = M.tsFar, wasMixology = mixologyKey();
  const wasAa = M.aaOn, wasAaHeard = M.aaHeard;
  Object.assign(M, frame);
  part("suite", () => {
    if (mixologyKey() !== wasMixology && learnView === "module") refreshLearn();   // Mixology came, went, or changed
    // The DAW: found from the host, or picked in any of the three. Learn says it again in that DAW's words.
    if (frame.daw && (frame.daw.key !== DAW.key || !!frame.daw.picked !== DAW.picked || frame.daw.detected !== DAW.detected)) {
      Object.assign(DAW, { key: frame.daw.key || "other", picked: !!frame.daw.picked, detected: frame.daw.detected || "other", from: frame.daw.from || 0 });
      fillDawSelect();
      refreshLearn();
    }
    // The song's style: this project's, or one just picked in Mixology or Voxology.
    if (frame.songStyle && frame.songStyle.version !== SONG.version) {
      const first = SONG.version === -1, ss = frame.songStyle;
      SONG.version = ss.version;
      if (ss.key !== SONG.key) {
        SONG.key = ss.key;
        SONG.name = ss.name;
        refreshAmChoices();
        if (!first && ss.key && ss.from !== 3)
          toast(`Style: ${ss.name}, picked in ${ss.from === 2 ? "Mixology" : "Voxology"}. Auto-Master masters it as ${GENRES[amGenreFor(ss.key)]}.`);
      }
    }
  });
  part("auto-master", () => {
    if (M.amState === 2 && wasAm !== 2) amThinkSince = performance.now();   // the analysis started: time it
    else if (M.amState !== 2) amThinkSince = 0;
    watchListening(wasAm, wasTarget, wasFail, wasFar);
    if (srChanged) { drawEq(); fetchGenreCurve(); }
  });
  part("undo and snapshots", () => { refreshHistory(); refreshSnapshots(); });
  part("auto analyze", () => {
    if (M.aaVersion !== aaSeenVersion || (!!frame.aaOn) !== (!!wasAa)) {
      aaSeenVersion = M.aaVersion;
      fetchAutoAnalyze();
    } else if (learnOn && learnView === "auto" && M.aaPause !== "" && Math.floor(M.aaHeard || 0) !== Math.floor(wasAaHeard || 0)) refreshLearn();   // the heard count moves on
  });
  part("report and reference", () => {
    if (M.amReport !== amSeenVersion) {
      const first = amSeenVersion === -1;
      amSeenVersion = M.amReport;
      fetchReport(!first);
    }
    if (wasAm !== M.amState || M.amState === 1 || M.amState === 2) updateAmButton();
    if (M.refVersion !== refSeenVersion) { refSeenVersion = M.refVersion; fetchReference(); }
    else if (M.refState === 1) refreshRefPill();
    if (P.levelMatch.getValue()) refreshMatch();
  });
  part("listening aids", () => {
    // Listening aids: the plug-in's state wins (it switches them off itself, e.g. for a new window), except
    // for a moment after a click here, while the click is on its way.
    if (frame.listen !== undefined && performance.now() - MON.sentAt > 500
        && (frame.listen !== MON.listen || !!frame.mono !== MON.mono || !!frame.phone !== MON.phone)) {
      MON.listen = frame.listen; MON.mono = !!frame.mono; MON.phone = !!frame.phone;
      refreshMonitor();
    } else if (MON.listen) $("monitor-level").textContent = monitorLevelText();
    watchLouderPart();
  });
  part("meters", () => {
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
  });
  part("readouts", updateLiveReadouts);
  part("history", () => {
    histFrame(performance.now());
    if (histView) { drawHistory(); if (learnOn && learnView === "history") scheduleHistLearn(); }
  });
  part("spectrum", () => {
    $("sp-mix").setAttribute("d", spectrumPath(M.mix));
    drawRefCurve();
    drawGenreCurve();
    const master = spectrumPath(M.master);
    $("sp-master").setAttribute("d", master);
    $("sp-master-glow").setAttribute("d", master);
  });
});
// Every module page has live numbers: once a second Learn checks them (nothing changes on screen unless one did).
setInterval(() => { if (learnOn && learnView === "module") refreshLearn(); }, 1000);
// The history's Learn page follows the graph, but only as often as a new point can change it.
let histLearnAt = 0;
function scheduleHistLearn() { const now = performance.now(); if (now - histLearnAt > 1000) { histLearnAt = now; refreshLearn(); } }

// ---------------------------------------------------------------------------------------------
buildSpectrumGrid();
buildHistoryGrid();
buildMeters();
renderChain();
renderModule();
setLearn(true);
refreshAmChoices();
updateAmButton();
refreshHistory();
refreshSnapshots();
refreshMatch();
refreshMonitor();
onParamsChanged();
setHistView(histView);
fetchGenreCurve();
