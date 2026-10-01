# Polisher — resume here (updated 2026-10-01)

Read this first in a new chat. Code: github.com/DK7602/polisher, branch `claude/gracious-wright-tgs0no`
(merge to `main` when ready). Every push builds Mac + Windows; download Polisher-Windows from the
latest green Actions run.

## Status: v0.10.4 — all seven modules live, Auto-Master sets every one
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

## Next, when the user asks (paused 2026-10-01: "good for now until bugs or add-ons")
1. Pro EQ: mid/side per band, linear-phase mode, dynamic EQ bands (harsh 3-5 kHz, boomy lows).
2. Maximizer: transient-aware / multiband modes, codec preview, dither for 16-bit exports.
3. Match ref: load a reference song, match tone / loudness / width (replaces genre curves).
4. Blind shoot-out vs Ozone 12 Advanced and competitors; fix what loses.
5. Optional: Auto-Master opens highs a little for Trap even when already fairly wide (user may want).
Housekeeping: repo private (due after Oct 1), GitHub 2FA by Nov 4 2026, Actions Node 20 warnings,
"Polisher" trademark search, rights to AI-rendered art, code signing / notarization before release.
