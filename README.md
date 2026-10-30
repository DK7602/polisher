# Masterology — AI mastering plug-in

(Formerly called **Polisher**; renamed in v0.25.0. The repository, code names and plug-in ID keep
"polisher", so projects saved with Polisher open with Masterology.)

Producer-minded mastering suite with an **Auto-Master** button and a **Learn** mode that explains
every setting. Style: red / white / powder-blue liquid glass.

Formats: **VST3** on macOS and Windows, plus **AU** on macOS (the Sonicology installer ships these; the
individual download also has a **Standalone** app and **Masterology Blind Test**).

**Install.** The Sonicology installer (built in the Mixology repo: Actions > Sonicology suite) installs all three
plug-ins on Windows or Mac and removes an old Polisher. By hand: the Masterology download from Build & Test, with
`docs/INSTALL.txt`.

**System requirements.** Windows 10 or 11 (64-bit) or macOS 11+. On Windows the Masterology screen runs on
the **Microsoft Edge WebView2 Runtime**: it ships with Windows 11 and comes with Microsoft Edge on
Windows 10. Without it Masterology still processes audio but opens a basic view (Maximizer controls and
meters only) with a button to Microsoft's free download page; install it and reopen Masterology.

User manual: [docs/manual/polisher-manual.md](docs/manual/polisher-manual.md) (also inside the plug-in: Quick Help > Open the full manual).
A live copy is kept in Claude Docs (project 3bf99dd7-1111-4be3-884d-d787ae49506c, node 15637893-1eb5): update it
whenever the manual changes.

## Status — v0.29.6

| Piece | State |
| --- | --- |
| Loudness meter (ITU-R BS.1770-4 / EBU R128): momentary, short-term, integrated, LRA, true peak | Done, tested against EBU Tech 3341/3342 cases |
| Maximizer: 4x oversampled true-peak look-ahead limiter, adaptive release | Done, ceiling verified under heavy drive |
| Auto-Master loudness step: finds the drive that lands a mix on a LUFS target | Done, reports unreachable targets honestly |
| Plug-in shell (JUCE 8): parameters, state save, latency reporting | Done |
| Liquid-glass UI (WebView): live Maximizer knobs, R128 meters, real-time spectrum, live Learn panel | Done (v0.2) |
| Soft Clipper: 1x–8x linear-phase oversampling, soft knee, constant latency, live UI + Learn | Done (v0.3) |
| Input loudness meter, "louder than your mix" in Learn, loudness restarts after a control change | Done (v0.3.2) |
| Tonal EQ: 7 musical bands, drag points on the spectrum, click-free glides, bit-exact when flat, live Learn | Done (v0.4) |
| Bass Control: Mono Below, Sub Tame (dynamic 808 leveller), Punch, bass-width meter, live Learn | Done (v0.5) |
| Auto-Master: listens 12 s, analyses tone vs genre, bass width, 808 jumps, kick punch and loudness; sets every module; explains each choice; undo | Done (v0.6) |
| Level-matched A/B (MATCH): the master plays at the original's loudness; off during a normal export, when the window closes and when a project opens | Done (v0.7) |
| Stereo Imager: width for lows / mids / highs (side only, LR4 crossovers), correlation meter, Learn | Done (v0.8) |
| Auto-Master recognises a single instrument (one band far above any genre balance), changes nothing and says to put Masterology on the master | Done (v0.8.1) |
| MATCH never boosts (the louder side is turned down) and gives a clipping original headroom, so A doesn't distort the interface | Done (v0.8.2) |
| Dynamics: 4-band compressor (LR4 splits, zero latency, per-band threshold / ratio / attack / release / gain, program-dependent release, per-band reduction meters, Learn) | Done (v0.9) |
| Dynamics Impact: per-band transient shaping (−100…+100 %), loudness kept per band, steady sound untouched | Done (v0.17) |
| Glue (module 07): one bus compressor for the whole mix in three vintage flavours (Bus / Opto / Tube: knee, detector, timing), Ignore Lows sidechain high-pass, Auto release, parallel Mix, Gain, zero latency, exact pass-through at 1:1 | Done (v0.24) |
| Suite link: Masterology tells Mixology its goal (target, ceiling, genre); Mixology's hand-off (style, peaks, loudness, tone, mono, its limiter) shows in Learn as FROM MIXOLOGY with a one-click matching genre (`dsp/include/suite/SuiteLink.h`, the same file in Voxology and Mixology) | Done (v0.26) |
| Auto Analyze: after Auto-Master, keeps listening while the song plays; when it hears the hook again and the mix changed, it re-runs Auto-Master in the background and offers per-module edits in Learn (now → new, APPLY / KEEP MINE, one undo step each), keeping the user's own knob offsets (`dsp/include/polisher/AutoAnalyze.h`) | Done (v0.27) |
| One song style for the trio: Mixology's 75 styles (`plugin/ui/mixstyles.js`, the same file in all three) in a picker on the genre pill; pick it in any of the three and the others follow (suite link layout 2, the newest pick wins, saved with the project); the style sets Auto-Master's genre | Built (v0.28), waiting for the Cubase test |
| Your DAW, shared by the trio: Settings > DAW (found from the host, or picked; suite link layout 3); every tip in Learn says how in that DAW (`plugin/ui/daws.js`, the same file in all three; `{daw:...}` tokens in the engine's text) | Built (v0.29), waiting for the test |
| New name and logo: Polisher is now **Masterology** (plug-in name, header logo, manual, Blind Test; vendor "Sonicology" since v0.29). Same plug-in ID, so old projects and presets load | Done (v0.25) |
| Shoot-out kit: Blind Test checks the masters were made at the same loudness (names the one to push and by how much) and and the same ceiling (names the file to change and by how much, matched to masterology.wav), and adds measurements to the answer key (kick / snare punch, range, width, bass width, 6-band tone balance); manual guide for Masterology vs Ozone 12 / TDR Limiter 6 GE / LoudMax | Done (v0.23) |
| Mid / side on Dynamics (Channels: Stereo / Mid only / Side only, all bands, click-free switch) and Saturation (Channels: Stereo / Mid / Side, works with Focus) | Done (v0.22) |
| Saturation Focus Highs only (sheen above 4 kHz) and Bass + Highs (808 + hats, mids clean), each with its own split and oversampling | Done (v0.21) |
| Maximizer Bass First (808-aware limiting: Linkwitz-Riley split at 120 Hz, low band takes the reduction first, park-and-glide switching) and Dither (TPDF, 16 / 24-bit) | Done (v0.20) |
| Tonal EQ Stereo / Mid / Side per band (glides on change, mono-safe) | Done (v0.19) |
| Dynamics Sustain: per-band tail length (−100…+100 %, the 808-length control), shares Impact's loudness keeping | Done (v0.18) |
| Saturation: tape / tube / clip, 4x oversampled (aliasing 74–119 dB below full scale: 119 dB on a single tone, about 74 dB with hot highs at full Drive), Drive, Mix, harmonics meter, exact pass-through at Drive 0, Learn | Done (v0.9) |
| Auto-Master sets Dynamics (per-band glue, thresholds found by running the compressor on your mix), Stereo Imager (opens narrow highs, never the lows, mono-safe) and Saturation (mode by genre, Drive found by measuring the harmonics against a target) | Done (v0.10) |
| Stem EQ: per-stem level for vocals / drums / bass / other from a small built-in model (64 bands, ~40 ms latency, plain C++ inference), exact pass-through at 0 dB; Auto-Master suggests a vocal move | Done (v0.11) |
| Tone Steady (keeps the tonal balance steady through the song) and Resonance Tamer (cuts ringing peaks), inside the Stem EQ's spectral stage (no extra latency); set by Auto-Master | Done (v0.12) |
| Unlimiter: puts back the punch a limiter squashed out of a beat (only on squashed audio); set by Auto-Master | Done (v0.13) |
| Validation in CI: pluginval at strictness 10 plus Steinberg's VST3 validator (537/537) | Done (v0.13) |
| Unlimiter's on/off switch fixed (it bypassed the whole chain); every module starts in its saved state on load | Done (v0.14) |
| Input Trim: Auto-Master turns a mix that is already louder than the target down before the chain, then sets every module for that level | Done (v0.14) |
| Validation: independent 16x true-peak checks, plug-in-level tests (block sizes, offline, latency, allocation, state), sanitizers, UI test, blind-test tool | Done (v0.14) |
| Stem EQ model v3: trained on all of MUSDB with augmentation and synthetic trap 808s; as good as v2 on ordinary songs, 3-5 dB better on the Bass knob when an 808 plays, beats the best fixed EQ on every test (tools/stem/README.md) | Done (v0.14.1) |
| Module-by-module audit fixes: Imager and Mono Below keep panned sounds in place; click-free Clipper; no Tube thump; Punch ignores 808 notes; Sub Tame and Tone Steady leave the first notes and a drop alone; consistent, band-only Unlimiter that Auto-Master only keeps when the punch survives the Maximizer; matched-magnitude EQ with a subsonic filter and band-overlap-aware Auto-Master gains; Stem EQ at 176.4/192 kHz; host bypass = latency-matched A | Done (v0.15) |
| Stem EQ Keep Loudness: moves change the balance, not the loudness (K-weighted, ~2 s, ±12 dB, inside the same STFT: no extra latency); on for new instances, off for songs saved before it; live "Keeping level" readout | Done (after v0.15.0) |
| Match ref: load a released song (WAV / AIFF / FLAC / MP3); its loudest 30 s is measured (tone, width, low end; loudness from its loudest 12 s, like your song) and saved with the project (no audio); Auto-Master aims for it instead of the genre (part of the way, same limits), says honestly when it can't get as loud, and Learn compares reference / mix / master; dashed reference curve on the spectrum | Done (v0.15) |
| Saturation Focus "Bass only" (808 to phone): only the low end (LR4 below 150 Hz) feeds the curve, its added harmonics are high-passed at 70 Hz and mixed onto the untouched dry signal, so the 808 gets harmonics phone speakers can play while vocals and hats stay clean (a 2 kHz tone at full Drive: THD -156 dB vs -13 dB in Full); Full is bit-for-bit the old sound; click-free switching; Auto-Master picks it for Trap and R&B | Done (after v0.15.0) |
| Undo / redo and snapshots: a linear history of the last 100 changes kept in the plug-in (one knob gesture = one step, quick repeats merged; Auto-Master, Stem EQ Apply and a recall are one step each and carry the report; host automation and A/B, MATCH, listening aids never recorded; not saved), arrows with "Undo: Drive +2.0 dB" labels and Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y; snapshots S1–S4 of every setting plus its report, saved with the project, click-free recall, "changed since" mark, MATCH remembers each snapshot's level for fair flips | Done (after v0.15.0) |
| Listening aids (monitoring only, never saved, never in an offline export, off when the window closes, red banner while on): Listen buttons that play only what Saturation adds / the Soft Clipper, Resonance Tamer, Maximizer and a Dynamics band take off (sample-aligned, at their level in the master, quiet ones turned up with the amount shown), Dynamics band solos, MONO and PHONE (mono, ~300 Hz–8 kHz) checks next to MATCH; click-free switching | Done (after v0.15.0) |
| Whole-song Auto-Master: press it and play the song; it keeps listening (up to 15 min, fixed memory: the loudest 12 s of audio in 100 ms pieces that change hands without copying, plus 100 ms loudness and 1 s tone readings; allocation-free on the audio thread) and finishes by itself when the song ends (4 s without music after 30 s of it), or on FINISH from 12 s on; decides from the loudest 12 s; report: "Listened to 2:47 of your song; the loudest part (0:54–1:06) set the levels", whole-song LUFS / LRA | Done (after v0.15.0) |
| Tone Steady stored target: the song's own tone (median of 1 s readings per band, learned by Auto-Master or Tone Steady's LEARN, saved with the project, follows the Tonal EQ); held to it, preview and export match from any start point (measured: 9e-7 vs 2e-3 following the music; quiet passages judged against the song's stored level); an 808 entering after an intro isn't cut, and sections without it aren't touched; a part entering above the bass is cut at most half the Range; a short Auto-Master listen keeps a stored tone only when it matches; "another song?" warning when the music sits far from it; CLEAR / old songs: follows the music as before | Done (after v0.15.0) |
| Spectrum genre line and loudness history: a dotted "Trap target" (etc.) line on the spectrum is the balance Auto-Master's EQ aims for (computed by the engine from the genre's band norms: a smooth curve that measures exactly those norms, drawn as the display would draw a steady master with it, moved to the loudness playing; hidden while a reference is loaded, legend click shows it); HISTORY view: the master's and the mix's short-term loudness over the last 2 minutes of playback with the target line and the Maximizer's gain reduction (orange past 6 dB), pauses when the transport stops, RESET clears it; Learn reads both | Done (after v0.15.0) |
| Re-audit of every module after the nine features (5 measured audits, reports in claude/reaudit-2026-10-10): MATCH relearns in ~3 s and is off on project load; clean B->A switch; Mono Below / Imager / Dynamics switch without holes (park and glide); Saturation Mode cross-fades; Tone Steady target leaves sections without the 808 alone; Keep Loudness follows section changes; Model Hears LUFS-weighted; reference matched hook against hook; honest report levels; Space key safe; project marked changed on snapshot / reference | Done (v0.16) |

## Validation

- **Engine tests** (`tests/`, Catch2): every module's exact pass-through at neutral, its behaviour, glides and
  stability; EBU Tech 3341 / 3342 loudness cases; the true-peak meter and the Maximizer checked by an
  independent 16x reference meter (`tests/ReferenceTruePeak.h`): the Maximizer holds its ceiling on tones up to
  20 kHz at 44.1-192 kHz; on hard-clipped audio it sits at the ceiling by BS.1770 (what streaming services
  read) and within ~0.9 dB of it on an ideal wideband reconstruction, which is why the default ceiling is
  -1 dBTP.
- **Plug-in tests** (`tests/plugin/`): the real processor as a DAW drives it: identical output at every
  block size (1 to 4096, and random sizes), offline export = real time (MATCH never reaches an export),
  reported latency = real delay at 44.1-192 kHz and the same in bypass, a sample-rate change starts like a
  fresh plug-in, no memory allocation on the audio thread, saved state restores every control, every control
  jumping at random never gives NaN or passes the ceiling, double precision = single precision; undo / redo
  (one step per gesture, host automation never recorded, the 100-step cap, Auto-Master undone exactly) and
  snapshots (exact recall, saved with the project, A/B and MATCH untouched, click-free, MATCH's per-snapshot level).
- **Sanitizers** (CI, Linux; pull requests and manual runs): the engine tests under AddressSanitizer + UndefinedBehaviorSanitizer.
- **UI smoke test** (CI, `tests/ui/`): the web UI in headless Chromium: every module's on/off switch, A / B,
  host changes, undo / redo (buttons, labels, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y), snapshots, no script errors.
- **pluginval** at strictness 10 + Steinberg's VST3 validator (CI, Windows).
- **Listening**: `MasterologyBlindTest` (built with the plug-in) turns exported masters into a loudness-matched,
  shuffled blind test with a score sheet and a separate answer key with a fairness check and measurements (see the
  manual's Blind test and Shoot-out sections).

## Learn rule

Every warning in Learn and in the Auto-Master report carries a plan: what is happening, **Do I need
to fix it?** (a straight answer: yes, only if..., optional, or no) and **How** (numbered steps naming
the exact control and value). Auto-Master notes encode it as `NEED:` / `STEP:` lines; a test enforces it.

## Layout

```
dsp/        Framework-free C++20 engine (no JUCE): unit-testable, reusable offline
  include/polisher/
    KWeighting.h         BS.1770 K-weighting filter
    TruePeakDetector.h   4x polyphase inter-sample peak detector
    LoudnessMeter.h      R128 meter with gated histograms (never allocates on the audio thread)
    TruePeakLimiter.h    Maximizer core
    TonalEQ.h            7-band mastering EQ (first in the chain, zero latency)
    BassControl.h        Mono Below, Sub Tame, Punch, bass-width measurement
    AutoMaster.h         Mix analysis + decisions + plain-language reasons (framework-free, tested)
    MultibandDynamics.h  4-band mastering compressor
    StereoImager.h       3-band width (side only)
    Saturation.h         Oversampled tape / tube / clip saturation
    SoftClipper.h        Oversampled soft-knee clipper (drive -> clipper -> limiter)
    LoudnessTargeting.h  Offline render + drive search used by Auto-Master
    LevelMatch.h         MATCH: loudness-matched, clip-free A/B gains
    StemEQ.h             Stem EQ + Tone Steady + Resonance Tamer (one STFT stage, model in StemNetWeights.h)
    SongListener.h       Auto-Master's whole-song listen: loudest part, song loudness, the song's tone
    Unlimiter.h          Restores hit contrast on squashed audio
    Monitoring.h         Listening aids: Listen / Solo switch (ListenMix), MONO / PHONE checks (MonitorOutput)
plugin/     JUCE AudioProcessor, web-view editor (+ basic native fallback editor when no web view exists);
            SettingsHistory.h: undo / redo history and snapshots S1-S4 (kept in the processor)
  ui/       The liquid-glass interface: index.html, styles.css, app.js, images, fonts (OFL).
            Browser preview: copy JUCE's modules/juce_gui_extra/native/javascript/*.js into
            plugin/ui/juce/, run `python3 -m http.server` in plugin/ui and open
            http://localhost:8000 (mock.js simulates the plug-in and audio).
tests/      Catch2 unit tests for the engine
docs/manual/  User manual (bundled into the plug-in; its version line must match the release)
tools/stem/   Stem EQ model training (Python / PyTorch; see tools/stem/README.md)
tools/blindtest/  Masterology Blind Test: loudness-matched, shuffled listening test from exported songs
.github/workflows/build.yml   Windows (every push) and macOS (manual run) builds, tests, validation, artifacts
```

## Build

Requirements: CMake 3.25+, a C++20 compiler (Xcode 15+ on macOS, Visual Studio 2022 on Windows), Git.
JUCE 8 and Catch2 are downloaded automatically on first configure.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Plug-ins land in `build/plugin/Polisher_artefacts/Release/{VST3,AU,Standalone}`.

Engine and tests only (any OS, no JUCE download):

```bash
cmake -S . -B build-tests -DPOLISHER_BUILD_PLUGIN=OFF && cmake --build build-tests && ./build-tests/tests/polisher_tests
```

Meter behaviour: the loudness meter resets automatically when the host transport starts ("Reset on play", on by default).

Every push runs the GitHub Actions workflow on Windows (build, unit tests, pluginval at strictness 10 with
Steinberg's VST3 validator, and the download Masterology-Windows: the plug-in, Standalone, Blind Test, the manual
and INSTALL.txt) and the engine tests under ASan + UBSan. macOS (universal arm64 + x86_64, VST3 / AU / Standalone,
pluginval + `auval`, packed as a .tar.gz so the programs keep their permission to run) builds on a manual run:
Actions > Build & Test > Run workflow.

## Engine notes

- **Metering accuracy.** Integrated loudness and LRA use 0.02 LU histograms that also keep exact energy
  sums, so memory stays constant; the EBU reference cases pass within 0.1 LU.
- **True peak.** 4x oversampling (Kaiser-windowed sinc, 25 taps per phase); the true-peak value never
  reads below the sample peak.
- **Limiter guarantee.** Sliding minimum over the look-ahead window followed by a moving average of the same
  length means the gain has fully reached its target by the time a peak leaves the delay line. Latency is
  3 ms + 12 samples and is reported to the host. "At or under the ceiling" is on the BS.1770 (4x) true-peak
  meter streaming services use; an independent 16x meter can read up to ~0.7 dB higher on clipped audio.
  Switching to bypass (A) keeps limiting what is in the delay line, the block the switch came with and
  50 ms more (fast release), then glides to unity: an A/B flip never lets a driven, unlimited peak out.
- **Adaptive release.** Dips below the ~400 ms average gain (transients) recover fast; the program-level
  gain moves slowly, so bass-heavy material doesn't pump.
- **Listening aids.** Monitoring only, outside the parameter tree (the editor sets them; they reset when it
  closes; `isNonRealtime()` bypasses them, so an offline export is the master bit for bit). A Listen signal is
  formed where its module sits (Saturation: output minus input delayed by its latency; Clipper: input x the
  clipper's clean gain minus output; Maximizer: aligned input minus output; Resonance Tamer: a second
  overlap-add inside the STFT with X x (1 - tamer gain); Dynamics: one band, or b x (1 - reduction)), given
  Drive and the limiter's per-sample gain and delayed to the output, so it is sample-aligned with the master
  (tests: delta + master = the right reference to float precision). Only one is computed at a time; 15 ms
  S-curve fades, the next one arms (fills its delay lines, measures its level for 0.1 s) before it fades in.
- **Loudness targeting.** A secant search on the real limiter output converges in a few passes. When a target
  can't be reached with limiting alone, it says so (Auto-Master will then add the clipper and saturation
  stages, and Learn will explain why).

## Signing and release (not done yet)

- macOS: Apple Developer ID certificate + notarization in CI.
- Windows: code-signing certificate (optional but reduces SmartScreen warnings).
- AAX (Pro Tools): Avid developer account + PACE signing tools.
