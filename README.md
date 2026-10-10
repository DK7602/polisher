# Polisher — AI mastering plug-in

Producer-minded mastering suite with an **Auto-Master** button and a **Learn** mode that explains
every setting. Style: red / white / powder-blue liquid glass.

Formats: **VST3** and **Standalone** on macOS and Windows, plus **AU** on macOS.

**System requirements.** Windows 10 or 11 (64-bit) or macOS 11+. On Windows the Polisher screen runs on
the **Microsoft Edge WebView2 Runtime**: it ships with Windows 11 and comes with Microsoft Edge on
Windows 10. Without it Polisher still processes audio but opens a basic view (Maximizer controls and
meters only) with a button to Microsoft's free download page; install it and reopen Polisher.

User manual: [docs/manual/polisher-manual.md](docs/manual/polisher-manual.md) (also inside the plug-in: Quick Help > Open the full manual).

## Status — v0.15.0

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
| Level-matched A/B (MATCH): the master plays at the original's loudness; off during export and when the window closes | Done (v0.7) |
| Stereo Imager: width for lows / mids / highs (side only, LR4 crossovers), correlation meter, Learn | Done (v0.8) |
| Auto-Master recognises a single instrument (one band far above any genre balance), changes nothing and says to put Polisher on the master | Done (v0.8.1) |
| MATCH never boosts (the louder side is turned down) and gives a clipping original headroom, so A doesn't distort the interface | Done (v0.8.2) |
| Dynamics: 4-band compressor (LR4 splits, zero latency, per-band threshold / ratio / attack / release / gain, program-dependent release, per-band reduction meters, Learn) | Done (v0.9) |
| Saturation: tape / tube / clip, 4x oversampled (aliasing 119 dB down), Drive, Mix, harmonics meter, exact pass-through at Drive 0, Learn | Done (v0.9) |
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
| Match ref (reference-track matching) | Planned |

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
  jumping at random never gives NaN or passes the ceiling, double precision = single precision.
- **Sanitizers** (CI, Linux; pull requests and manual runs): the engine tests under AddressSanitizer + UndefinedBehaviorSanitizer.
- **UI smoke test** (CI, `tests/ui/`): the web UI in headless Chromium: every module's on/off switch, A / B,
  host changes, no script errors.
- **pluginval** at strictness 10 + Steinberg's VST3 validator (CI, Windows).
- **Listening**: `PolisherBlindTest` (built with the plug-in) turns exported masters into a loudness-matched,
  shuffled blind test with a score sheet and a separate answer key (see the manual).

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
    Unlimiter.h          Restores hit contrast on squashed audio
plugin/     JUCE AudioProcessor, web-view editor (+ basic native fallback editor when no web view exists)
  ui/       The liquid-glass interface: index.html, styles.css, app.js, images, fonts (OFL).
            Browser preview: copy JUCE's modules/juce_gui_extra/native/javascript/*.js into
            plugin/ui/juce/, run `python3 -m http.server` in plugin/ui and open
            http://localhost:8000 (mock.js simulates the plug-in and audio).
tests/      Catch2 unit tests for the engine
docs/manual/  User manual (bundled into the plug-in; its version line must match the release)
tools/stem/   Stem EQ model training (Python / PyTorch; see tools/stem/README.md)
tools/blindtest/  Polisher Blind Test: loudness-matched, shuffled listening test from exported songs
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

Every push runs the GitHub Actions workflow on Windows: build, unit tests, pluginval at strictness 10
(the maximum) with Steinberg's VST3 validator, and the built plug-in as a downloadable artifact
(Polisher-Windows). macOS (universal arm64 + x86_64, VST3 / AU / Standalone, pluginval + `auval`) builds
on a manual run: Actions > Build & Test > Run workflow, "Also build macOS".

## Engine notes

- **Metering accuracy.** Integrated loudness and LRA use 0.02 LU histograms that also keep exact energy
  sums, so memory stays constant; the EBU reference cases pass within 0.1 LU.
- **True peak.** 4x oversampling (Kaiser-windowed sinc, 25 taps per phase); the true-peak value never
  reads below the sample peak.
- **Limiter guarantee.** Sliding minimum over the look-ahead window followed by a moving average of the same
  length means the gain has fully reached its target by the time a peak leaves the delay line. Latency is
  3 ms + 12 samples and is reported to the host.
- **Adaptive release.** Dips below the ~400 ms average gain (transients) recover fast; the program-level
  gain moves slowly, so bass-heavy material doesn't pump.
- **Loudness targeting.** A secant search on the real limiter output converges in a few passes. When a target
  can't be reached with limiting alone, it says so (Auto-Master will then add the clipper and saturation
  stages, and Learn will explain why).

## Signing and release (not done yet)

- macOS: Apple Developer ID certificate + notarization in CI.
- Windows: code-signing certificate (optional but reduces SmartScreen warnings).
- AAX (Pro Tools): Avid developer account + PACE signing tools.
