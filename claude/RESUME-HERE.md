# Polisher — resume here (updated 2026-10-10)

Read this first in a new chat. Code: github.com/DK7602/polisher. Newest work: branch
`claude/eloquent-einstein-sscadx` (v0.14.0, 2026-10-10 session below; it fast-forwarded from
`claude/gracious-wright-tgs0no`, which is older). Merge to `main` when ready. Every push builds Mac + Windows; download Polisher-Windows from the
latest green Actions run.
CI: pushes build Windows only (private repo minutes; macOS counts 10x). Mac build on request:
Actions -> Build & Test -> Run workflow ("Also build macOS" ticked), or ask Claude to trigger it.

## 2026-10-10 session (user asked: do all five of the audit follow-ups, one after the other, overnight)
1. Unlimiter header button fixed: it toggled the GLOBAL bypass (A) instead of ulOn. app.js now has one table
   HEAD_POWER of every module's own switch (the Maximizer's button stays the global bypass); ulOn host changes
   refresh it. tests/ui/ui_smoke.mjs (Playwright, mock.js) checks every module's switch flips only its param.
2. Stem EQ model v3 (v0.14.1): all of MUSDB (100 train / 50 held out, tools/stem/fetch_musdb.py), per-stem
   augmentation, synthetic trap 808s (synth808.py: standard + distorted/octave + clean sub) on the bass stem,
   EMA. On 40 untouched songs: plain -14.25 (v2 -14.28, static -13.88: v2 ALREADY beat static there; the old
   "= static" was 6 songs); with 808s v3 beats v2 by 3.3-4.8 dB on the Bass knob and beats static on every
   set (v2 lost to static whenever an 808 played). Full table + recipe: tools/stem/README.md "Model v3".
   The user's 13 songs were NOT available (scratchpad gone): v3 never saw them. ASK the user to try Stem EQ
   Bass on a trap song with 808s (v3 vs v2 = v0.14.0 build). Vocal readings match v2 (-0.07 dB), so the
   genre vocal norms stay. Ideas not tried: own songs back in (teacher-labelled), bigger model.
3. Release polish: README at v0.14 + requirements + validation section; COMPANY_WEBSITE (example.com) removed;
   basic fallback editor shows a WebView2 notice + GET WEBVIEW2 button (Windows); manual install note.
4. Validation: tests/test_validation.cpp + tests/ReferenceTruePeak.h (independent 16x meter: Maximizer holds
   its ceiling on tones to 20 kHz; on hard-clipped audio it sits at the ceiling by BS.1770 and within ~0.9 dB on
   a wideband reconstruction -> why -1 dBTP is the default), more EBU 3341/3342 cases. tests/plugin/
   (polisher_plugin_tests, juce_add_console_app, POLISHER_HEADLESS): block sizes 1..4096 + random, offline =
   realtime, latency at 44.1-192k and in bypass, sample-rate change = fresh, no operator-new on the audio
   thread, state round trip (and an older state without a new control -> its default: JUCE does this),
   automation torture (true peak), double = float, Input Trim. FOUND + FIXED: prepareToPlay set module params
   after prepare -> Bass/Dynamics/Imager/spectral faded in from neutral over ~0.3 s on load/export start; now
   params go in first (and TonalEQ only arms "land instantly" when it had no params yet). ASan+UBSan: engine
   tests clean (CI job, PRs/manual only to save minutes). tools/blindtest = PolisherBlindTest.exe (in the
   Windows artifact, BlindTest/): drag 2-8 exported masters on it -> loudness-matched (down only, no clipping),
   shuffled Sample A/B/..., Score sheet + Answer key. Manual: "Blind test" section.
5. Input Trim (v0.14.0, param inTrim -24..0 dB, hint 4): at the very start of the chain (after input meter,
   spectrum tap, Auto-Master capture; off in A). Auto-Master: if Drive 0 still overshoots the target by more
   than 0.3 LU, sets trim (0.5 dB past the minimum) and re-decides everything (<= 2 refinements); report quotes
   the mix as it arrives. Knob on the Maximizer screen + Learn; manual updated.
Two fresh-eyes review agents ran (items 1/3/4 and 5); all real findings fixed (plug-in test app crashed on exit
from a static JUCE initialiser; Windows MSVC compile error in BlindTest; mono files 3 dB loud in BlindTest; false
"-24 dB limit" text; DIMINISHING RETURNS counting the trim). One claim (stale trim from old presets) was
checked and is not a bug in JUCE 8.0.15 -> pinned by a test instead.
CI NOTE: from the 6722dd0 push on, GitHub Actions jobs fail in 2 s without a runner (runner_id 0, no log),
also on re-run: almost certainly the private repo's Actions minutes / spending limit. Everything up to
6417e09 was green on Windows incl. pluginval 10 + VST3 validator; later commits verified locally (Linux).

## Status: v0.13.0 — eleven modules (Unlimiter new, slot 01); Auto-Master sets all but Stem EQ (suggests vocals)
Chain: Unlimiter -> Tonal EQ -> Stem EQ / Tone Steady / Resonance Tamer -> Bass Control -> Dynamics -> Stereo Imager -> Saturation -> Drive -> Soft Clipper
-> Maximizer (+ MATCH gain at the very end). Engine tests: 73 cases
(`cmake -S . -B build-tests -DPOLISHER_BUILD_PLUGIN=OFF && cmake --build build-tests && ./build-tests/tests/polisher_tests`).

What landed 2026-09-30 / 10-01:
- Auto-Master refuses a single instrument (Polisher on a track instead of Stereo Out) and says how to fix.
- MATCH never boosts; turns the louder side down; gives a clipping original (A) headroom.
- Dynamics (4-band) and Saturation (tape / tube / clip, 4x oversampled) in the plug-in with UI + Learn.
- Saturation Drive calibrated on the user's song; harmonics meter averages energy (honest reading).
- Auto-Master sets Dynamics (per-band glue, thresholds found by running the compressor), Stereo Imager
  (opens narrow highs only, mono-safe) and Saturation (mode by genre, Drive by harmonics target;
  Trap / Balanced target -25 dB = the user's favourite, Tube +12 / Mix 50 %).
- "Checked · kept" when Auto-Master looked at a module and chose no change.
- Learn rule: every warning carries "Do I need to fix it?" + numbered "How" steps; "No" answers show
  calm (blue), not red. Auto-Master notes use NEED: / STEP: lines; a test enforces it.

## User manual (2026-10-09) — STANDING RULE: keep it in step with the plug-in
Source: docs/manual/polisher-manual.md (plain language, ranges taken from the code). Bundled into the plug-in
(Quick Help -> OPEN THE FULL MANUAL; rendered offline by a small Markdown reader in app.js). Shared copy:
Claude Docs page https://claude.ai/code/artifact/3bf99dd7-1111-4be3-884d-d787ae49506c (update both).
Whenever a control, range, default, module, Auto-Master rule or UI behaviour changes, update the manual in the
same commit. The build stops if the manual's "Version x.y.z " line doesn't match the CMake project version.

## The user
Mastering novice, Cubase on Windows, headphones. Tests by screen recording (audio via headphones held
to the mic, or no audio) — trust the plug-in's own meters over recorded audio. Wants momentum
("you make the best choice"), plain language, honest assessments, and a plan with every warning.
Their test song: Trap / Hip-Hop, mix pre-gained -6 dB into Stereo Out (input about -17 LUFS,
peaks about -1 dBTP); Auto-Master lands about -9.7 LUFS (stops short of -9 to protect drum peaks).

## Quality bar (standing)
Measured (tests), clean internals (double precision, oversampled nonlinearities, no audio-thread
allocation), fresh-eyes review by a separate agent before shipping anything significant, heard
(user test with MATCH), and later proven in blind loudness-matched shoot-outs vs Ozone 12 and others.

## Stem EQ (requested 2026-10-01; UI slot in v0.10.6; model work in progress)
Module 02 in the chain (after Tonal EQ): per-stem EQ for vocals / drums / bass / other.
Spike done: HT-Demucs splits the user's song well (user: stems good, a little drum bleed in "other");
too heavy for live use (needs ~8 s chunks, 2-4 cores). Decided design (see tools/stem/README.md):
small band-share model (conv + GRU, ~250k params, ~40 ms latency, plain C++ inference, no ONNX)
distilled from HT-Demucs. Oracle ceiling is good (+/-6 dB moves within about -20 dB of ideal).
Training pipeline (tools/stem: common.py, prep.py, model.py, train.py) works; smoke test on the
user's one song: loss 0.033 -> 0.013 in 600 steps.
Data (user chose "Both"): (a) MUSDB18-HQ from HF dataset danjacobellis/musdb18HQ (27 GB parquet,
non-commercial licence: fine while Polisher is free; keep an own-songs-only model if it is ever
sold); (b) 20-40 of the user's own songs, teacher-labelled (clean licence).
2026-10-01 (2nd session): network reaches us.aws.cdn.hf.co; htdemucs_ft downloads and is now the
default teacher. tools/stem/musdb.py unpacks shards (one row = one stem file; songs can straddle
shards; test split -> val_*). Subset in progress: train shards 14-17,21,22,27,28,30,32 + validation
12,13,16 (~4.5 GB). Session data is ephemeral: re-download in a new session.
User uploaded 13 own songs (masters, trap/hip-hop; Gallas held out as val_; Jay + Mom have no drums).
808 rule (prep.low_end_to_bass, teacher songs only): below 120 Hz, vocals/other -> bass, low hits
>= 0.3 s and sustain under kicks -> bass, short hits stay drums. User checked Bubba by ear: correct.
First real run (112 songs, 12 own at 35 % of draws, 20k steps, 87 min): evaluate.py (+/-6 dB move
error vs ideal, dB, lower is better), 7 held-out songs avg: static EQ (true song-average shares)
-14.5, model -14.4, oracle -20.4.  Gallas: static -13.8, model -15.1, oracle -18.6.  Training songs:
static -13.7, model -15.6, oracle -17.9 -> the model memorises songs; it does not yet track the
time-varying part on new songs.  val-musdb best at ~4k steps.
Run 2 (remix augmentation from per-stem mid/side energies; container restarted at step 10.6k, best
checkpoint step 7.6k): 7 held-out avg static -15.0, v1 -14.4, v2 -15.2, oracle -20.4; Gallas static
-13.8, v1 -15.2, v2 -15.2, oracle -18.6.  Val loss 0.152 -> 0.125 but moves only ~0.8 dB better:
still about a best-possible static EQ.  Likely limit: input is only 64 band energies (no harmonic
detail to tell a voice from a guitar in the same band).  Next: richer input (per-bin log spectrum,
conv over frequency), keep 64 output bands.  Teacher stems of the user's 13 songs are saved
(KEEP_STEMS) in the session scratchpad; MUSDB needs re-download (~5 min) + prep in a new session.
Experiments 2026-10-02 (7 held-out avg / Gallas, dB; static -15.0 / -13.8, oracle -20.4 / -18.6):
  coarse, no remix, 6k: -14.5 / -14.7    detailed input (192 bands), no remix, 6k: -14.2 / -14.9
  coarse + remix, 6k: -15.1 / -14.9      bidirectional (whole-song context) + remix, 6k: -15.4 / -15.1
  -> input detail, look-ahead and remix each move < 1 dB; all near a static EQ.  Next check: score
  the HT-Demucs teacher itself with the same metric vs true MUSDB stems (realistic ceiling).
  Done (teacher_score.py, 6 MUSDB held-out): HT-Demucs -17.9 vs ours -15.3, static -15.2, oracle -20.7
  -> ~2.5 dB real room.  Smoothing our shares over time makes it worse (-13.9 at 0.4 s).
  Bigger model (HIDDEN=256 LAYERS=2, 1.1M params, 6k steps): better val loss (0.130) but move test
  unchanged (-15.2 / -15.2) -> the share loss rewards quiet bands the move test barely weighs.
  LOSS=move (trains on the move error itself): worse, -14.1 / -14.4; train reached -21..-27 dB vs
  val -14 -> it is a generalisation limit (~110 songs), not the loss, size, input or look-ahead.
  All variants sit at about -15 dB = the best static EQ that knew the true song-average shares.
  Listening test (render.py, Gallas 1:00-1:20, vocals +6): user heard our model's vocals at the
  same level as the ideal, no odd sounds.  DECISION: research done, ship model v2 (share loss, remix,
  HIDDEN 128, 1 layer, 247k params; best checkpoint of the 16k run, step 7.6k).  Keep a copy of
  stemnet_v2.pt in the repo (tools/stem/models/) - the scratchpad does not survive.
  C++ engine done (dsp/src/StemEQ.cpp + StemNetWeights.h from export.py; 7 tests, 80 total pass):
  C++ shares match PyTorch within 2.4e-6, exact delayed pass-through at 0 dB / off, equal gains =
  plain gain (3e-8), bass +6 lifts a 110 Hz tone 5.85 dB vs 0.76 dB for the rest, no clicks,
  latency 1792 (3584 at 88.2k+), ~3 % of one core at 48 kHz stereo.
  Plug-in done: params stemOn / stemVoc / stemDrums / stemBass / stemOther (version hint 2), chain
  slot after Tonal EQ, latency reported, Stem EQ screen + 'Model Hears' meter + Learn, Auto-Master
  hears the mix with the user's Stem EQ and keeps it.  Review agent: no high-severity bugs; fixed
  Learn update rate, silent meter, meter race (CAS), stale meter on editor reopen, params before
  prepare.  Left as known: 88.2k+ features ~3.5 dB hotter than training (NFFT 2048); mono input
  gives side features at the floor; Auto-Master applies Stem EQ before Tonal EQ (approximation).
  User test (Bubba, 2026-10-02): Vocals +3 / Bass +3 pulled forward as expected.  Their earlier hot
  input (+9.8 dBTP) came from their old chain (ZL EQ, Magneto II, Compressor, Velvet, Limiter) still on
  Stereo Out before Polisher: told them to bypass it for demos.
  Auto-Master Stem EQ suggestion (user asked): vocals only (drums / bass / other vary too much with the
  arrangement), measured by the model on what Auto-Master heard; genre usual levels (trap / R&B -3 dB
  from the user's 13 songs, median -2.8; others are starting points); suggest when > 1.5 dB low or
  > 2.5 dB loud, max +/-3 dB; shown in the report and Stem EQ Learn with an APPLY button; never applied.
  Quick Help has Stem EQ; version 0.11.0.
  Offered, not done: Auto-Master turning a too-hot mix down (Drive can't go below 0 dB, so a +10 dBTP
  input overshoots the target).
Next: download MUSDB (a subset first), prep, train ~20k steps, validate on held-out songs, try to
cut drum bleed in "other" (htdemucs_ft teacher failed to download here: retry with Full network),
then C++ engine + exact-passthrough tests, params/UI/Learn ("other" cuts touch drums a little),
Auto-Master hooks, review agent, user listening test.

## Next, when the user asks (paused 2026-10-01: "good for now until bugs or add-ons")
1. Pro EQ: mid/side per band, linear-phase mode, dynamic EQ bands (harsh 3-5 kHz, boomy lows).
2. Maximizer: transient-aware / multiband modes, codec preview, dither for 16-bit exports.
3. Match ref: load a reference song, match tone / loudness / width (replaces genre curves).
4. Blind shoot-out vs Ozone 12 Advanced and competitors; fix what loses.
5. Optional: Auto-Master opens highs a little for Trap even when already fairly wide (user may want).
Housekeeping: repo private (due after Oct 1), GitHub 2FA by Nov 4 2026, Actions Node 20 warnings,
"Polisher" trademark search, rights to AI-rendered art, code signing / notarization before release.

## Tone Steady + Resonance Tamer (v0.12.0, 2026-10-02)
User asked for Ozone-style Stabilizer / Clarity; our own names (Ozone trademarks). Both run inside the
Stem EQ STFT (no extra latency), chain slots 03 / 04. Resonance Tamer: peaks over the +/-0.3 oct mean
power (40 ms smoothed) above a Sensitivity threshold, cut by Amount; Focus Full / Boxiness / Harshness.
Tone Steady: 64-band short (Speed) vs 10 s long-term dB balance, 1 dB dead band, cuts <= Range, boosts
<= Range/2; silence / quiet frames never update the reference (review fix: stopped transport used to
flatten the mix for ~20 s). Auto-Master measures both at full strength and sets them (Resonance Tamer
off on Bubba / Schaf, 45-60 % Boxiness on Addie / Dean; Tone Steady 40 % on all four).
Sent the user A/B clips (Addie, Dean: off vs Auto-Master's settings, level-matched): B trims
400-650 Hz ~2 dB, 808 range < 0.5 dB. User's ears: B "lessened some of the high pitched
frequencies" (no complaint about 808 / vocal body). Measured: Addie B was -0.6 dB at 4-8 kHz and
-1.0 dB at 8-16 kHz (Tone Steady, not the tamer, whose Focus was Boxiness); Dean's top end ~0.
DONE 2026-10-02 (user: "go with your pick"): above ~2.5 kHz Tone Steady's short-term average is 2 s, dead
band 3 dB, cuts capped at Range/2; band averages freeze in silence. Test: busy trap hats keep their top
(was -0.7 dB, now 0.0 at all speeds); a harsher section is still cut. A/B clips NOT re-rendered (user songs
not in this session) - ask the user to A/B Addie in the plug-in.
(Old note:) ask whether the duller top is wanted; likely fix: keep Tone Steady from
cutting the top unless that band truly drifts (e.g. no cuts above ~4 kHz from short-term hat / cymbal
bursts: longer short-term average or a larger dead band up there), re-render the A/B, ask again.
Clips were rendered before the silence fix (e5d4bb6); Dean's tamer now caps at 60 %.

## Next session quick start
- Read this file. Branch claude/gracious-wright-tgs0no. Pushes build Windows only; Mac on request
  (Actions -> Build & Test -> Run workflow, or ask Claude).
- Session scratch data (MUSDB, teacher stems, venv) does not survive; tools/stem/README.md has the
  steps to rebuild. Model v2 is in tools/stem/models/stemnet_v2.pt.
- Open items: Tone Steady top-end dulling (above); Auto-Master turning a too-hot mix down (offered,
  not done); repo private (user doing it); Pro EQ / Maximizer modes / Match ref / shoot-out list below.

## Unlimiter (v0.13.0, 2026-10-02; user asked for an Ozone-Unlimiter-like "undo" for squashed beats)
Module 01 (before Tonal EQ). dsp/src/Unlimiter.cpp: 5 ms RMS detector (max over channels, Focus filter:
Full / Kick < 150 Hz / Snap > 1 kHz), fast follower held 25 ms + 80 ms release, slow follower 50 ms attack
falls with it; lift = 6 dB per dB of rise beyond 1.5 dB x Amount x squashScale, max 12 dB, lets go over
Length (5-50 ms, default 15); wideband gain, 1.5 ms look-ahead (latency 72 @ 48k). Squash = sample peak
minus RMS over the last 3 s (blocks above -60 dBFS). squashScale: 1 at <= 8.5 dB, 0 at >= 12.5 dB, so
open mixes are never touched (the user's own mixes, -17 LUFS / -1 dBTP, should read ~13: CHECK on a real
song). Calibration is from synthetic tests only (beat pushed 14 dB into a 30 ms limiter gets ~3/4 of its
hit contrast back). Auto-Master: measures Squash on what it heard; scale >= 0.25 -> Amount 50/75/100 by
intensity + a NEED/STEP note; the rest of the chain is set for the restored mix. Params ulOn / ulAmount /
ulLength / ulFocus (hint 3), meters ulBoost / ulSquash. UI: chain note moved to the top of Quick Help,
11 rows fill the panel. NEXT: user test on a squashed beat (MATCH A/B); confirm Squash readings on the
user's own mixes and on a released master.

## Validation (2026-10-03): step 1 of the "prove it's top tier" plan DONE
CI (Windows) now runs pluginval at strictness 10 (max) + Steinberg's VST3 validator (built from the VST3
SDK, cached). Run #62: pluginval SUCCESS, VST3 validator 537/537 (the one earlier failure was the
unnamed program -> getProgramName returns "Default"). CI bug found and fixed: pluginval.exe is a GUI app,
so pwsh didn't wait for it and a failure showed green; now Start-Process -Wait -PassThru.
Next steps the user agreed to (planned for after credits reset Sunday): 2) EBU Tech 3341/3342 loudness
compliance files + true-peak worst cases as tests; 3) sanitizer builds (ASan/UBSan/RealtimeSanitizer) +
block-size / offline-vs-realtime invariance; later competitor comparisons (user renders TDR Limiter 6 GE /
LoudMax / Ozone trial in Cubase) and blind loudness-matched listening. The user does NOT own Ozone.

## Where the MAIN thread paused (2026-10-03): waiting for the user's credits to reset (Sunday)
Done this session: Tone Steady top-end fix, Unlimiter (v0.13.0), review fixes, CI validation step 1 (green).
Resume here with validation step 2 (EBU 3341/3342 + true-peak tests), then step 3 (sanitizers + block-size /
offline-vs-realtime). Open questions for the user: Unlimiter listening test on a squashed beat; Squash
readings on their own mix and a released song; A/B Addie for the Tone Steady fix.
A separate UI thread was started (2026-10-03) for UI work on the same branch: pull before pushing.
