# Polisher — resume here (updated 2026-10-01)

Read this first in a new chat. Code: github.com/DK7602/polisher, branch `claude/gracious-wright-tgs0no`
(merge to `main` when ready). Every push builds Mac + Windows; download Polisher-Windows from the
latest green Actions run.
CI: pushes build Windows only (private repo minutes; macOS counts 10x). Mac build on request:
Actions -> Build & Test -> Run workflow ("Also build macOS" ticked), or ask Claude to trigger it.

## Status: v0.11.0 — all eight modules live (Stem EQ new); Auto-Master sets every one but Stem EQ
Chain: Tonal EQ -> Bass Control -> Dynamics -> Stereo Imager -> Saturation -> Drive -> Soft Clipper
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
