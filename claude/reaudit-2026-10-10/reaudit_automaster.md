**Auto-Master re-audit (commit 858c1ff): 3 medium defects, 6 low ones, 8 text problems; handoffs, landing and allocation checked OK**

All numbers below are measured. I built my own copy in `/tmp/claude-0/reaudit/automaster/build` and ran small programs against the engine and the real processor. The existing tests pass on that build: engine 39 cases (5303 assertions), plug-in `[automaster]`, `[reference]`, `[snapshots]` and `[history]` 15 cases.

### A. Real defects

1. **MED: reference loudness is compared over different lengths.** The reference's loudness is measured over its loudest 30 s (`analyseReference`, `ReferenceTrack.cpp:234`), but the mix is mastered from its loudest 12 s (`AutoMaster.cpp:1057`).
   - Reference hook is −8.0 LUFS; its 30 s value is −8.14 to −9.41, depending on hook length (13.7–27.4 s) and verse level.
   - The master's hook lands at −8.84 to −9.39 LUFS: 0.7–1.4 LU quieter than the reference's hook.
   - The report still says "reached" and "Matched", and Learn's REF/MASTER loudness cell is green.
   - Fix (S, low risk): store the reference's loudest-12 s LUFS in the profile and use it for the target and the Learn table. Old saved profiles fall back to the 30 s value.

2. **MED: the report treats the mix after the user's Stem EQ as "the mix as it arrives"** (`AutoMaster.cpp:1066`). This hits every song saved before Keep Loudness existed, since those load with it off.
   - Real input: −17.0 LUFS, −3.6 dBTP (`capturedLufs` −17.00).
   - Bass +6: report says "Your mix measured −12.5 LUFS with peaks at 0.4 dBTP", plus a false HEADS UP: "peaks at +0.4 dBTP before Polisher… put a gain plug-in before Polisher by about 2 dB".
   - Bass +12: −7.1 LUFS, +5.8 dBTP, "about 7 dB".
   - Input Trim reason says "Your mix arrives at −7.5 LUFS" for a −12.0 LUFS mix. Learn's MIX column (`inputLufs`) has the same error.
   - Fix (S): take the arriving level and peak from the raw input, and say the Stem boost is why Input Trim is needed.

3. **MED: Tone Steady can be held to another song's tone.**
   - The target is saved in presets. A preset saved after a 45 s Auto-Master on song A, loaded on song B, shows "your song · 0:45".
   - A 15 s Auto-Master on B then keeps A's tone (`AutoMasterController.cpp:222`).
   - Effect at Amount 40, Range 3, on an acoustic song: Low End +2.2, Bass +1.35, Mid −0.7, Air +1.3 dB, −2.5 LU. On a pink-noise-style song: +2.1, +1.9, Upper Mid −1.1 dB. With its own tone or following the music: ≤0.4 dB.
   - Fix (S): keep the stored tone over a short listen only if the median band difference is under ~2 dB. Optional (M): warn when the live tone sits more than 3 dB off the target for 20 s.

4. **LOW-MED: the Bass-only fallback depends on input level, not on whether there is low end** (`AutoMaster.cpp:1617`). An 808-heavy mix (sub 13 dB above the Trap norm) at −30 LUFS or quieter gets Focus Full at Drive 18, with the text "no 808 to speak of". At −27 LUFS it correctly gets Bass only. Fix (S): decide from the low-end share (`hasLowEnd`, bands 0–1).

5. **LOW-MED: the reference curve (dashed line) ignores sample rate** (`AutoMaster.cpp:825`, `drawRefCurve`). The display's reading of the same sound at the same LUFS changes with rate: +0.2–0.6 dB at 48 kHz, +3.3–3.9 dB at 96 kHz, +5.8–6.8 dB at 192 kHz, relative to 44.1 kHz. The genre line corrects for this; the reference line doesn't, so a 44.1 kHz reference draws 3.4 dB low in a 96 kHz project. Fix (S): save the file's rate and shift by 10·log10(project rate / file rate).

6. **LOW: mono bus.** The song listener treats mono as two identical channels. Input −17.00 LUFS reads as −13.95. The report says the master is −9.0; the plug-in's own meter reads −11.97. Fix (S): pass the channel count to the listener.

7. **LOW: the silence check works per host block** (`SongListener.cpp:101`), so very small blocks lose samples inside music.
   - Heard 53.79 / 53.22 / 52.45 s of a 54 s song at 16 / 4 / 1-sample blocks.
   - At 16-sample blocks the learned tone is +4 to +11 dB off above 200 Hz. At 64 samples and up it is exact.
   - Cubase is normally fine; hosts with variable block sizes are not.
   - Fix (S): decide silence per 100 ms piece.

8. **LOW: timeline text after transport jumps or cycle loops.**
   - Playing 0:40–0:50, then jumping to 2:00–2:10, reports "0:43–0:55". 0:50–0:55 was never played, and the analysed audio is spliced.
   - A 1:00–1:16 cycle reports "1:09–1:21".
   - The "Whole song" figures (−13.55 LUFS, LRA 3.26) are measured on the repeated loop.
   - Fix (S): check that the window's times run continuously; otherwise say "into what I heard".

9. **LOW: analysis takes 6.6 s at 44.1 kHz, 12.9 s at 96 kHz and 23.7 s at 192 kHz** on this machine, against "a few seconds" in the UI. Closing the project during analysis can block for up to 60 s.

### B. Text problems

1. `AutoMaster.cpp:1144–1151`: beat at −7 LUFS, target Stream −14, Unlimiter kept only 0.5 dB. The reason says "It pays off when the target is a few dB below the −7.0 LUFS… for example Stream −14", but the target already is Stream −14. The note says "arrives about as loud as your target", but it is 7 LU louder.
2. `AutoMaster.cpp:1390`: "I learned your song's overall tone from the 0:45 I heard" appears beside "Listened to 0:15" when a stored tone is kept.
3. `app.js:1573/2143`: "Paused. Press play to go on" and "Paused at… play on". After 30 s of music a 4 s pause ends the listen (measured: a 4.2 s pause after 35 s started the analysis). `app.js:1576` "finishes by itself" also needs "after 30 s of music".
4. `AutoMaster.cpp:1878`: "LOUDER, IF YOU WANT… it will get closer to your target" appears even when the target was reached (−9.20 LUFS). Show it only when the target was missed.
5. `AutoMaster.cpp:1641`: the false "no 808" text from A4.
6. `AutoMaster.cpp:1809`: at −40 LUFS input it says "At the Drive this target needs, no peak would reach the clip level", but the target is out of reach (Drive at its 24 dB maximum, master −17.1 LUFS).
7. Manual line 136 ("Loudness of its loudest part — aims for it") and the Learn REF table compare 30 s against 12 s (A1).
8. `app.js:1569/2159`: "a few seconds" (A9).

### C. Test gaps

- Report text with Stem EQ moves and Keep Loudness off.
- Reference loudness compared hook against hook.
- A tone target from another song, via preset or a short listen.
- Bass-only fallback against input level.
- Reference curve when the project rate differs from the file's rate; the genre-curve test only runs at 44.1/48 kHz.
- Song listener with tiny or variable block sizes (the existing block-size test checks only the audio output).
- Report positions after transport jumps and cycle loops; a pause longer than 4 s after 30 s of music.
- Auto-Master on a mono bus.
- The allocation test's counter is process-wide, so it would also count the background analysis if the two overlapped. I counted per thread instead.
- No Auto-Master-level check with the 16x reference true-peak meter: masters read −0.64 to −0.96 dBTP against a −1.5 ceiling, and −0.08 against −1.0 on plucked acoustic material with no clipper. That is the Maximizer's known ~0.9 dB margin, not my area.

### D. Verified OK

- **Target landing:** the report's output matches a fresh render of the whole song's loudest 12 s within 0.01–0.03 LU at 44.1, 48, 96 and 192 kHz. Input-referenced true peak sits at the ceiling.
- **Whole-song figures:** song LUFS and LRA match the loudness meter (−18.70 / 12.52 both). The loudest part is found at 63.7 s for a hook at 64 s, and the "Listened to…" line is correct.
- **Auto-finish:** finishes 4.5 s after the song ends with the "by itself" flag set; FINISH is refused before 12 s.
- **Audio thread:** capture is allocation-free, including the finish-to-analysis handoff, at all rates. Listener memory is fixed at 8.8 / 9.5 / 18.7 / 37 MB (44.1 / 48 / 96 / 192 kHz).
- **Thread safety:** ThreadSanitizer stress (8 analyses, 4 cancels, tone-only runs) found no races in the handoffs. Its only warnings were at JUCE thread-pool teardown, a side effect of building JUCE without the sanitizer.
- **Input Trim:** a −7 LUFS brickwalled mix lands at −13.97 for Stream −14 with trim −6.2 dB.
- **Quiet mix:** a −40 LUFS mix gets the honest "maximum Drive only reaches" note with NEED/STEP.
- **Keep Loudness on:** report quotes the right input level, and the master lands correctly.
- **Reference:** loudness limits, ceiling choice and the "NOT AS LOUD" note are correct, and undo reads "Auto-Master (Match ref, Balanced)".
- **Tone target in Auto-Master:** it changes the hook by at most 0.43 dB and doesn't move the landing.
- **"Louder than what Auto-Master heard" warning:** no false alarm on steady hooks.
- **Genre line:** follows the project's sample rate.

Probe programs are in `/tmp/claude-0/reaudit/automaster/probes/` (engine), `/tmp/claude-0/reaudit/automaster/pprobe/` (processor) and `/tmp/claude-0/reaudit/automaster/tsan/` (thread checks).