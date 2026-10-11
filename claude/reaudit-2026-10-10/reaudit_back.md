A. Real defects

**A1. MED: switching Mono Below, the Imager or Dynamics briefly cuts a band out of the centre of the mix.**
- Where: `dsp/include/polisher/BassControl.h:90`, `StereoImager.h:60`, `MultibandDynamics.h:162`.
- Cause: when one of these modules switches on or off, it cross-fades the plain signal against an all-passed copy of it. Halfway through the 20 ms fade the two cancel at each all-pass frequency, and in about an octave around it.
- What triggers it: on/off, the host bypass / A-B button, a snapshot recall, undo/redo, Auto-Master's apply, and any knob dragged through its neutral value. Examples: Imager 100→101 %, a Dynamics band gain set to 0.1 dB, Mono Below crossing Off.
- Measured on a mono tone at 48 kHz, worst dip of the short-time level:

| Switch | Tone | Dip |
|---|---|---|
| Mono Below 120 Hz on or off | 120 Hz | -17.9 dB (about 56 ms below -1 dB) |
| Mono Below 120 Hz on or off | 90 / 160 Hz | -8.4 / -8.6 dB |
| Imager highs 100→101 % | 250 Hz / 5 kHz | -17.6 / -20.0 dB |
| Dynamics neutral → Air +0.1 dB | 1 kHz | -21.0 dB |
| Dynamics neutral → Air +0.1 dB | 120 Hz / 6 kHz | -10.8 / -11.7 dB |

- Same depth at 44.1 and 192 kHz.
- Trap beat with the plug-in test's busy settings, A/B flip:
  - B→A: 1 kHz band -12.3 dB for 35 ms; 5 kHz -13.6 dB and 6 kHz -15.5 dB for 40 ms.
  - A→B: 60–250 Hz -5.8 to -6.3 dB.
  - With these three modules left neutral, my-area dips disappear (≤ -1.25 dB at 250 Hz–6 kHz). The 60–120 Hz dips that remain come from the EQ and Drive (see the cross-area note).
- The snapshot no-click test's own signal (110 Hz tone, S1↔S2) dips -17.8 / -18.8 dB about 13 ms after the recall. The test still passes because it only checks the largest sample-to-sample step.
- Fix, "park and glide": do the dry→all-pass fade while the all-pass sits at a subsonic frequency (5–12 Hz, where the cut can't be heard), then glide it to the target. Reverse the order when switching off.
  - Mono Below prototype (`/tmp/claude-0/reaudit/bass-dyn-sat/m/fix/BassControlFix.cpp`, about 25 lines):

    | Tone | Before (on / off) | Prototype (on / off) |
    |---|---|---|
    | 120 Hz | -17.9 / -17.8 dB | -1.1 / -0.13 dB |
    | 90 Hz | -8.4 / -8.5 dB | -1.9 / -0.2 dB |
    | 160 Hz | -8.6 / -8.6 dB | -0.6 / -0.07 dB |

  - With a 60 ms glide, every tone from 30 to 250 Hz stays within -3.3 dB on and -0.4 dB off.
  - Once settled, the prototype is identical to the current code.
  - Trade-off: a 30–45 Hz tone gets a brief phase sweep when switching on (-0.7 → -3.2 dB).
  - Imager: same idea for its two mid all-passes and the side split, with band gains held at 1 until the glide lands.
  - Dynamics: park all three crossovers below their normal range, hold band gains at unity until they arrive, then fade the compression in.
- Effort M (Bass S–M, Imager M, Dynamics M). Risk M: switches respond 0.1–0.2 s slower. The neutral state stays bit-exact.

**A2. LOW (the known issue): Tube in Full mode starts with a small DC offset after `reset()`.**
- Where: `dsp/src/Saturation.cpp:141` (`reset` starts the path with no warm-up, so no DC is measured) and `:244` (the 1 Hz DC filter starts cold).
- Measured: a render that starts at 4 s into the trap beat, compared with a continuous render:
  - Tube +12 dB, Mix 50 %: subsonic offset -43 dBFS, peaking about 100 ms in; still -66 dBFS between 0.5 and 1 s.
  - Tube +18 dB: -39 dBFS, then -62 dBFS.
  - Tape, Clip and Bass only: none (-206 dBFS after 0.5 s).
- For scale: Tube adds -37 dBFS of subsonic content all the time anyway. In normal playback, any entrance after silence produces -35 dBFS. So this is inaudible.
- Fix (S, low risk): after `reset()`, start the DC filter at 40 Hz and glide it to 1 Hz with a 50 ms time constant, then snap to the exact 1 Hz coefficient.
  - Prototype (`m/fix/SaturationFix.cpp`): start offset -58 dBFS at +12 and -53 dBFS at +18; below -82 dBFS after 0.5 s.
  - Continuous renders are unchanged after 2 s (difference 1.8e-8).
  - The `LegacySaturation` copy in `tests/test_saturation.cpp` renders from `reset()`, so it needs the same start or the bit-exact Full test will fail.

**A3. LOW: changing Saturation Mode while playing switches instantly, with no fade and no DC handling.**
- This happens on a user click, a snapshot recall, undo or Auto-Master.
- Measured, Tape→Tube on the trap beat:
  - Subsonic offset -46 dBFS at +12, -39 dBFS at +18, decaying over about 1 s.
  - A small step: the energy above 8 kHz at the switch is about -40 dB, against -19 dB for the hi-hats.
- Part of it is a state bug at `Saturation.cpp:250`: in Tape and Clip the DC filter keeps `hpX = 0` while `hpY = res`. On a switch to Tube, the last residual sample therefore becomes an extra offset.
  - Setting `hpX = res` there changes no steady output and improves the offset (-46 → -48, -39 → -43.5 dBFS). Effort S.
- Full fix: handle Mode like Focus already is (warm the new curve next to the old one with DC seeding, then a 30 ms raised-cosine fade). Effort M.

B. Text problems
1. Snapshot texts say every control glides and nothing clicks: `docs/manual/polisher-manual.md:369` and `plugin/ui/app.js:2703`. Mode jumps (A3), and on/off changes of Mono Below, Imager and Dynamics dip (A1, -18 dB). Suggest "settings change smoothly" until A1 and A3 are fixed.
2. `docs/manual/polisher-manual.md:273`, Imager: "the centre … is never touched". Since v0.15 the centre runs through the 250 Hz and 5 kHz all-passes while the Imager works, and dips during switches. Suggest "the centre keeps its level and its place".
3. `README.md:34`: "aliasing 119 dB down" is the best case only (Tape/Tube, one 7 kHz tone, 48 kHz: -118.6 dBFS).
   - Clip mode at 44.1 kHz: -87.7 dBFS.
   - Hot 7 kHz + 11 kHz two-tone at Drive 18: -74 to -77 dBFS.
   - The test only requires < -80.
4. All other Learn and manual texts for Bass Control, Dynamics, Imager, Saturation and Clipper match my measurements, and every warning has its NEED/STEP plan. Sub Tame "about a second" is 0.75 s in the code; close enough.

C. Test gaps
1. All "no click" tests (`tests/test_bass.cpp:336`, `tests/test_dynamics.cpp:221`, `tests/plugin/test_plugin.cpp:1739`) check only the largest sample step, which a cancellation dip never raises. Add a check on the 2-period level of a tone at each all-pass frequency (no more than about 1–2 dB below the lower of before and after).
2. `tests/test_imager.cpp` has no switching, sample-rate or stability test.
3. Nothing tests a Saturation Mode change while running, or the Tube DC after `reset()` (start mid-song vs continuous render).
4. No plug-in-level test of the transient when A/B is flipped, the switch this user makes most.
5. Punch and Sub Tame are not tested across sample rates (cheap to add; I measured them fine).

D. Verified OK
- My build: 51 engine test cases in my areas plus monitoring, and all 36 plug-in tests, pass.
- Punch: kicks +4.00 dB, clean 808 0.00 dB at 44.1/48/96/192 kHz. Sub Tame -1.69 / -0.54 dB at every rate.
- Mono Below once settled: side at 50 Hz -41 dB, centre at 400 Hz unchanged.
- Clipper on/off and oversampling changes: no dip at 1–19 kHz.
  - The oversampled path loses -0.11 dB at 20 kHz and -2 dB at 21 kHz (44.1 kHz).
  - Aliasing at 1x/2x/4x/8x: -19 / -48 / -64 / -79 dBFS, which fits the Learn advice to use 4x.
- A/B flips with only Saturation or only the Clipper active: no dip (≤ 0.02 dB).
- Bass only after `reset()`: no DC.
- Dynamics band solo while the module is switched on mid-solo: no step (0.00438, same as steady playback).
- Latency is constant and exact pass-through holds at neutral (existing tests pass). Old states without `satFocus` load as Full.

Cross-area note for the EQ auditor: with only Tonal EQ Low End +2 dB, a B→A flip dips the 60–120 Hz bands by -3.7 dB for 5 ms.

All measurement programs (`m1`–`m9`) and the two prototypes (`fix/`) are in `/tmp/claude-0/reaudit/bass-dyn-sat/m/`; the build is `/tmp/claude-0/reaudit/bass-dyn-sat/build`.