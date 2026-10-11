**Polisher plug-in/UI re-audit (commit 858c1ff)**

I found 9 real defects. Four are medium severity, and no defect reaches an offline export. The existing tests pass: plug-in tests 36/36 (2184 assertions) and the UI smoke test. Scratch builds, probes (`probe/probe1.cpp` to `probe8.cpp`), Playwright scripts and screenshots are in `/tmp/claude-0/reaudit/plugin-ui/`.

### A. Real defects

1. **MED-HIGH: Space or Enter presses the last-clicked button again** (all `<button>`s in `app.js`, measured in Chromium).
   - After clicking Auto-Master (LISTENING), Space cancels it: "Auto-Master stopped listening. Nothing was changed." After 12 s of music, the same Space would end the listen early.
   - Space after MATCH turns it off again, and Space after a snapshot slot recalls it again. LISTEN, PHONE and STORE behave the same way.
   - The UI says "Click, then play your whole song", so a Cubase user will reach for Space.
   - JUCE's WebView2 code has no key forwarding (`juce_WebBrowserComponent_windows.cpp`). I could not run Cubase here, so whether Cubase ever sees the Space is unconfirmed; either way the button fires again.
   - Fix (S, low risk): blur buttons after a pointer click, or stop them taking focus on mousedown. Also swallow Space/Enter at document level unless the user is navigating with the keyboard.

2. **MED: MATCH comes back on from a saved project** (`PluginProcessor.cpp:786-824`). `levelMatch` is saved like any parameter.
   - A project saved with MATCH on reloads with it on. With the window never opened, playback measured **−8.98 dB** against the real master, with no banner. A Realtime Export would include it.
   - Fix (S): force `levelMatch` to 0 in `setStateInformation`, the same way `stemKeep` is inserted.

3. **MED: Listen and Solo ignore the Soft Clipper's makeup** (`PluginProcessor.cpp:490-557`). Measured with Clip Level −6:
   - The four Dynamics solos sum to **−5.99 dB** against the master.
   - Saturation LISTEN plays **−6.00 dB** below its real level.
   - The `listenUnder` reading is −79 dB where the true value is −73.2 dB.
   - Negative Clip Levels are normal use: Learn tells users to lower Clip Level, and Auto-Master chooses e.g. −1.0.
   - Fix (S): apply the clipper's clean gain to signals formed before the clipper.

4. **MED: storing a snapshot, or loading/clearing a Match ref, doesn't mark the project as changed** (`SettingsHistory.h:375-393`, `PolisherWebEditor.cpp:289-311`).
   - Neither path calls `updateHostDisplay(...withNonParameterStateChanged(true))`. JUCE's VST3 wrapper only flags the project dirty for parameter edits or that call (`juce_audio_plugin_client_VST3.cpp:1560`).
   - So Cubase won't ask to save, and S1–S4 or the reference are lost on close. Found by reading the code; I could not confirm it in a host.
   - Fix (S): make that call on store and on reference load/clear.

5. **LOW: a zero-sample process call skips the Drive ramp** (`:519-520`; also Trim `:469-471` and MATCH `:599-601`).
   - With a 12 dB Drive jump, the worst sample step was 0.211 against 0.132 without the empty call.
   - Fix: return early when `numSamples == 0`.

6. **LOW: PHONE safety stage** (`Monitoring.h:172-185`).
   - On bright, loud masters PHONE needed 2.4–3.8 dB of instant-attack limiting. That distortion is the monitor's, not the master's.
   - `safetyGain` is not reset when monitoring goes idle, so the next PHONE starts **−0.42 dB** low with a small level step.

7. **LOW: snapshot recall with a control the slot lacks.**
   - Recall leaves that control where it is, and the slot still shows as playing and unchanged (measured: `satFocus` stayed on Bass only, modified = 0).
   - This only matters once another control is added after users have saved snapshots.
   - Fix: set missing controls to their defaults on recall.

8. **LOW: undo gaps** (`app.js:912`, `:375-389`, `:898-908`).
   - Double-clicking an EQ point to reset it makes two undo steps.
   - There is no `lostpointercapture` handler, so a lost drag leaves the gesture open. The history then stops recording separate steps until Undo is pressed.

9. **LOW: Integrated isn't restarted after undo/redo, a snapshot recall, an Auto-Master apply or LEARN/CLEAR**, even with "Restart loudness after you change a control" on. Only direct edits restart it.

### B. Text problems

1. "Never in your export" in the MONO/PHONE tooltips (`index.html:40-41`) and the LISTEN texts (`app.js:1781`, `:1888-1889`) is false for Cubase Realtime Export. Quick Help and the manual correctly say "normal export".
2. The manual's "MATCH switches itself off when you close Polisher and during export" (`polisher-manual.md:335`) is false after a project reload (A2).
3. The manual says a solo plays "at its real level in the master" and that each change gets "the same Drive and limiting". Both are false whenever Clip Level is below 0 (A3).
4. Undo labels for dB controls show the change but read like a value:
   - "Ceiling −0.9 dB" means −1.0 → −1.9.
   - "Dynamics Threshold 1 −14.4 dB" means −20 → −34.4.
   - Dynamics bands are numbered 1–4 where the UI says LOW / LO-MID / HI-MID / HIGH, and the label says "Clipper Level" where the knob says "Clip Level".
5. PHONE Learn says "almost nothing below 300 Hz". Measured: −6.3 dB at 200 Hz, −10.8 dB at 150 Hz, −17.6 dB at 100 Hz. That is gentler than a real phone, so the 808 check is generous.
6. Maximizer Learn: "+3.7 dB louder than your mix (−13.7 LUFS integrated)". The bracket is the master's Integrated but reads as the mix's.
7. Stem EQ Learn: "it hears of what you hear, vocals 38 %…" is broken grammar.
8. Snapshot TRY THIS says "play your chorus, run Auto-Master", which is the wrong order for the whole-song listen. That page also leaves out that snapshots keep Tone Steady's target.
9. The banner cuts off "— not the master": Dynamics LISTEN overflows by 45 px, and Clipper, Tamer, Maximizer and Dynamics with PHONE overflow by 20–111 px. At 800×450 all text is drawn at half size, about 6 px.

### C. Test gaps

- Listen and Solo with Clip Level below 0.
- MATCH surviving a save/load.
- Project marked changed after snapshot store or reference load.
- Zero-sample process calls.
- Mono 1-in/1-out bus layout (it works; nothing tests it).
- Listen alignment is only tested at 48 kHz.
- The PHONE safety stage: the click test's 200 Hz tone never triggers it.
- UI: Space/Enter on focused buttons, visual overflow (the smoke test only checks `textContent`), the 800×450 window, lost pointer capture, EQ double-click step count.
- Snapshot recall of a slot missing a control.

### D. Verified OK

- Chain order matches the docs.
- Reported latency equals the real delay at 44.1–192 kHz, including Bass-only saturation, Unlimiter Kick and Keep Loudness off (error ≤ −218 dB).
- Offline export equals real time exactly with every new feature on, including 3000-sample blocks. Listen/MONO/PHONE never reach an offline export.
- Listen stays aligned at 44.1, 96, 176.4 and 192 kHz: Tamer 1e-8, Maximizer 8e-8, Dynamics solo sum 6e-8.
- No memory allocation on the audio thread, including Focus/Keep toggling, tone-target set/clear, every listen mode, PHONE, and 4096-sample host blocks after a 512 prepare.
- An old v0.15.0 state loads with Keep Loudness 0, Focus Full, no target, empty slots and a cleared undo list; a current state with Keep Loudness on loads with it on.
- Mono bus layout runs finite in every listen and check mode.
- Swapping or clearing Tone Steady's target mid-song (via undo, recall or CLEAR) doesn't click.
- PHONE and MONO switch-off doesn't click (≤ 0.08 dB).
- The Learn panel keeps its scroll position through the 1-second redraws.
- No script errors at 1280×720 or 800×450.