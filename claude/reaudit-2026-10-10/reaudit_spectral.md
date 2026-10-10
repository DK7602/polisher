**Spectral stage re-audit (Stem EQ 03, Tone Steady 04, Resonance Tamer 05, Model Hears) at 858c1ff**

Everything was measured with small programs linked against my own build. All existing tests pass: 39 engine cases tagged spectral / stemeq / songlistener / keep, and all 36 plug-in cases. Unless noted, Tone Steady numbers use Auto-Master's usual setting: Amount 40 %, Range 3 dB, Slow.

## A. Real defects

**A1 HIGH: with a stored target, every section without the 808 gets all its mids and highs cut.** `dsp/src/StemEQ.cpp:544-553`
- **Cause:** the overall offset is removed with a mean weighted by the target's band levels, so the 808 bands dominate it. The guard at :569-574 then fades the low-band correction, but those bands still set the mean. Everything above 160 Hz reads "too loud" and is cut.
- **Measured (808 enters at 0:16):** in the intro, 200-500 Hz −2.35 dB, 500 Hz-2 kHz −3.00 dB, 2-5 kHz −2.45 dB, 5-16 kHz −1.52 dB, and the intro is −0.58 LU quieter. Following the music, the same intro is moved 0.00 dB.
- **When the 808 leaves for half the song:** the rest of the song is cut −1.3 to −1.8 dB.
- **Fix (S, low risk):** recompute the mean over the bands the guard keeps (two passes). I tried this on a copy: the intro moves only −0.16 to −0.01 dB, and all 39 engine test cases still pass. My copy is in `/tmp/claude-0/reaudit/spectral/fix/`.

**A2 MED: Keep Loudness holds a loud section's correction for 8-10 s into a quieter one.** `StemEQ.cpp:636-642`
- **Cause:** the ~2 s average is weighted by energy, so a loud section outweighs a quieter one that follows.
- **Measured (Bass +6 dB, a −6 dB breakdown without the 808):** Keeping level stays at −4.6 dB at the breakdown start, −3.3 dB 5 s in, −1.15 dB 10 s in. The breakdown plays −3.96 LU quieter for 4 s, then −2.32 LU. Played on its own it is −0.21 LU, so it also depends on where playback starts.
- **Fix (S, low risk):** average each frame's own mix of stems (divided by its energy) and add a relative silence gate. On my copy: −2.13 / −0.39 LU, and the tests pass.

**A3 MED: Model Hears understates the 808 about tenfold.** It weights bands by A-weighting (`StemEQ.cpp:210`, `bandLoud`).
- **Measured:** Bass shows 5.9 % on the test-suite trap mix and 10.6 % on the trap-like beat in `tests/Signals.h`.
- **By LUFS weighting:** the same mixes are 68.7 % and 63.2 % bass.
- **What a move does:** Bass +6 dB (Keep off) adds +4.77 / +4.50 LU.
- **Why:** A-weighting at 45 Hz is −32 dB; the LUFS weighting is −4.7 dB.
- **Fix (S):** weight by K (the `binK` table is already in the class, as Keep Loudness uses it), or by an 80-phon equal-loudness curve.

**A4 MED: a target from another song is used without warning.**
- **How it happens:** presets, a default preset and duplicated instances all carry `toneTarget`. A short Auto-Master run (under 30 s) keeps the stored target (`plugin/AutoMasterController.cpp:221-223`).
- **Measured (song B held to song A's tone):** 1-3 kHz −2.50 dB, 3-8 kHz −1.90 dB, 8-16 kHz −1.50 dB. With its own target: about 0.
- **The two tones were 10.5 dB RMS apart,** yet the cell still reads "your song · m:ss".
- **Fix (S-M):** keep the stored target only when the new tone is within about 3 dB RMS of it. Also add a NEED/STEP warning when the live drift from the target stays large.

**A5 MED: with a target, a part entering above 160 Hz is cut for as long as it plays.**
- **Measured:** a chorus lead adding +4.7 dB at 500 Hz-3 kHz is cut −1.3 dB for all 20 s. Following the music it starts at −1.2 dB and eases to −0.25 dB.
- **Why it can't simply be fixed:** a new part can't be told apart from a harsher drift.
- **Fix:** correct the texts (see B1). A design option is to cap target-mode cuts above 160 Hz at Range/2.

**A6 LOW: the stored target still depends on the start point.**
- **Cause:** the "quiet passage, more than 15 dB under" gate uses a 10 s running level (`StemEQ.cpp:540`).
- **Measured (a breakdown 17 dB under the chorus):** 200-400 Hz −0.99 dB when played from bar 1, −0.27 dB when played from the chorus (difference −18 dB re signal). After the A1 fix the difference falls to −25.5 dB (about 0.5 dB).
- **Fix (M):** store the song's level in `ToneTarget` (SongListener already measures it) and gate against it, adjusted for Input Trim and the Tonal EQ.

**A7 LOW: CPU spikes on the blocks that contain an FFT hop.**
- **Measured, stage only, 99.9th-percentile block (shared machine, generic -O2 build):**

| Rate | Block | Time | Share of the block's time |
|---|---|---|---|
| 48 kHz | 32 samples | 0.43 ms | 64 % |
| 96 kHz | 64 samples | 0.79 ms | 118 % |
| 192 kHz | 128 samples | 0.87 ms | 131 % |

- **Average** is only 3.3-9.9 % of one core.
- **Fix:** document a minimum buffer of 256 at 96 kHz and up (S), or spread the model work across blocks (L).

**A8 LOW (synthetic test material only): the tamer's statistics may treat a clean sung line as ringing.** On the test-suite trap mix the full-strength cut reads 0.43 dB, so Auto-Master sets the tamer to 60 % Full. That trims the vocal harmonics at 0.8-1.5 kHz by −0.63 dB. The trap-like beat reads 0.30, right at the threshold. Re-check the 0.3 dB threshold on real vocals.

## B. Text problems

1. **"What you hear is what you export, from any start point" / "Parts that come and go are left alone"** (`app.js:1524`, `manual:220`, `manual:518` FAQ, Auto-Master report `AutoMaster.cpp:1390-1392`). False because of A1, A5 and A6. Suggested wording: "parts that come or go in the low end are left alone; a part that comes in above the bass is treated like a drift".
2. **Model Hears** (`app.js:1450`, caption `app.js:724`, `manual:196`):
   - "Each frequency counts by how loud it sounds… a part it barely hears won't move much": A3 contradicts this.
   - The meter also shows the incoming mix before the user's moves, not "what you hear".
3. **`manual:198`, Keep Loudness "listens over about 2 seconds":** it takes 8-10 s after a loud section (A2).

## C. Test gaps

- Target mode in a section without the 808: only the low end is checked (`test_songlistener.cpp:306`). This is how A1 slipped through.
- A part entering above 160 Hz with a target.
- Keep Loudness across a loud-to-quiet section change (only steady mixes are tested).
- The start-point test uses an even level throughout: no quiet intro or breakdown near the 15 dB gate, and no stop/locate at plug-in level with state kept.
- Model Hears share against the loudness a move actually adds.
- A short Auto-Master run with a stored target from another song.
- A clean harmonic line must not count as ringing.
- The no-allocation test never sets a target while the EQ is automated. I checked by reading that the `toneTargetAtStage` path doesn't allocate.
- The tamer listen alignment is tested at 48 kHz only.

## D. Verified OK

- **Latency:** 1792 / 3584 / 7168 samples = 40.6 / 37.3 ms at 44.1-192 kHz.
- **Offline equals real time:** bit-exact with random blocks of 1-4096, everything on (Stem EQ, Keep, Tone Steady with target, tamer), at all six rates.
- **Keep Loudness on a steady mix:** within 0.39 LU at all six rates (up to 10.4 LU without it).
- **Tamer meter frequency:** within 2.3 % from 90 Hz to 12 kHz at every rate.
- **Tamer listen:** output plus what it cut equals the delayed input (error < 1e-6) after 879-3411 samples. That is under one FFT frame, so the 2-frame arm time is enough.
- **808 notes** are not evened out: at most 0.15 dB at Slow.
- **Target learned at 44.1 kHz** and used at 48-192 kHz: at most 0.15 dB difference.
- **Unlimiter Kick/Snap** in front of a target: no fight (+0.06 / +0.27 dB).
- **Old states:** `stemKeep=0` is inserted, a missing target means "following the music", and a damaged target is ignored.

Measurement programs are in `/tmp/claude-0/reaudit/spectral/m/` and the build is in `/tmp/claude-0/reaudit/spectral/build/`.