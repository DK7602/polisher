# Polisher — AI mastering plug-in

Producer-minded mastering suite with an **Auto-Master** button and a **Learn** mode that explains
every setting. Style: red / white / powder-blue liquid glass.

Formats: **VST3** and **Standalone** on macOS and Windows, plus **AU** on macOS.

## Status — v0.1 (engine milestone)

| Piece | State |
| --- | --- |
| Loudness meter (ITU-R BS.1770-4 / EBU R128): momentary, short-term, integrated, LRA, true peak | Done, tested against EBU Tech 3341/3342 cases |
| Maximizer: 4x oversampled true-peak look-ahead limiter, adaptive release | Done, ceiling verified under heavy drive |
| Auto-Master loudness step: finds the drive that lands a mix on a LUFS target | Done, reports unreachable targets honestly |
| Plug-in shell (JUCE 8): parameters, state save, latency reporting, engineering UI | Done |
| Liquid-glass UI (WebView) | Next |
| Tonal EQ, Bass Control, Dynamics, Imager, Saturation, Soft Clipper | Next |
| Analysis engine + Learn explanations | Next |

## Layout

```
dsp/        Framework-free C++20 engine (no JUCE): unit-testable, reusable offline
  include/polisher/
    KWeighting.h         BS.1770 K-weighting filter
    TruePeakDetector.h   4x polyphase inter-sample peak detector
    LoudnessMeter.h      R128 meter with gated histograms (never allocates on the audio thread)
    TruePeakLimiter.h    Maximizer core
    LoudnessTargeting.h  Offline render + drive search used by Auto-Master
plugin/     JUCE AudioProcessor + editor
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
