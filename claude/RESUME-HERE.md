# Polisher — resume here (updated 2026-10-01)

Read this first in a new chat. Code: github.com/DK7602/polisher, branch `claude/gracious-wright-tgs0no`
(merge to `main` when ready). Every push builds Mac + Windows; download Polisher-Windows from the
latest green Actions run.

## Status: v0.10.6 — all seven modules live, Auto-Master sets every one
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
