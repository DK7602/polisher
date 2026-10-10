**Re-audit: Unlimiter, Tonal EQ, Input Trim, Maximizer, meters, MATCH, bypass/tail, ceiling (commit 858c1ff)**

I made no edits to the repo. Every number below comes from probe programs run against `libpolisher_dsp.a` and the real `PolisherAudioProcessor`, linked from the plug-in test objects. The existing tests in this area all pass: 61 engine cases and 25 plug-in cases.

## A. Real defects

**A1. HIGH: MATCH re-learns slowly after a level change, so B plays louder in the first A/B after Auto-Master.**
- **Where:** `dsp/include/polisher/LevelMatch.h:80-82`, plus the snapshot memory in `plugin/PluginProcessor.cpp:668`.
- **Cause:** the gap is an 8 s energy average. After a change it starts from the old gap. MATCH also learns while it is switched off, so after the whole-song listen (Drive 0) it has firmly learned a gap of 0 dB.
- **Scenario:** a trap mix at −17 LUFS is played, Auto-Master adds about 8 dB of Drive, the user presses play with MATCH on (Reset Meter On Play left at its default).
  - The true gap is −6.87 dB.
  - MATCH reads 0.00 at 1–3 s, −2.71 at 5 s, −5.13 at 10 s, −6.00 at 15 s and −6.44 at 20 s.
  - So B is 6.9 dB louder than A for the first 3 s, and still 1.7 dB louder at the manual's "about 10 seconds".
- **Snapshot memory stores a gap that hasn't settled.** S2 is 6 dB louder than S1:

  | Time S2 was played (incl. 3 s hold) | Stored gap | True gap | Error |
  |---|---|---|---|
  | 13 s | −4.96 | −6.00 | 1.03 dB |
  | 16 s | −5.31 | −6.00 | 0.69 dB |
  | 40 s | −5.97 | −6.00 | 0.03 dB |

  With a 2 dB difference the error at 13 s is 0.48 dB. The existing test plays 25–30 s, so it never sees this.
- **Fix (S, low risk):** add `LevelMatch::relearn()`. It clears both energies and holds for 3 s, so the first valid short-term pair sets the gap at once (from scratch this takes 3.5 s and is off by only 0.01 dB). Call it:
  - when Auto-Master applies,
  - when entering a snapshot that has no stored level,
  - on state load.
  - Optionally also re-seed whenever the live gap differs from the learned one by more than 2.5 dB for 3 s.

**A2. MED: switching B→A (the A/B button or the host's bypass) lets about 3–14 ms of driven, unlimited audio out.**
- **Where:** `dsp/src/TruePeakLimiter.cpp:150`. On bypass the limiter outputs the samples already in its delay line without applying gain. In addition, `PluginProcessor.cpp:519` ramps Drive down over the first bypassed block, and those samples are also unlimited.
- **Engine alone:** a +6 dBFS tone into a −1 dBTP limiter comes out at +6.00 dBFS for 2.75 ms after bypass.
- **Plug-in:** with Drive +12 on a mix peaking at −10.9 dBFS, the worst peak in the 20 ms after B→A is −0.56 dBFS (block 64 or 512). That is 10.3 dB above A's own peak and 0.44 dB over B's ceiling. It is an audible click on every A/B flip.
- A→B is clean: worst true peak −1.03 dBTP.
- **Fix (S, low risk):** keep the limiter's gain computer running for `delayLength` plus one block after bypass engages, or make Drive jump to 1 instead of ramping when entering bypass. Add a test that the peak after B→A stays at or below max(A peak, ceiling).

**A3. LOW: Unlimiter Focus Kick/Snap cross-fade briefly cancels the crossover frequency.**
- **Where:** `dsp/src/Unlimiter.cpp:178` and `:215`. The 20 ms fade mixes the plain delayed signal with the band-split (Linkwitz-Riley) path, which is 180° out of phase at the split frequency.
- **Measured:** a 150 Hz tone (Kick) drops −17.1 dB and a 1 kHz tone (Snap) drops −15.0 dB, about 11 ms into the fade.
- **When it happens:** Amount leaving 0, a Focus change, or ulOn / A/B toggles.
- **Fix (M):** fade the lift instead of switching paths, or always run the band path while Focus is not Full.

**A4. LOW–MED: Auto-Master's EQ model assumes a −4.5 dB/oct spectrum and over-predicts the Low End shelf's effect.**
- **Where:** `dsp/src/AutoMaster.cpp:881`.
- **Measured:** for Low End +3 dB the model predicts +2.04 dB in the band. The actual change is +1.34 dB on the `trapLike` test mix and +0.89 dB on `musicLike`. So Auto-Master delivers about 45–65 % of the planned low-end change on 808-heavy mixes. Other bands are off by 0–0.4 dB.
- **Fix (M):** build the band-interaction matrix from the clip's own measured spectrum.

**A5. LOW: the "Covered band" reason is sometimes false.**
- **Where:** `AutoMaster.cpp:1218`.
- **Measured:** in 1,600 random solver cases, 2–4 per 400 had a band labelled "covered" that actually moved the wrong way or not at all. Example: Mid wanted +0.78 dB, got 0.0 dB gain, ended at −0.02 dB because the neighbours' moves cancelled out.
- **Fix (S):** only say "covered" when the achieved change is at least half the wish, in the same direction. Otherwise say the neighbours' moves pull against it.

## B. Text problems

1. **MATCH timing (manual:329, manual:374):** "play B for about 10 seconds", "give it 10–15 seconds", "matched at once". As measured in A1, these are 0.7–1.7 dB off after a change. Fix A1, or say about 25 s.
2. **Club ceiling (`AutoMaster.cpp:1872-1873`, `app.js:1121-1122`):**
   - Auto-Master gives Club (−7 LUFS) a −1.0 dBTP ceiling. But its own Loud reason (−9 LUFS) says louder masters need a lower ceiling and that "Spotify asks for −2". The Maximizer's Learn then tells a Club user that "−1.5 to −2 keeps it clean", contradicting Auto-Master.
   - The Loud reason cites −2 dBTP but Auto-Master sets −1.5.
   - Measured on a −7 LUFS master: Ogg q5 decodes at +0.49 to +1.12 dBTP with a −1.0 ceiling, and +0.21 to +0.40 with −1.5. MP3 320k stayed below 0 (−0.22 to −0.37).
3. **Subsonic filter (manual:184, `app.js:198`):** "the boost doesn't also pump up rumble below 20 Hz". With Low End +6, 15 Hz is still +2.9 dB and 18 Hz +4.9 dB; only below about 12 Hz is it cut. The other claim ("costs less than 0.5 dB at 20 Hz") is true: −0.42 dB.
4. **True-peak promise (`app.js:1121`, manual:311):** "Nothing leaves Polisher above X dBTP, including peaks between samples" / "guarantees". This holds on the standard BS.1770 meter (−1.03 dBTP). An independent 16x meter reads up to 0.74 dB higher on a trap mix with the clipper at 44.1 kHz. Suggest "measured the way streaming services measure it".

## C. Test gaps

- No test of the peak at the B→A / host-bypass switch (A2).
- MATCH tests only use 25–30 s. Nothing checks the documented 10 s, re-learning after a change, or how accurate the snapshot memory is at 10–15 s.
- No test of the mono bus layout. I checked it by hand: it runs, true peak −1.03 dBTP, no NaN.
- No test that Auto-Master's EQ actually achieves the planned band changes on a mix, and none that the "covered" text is true.
- No test of the level during the Unlimiter's Focus path cross-fade.
- The tail test only checks that the tail is more than latency + 0.1 s.
- The sample-rate-change test cannot see the Unlimiter carrying its Squash reading across a re-prepare, because its test signal reads open.

## D. Verified OK

- Tonal EQ against the analog shapes, 20 Hz–20 kHz: at most 0.63 dB off (44.1 kHz, Presence at 10 kHz −9 dB, at 19.7 kHz), 0.41 dB at 48 kHz, ≤0.03 dB at 88.2–192 kHz. Above 20 kHz it stays within ±1.04 dB.
- EQ frequency jumps at ±9 dB (also Low End 20→120 Hz): no clicks.
- EQ solver, 1,600 random cases at 44.1–192 kHz: no limit or direction violations, never worse than leaving the EQ flat.
- Unlimiter: lift law matches the docs (10 dB × strength). The live Squash meter agrees with Auto-Master's reading within 0.7 dB at 44.1–192 kHz. Open 808-led loops read open (strength ≤0.13, below Auto-Master's 0.25 threshold).
- Limiter: true peak stays within the ceiling after A→B at 44.1, 48 and 96 kHz.
- Tail: with extreme low EQ the output falls below −60 dBFS 0.20 s after the input stops; the reported tail is 0.25 s.
- MATCH stays out of offline export; the editor switches it off on close; A ignores Input Trim; Auto-Master's report quotes the mix before trim.
- Old saved states without `ulSquashSet` or `inTrim` fall back to the live meter and 0 dB; the stored Squash goes into undo and snapshots.

The probe programs are in `/tmp/claude-0/reaudit/unlim-eq-max/scratch/` (plug-in probes in `plug/`, linked with `build_probe.sh`).