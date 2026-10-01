# Polisher — AI mastering plug-in

Producer-minded mastering suite with an **Auto-Master** button and a **Learn** mode that explains
every setting. Style: red / white / powder-blue liquid glass.

Formats: **VST3** and **Standalone** on macOS and Windows, plus **AU** on macOS.

## Status — v0.10

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
| Match ref (reference-track matching) | Planned |

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
plugin/     JUCE AudioProcessor, web-view editor (+ basic native fallback editor)
  ui/       The liquid-glass interface: index.html, styles.css, app.js, images, fonts (OFL).
            Browser preview: copy JUCE's modules/juce_gui_extra/native/javascript/*.js into
            plugin/ui/juce/, run `python3 -m http.server` in plugin/ui and open
            http://localhost:8000 (mock.js simulates the plug-in and audio).
tests/      Catch2 unit tests for the engine
.github/workflows/build.yml   macOS + Windows build, tests, pluginval, auval, artifacts
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

Every push runs the GitHub Actions workflow: builds on macOS (universal arm64 + x86_64) and Windows,
runs the unit tests, validates the VST3 with pluginval (strictness 5) and the AU with `auval`, and
uploads the built plug-ins as downloadable artifacts.

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

## Signing and release (later)

- macOS: Apple Developer ID certificate + notarization in CI.
- Windows: code-signing certificate (optional but reduces SmartScreen warnings).
- AAX (Pro Tools): Avid developer account + PACE signing tools.
