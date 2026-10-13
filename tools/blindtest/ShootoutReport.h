#pragma once

// Measurements for the blind test's answer key: what differs between the masters, in numbers, so a
// listening result can be traced to a cause (less punch, more bass, wider...). Free of JUCE so the unit
// tests can check it. Everything is measured on the files as exported (before the test turns anything
// down); the tone balance is relative, so it doesn't depend on loudness at all.

#include "polisher/Biquad.h"
#include "polisher/FilterDesign.h"
#include "polisher/LoudnessMeter.h"
#include "polisher/LoudnessTargeting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace blindtest {

inline constexpr int kBands = 6;
inline constexpr std::array<double, kBands + 1> kBandEdgesHz { 0.0, 60.0, 200.0, 800.0, 3000.0, 8000.0, 0.0 };   // 0 = open end
inline constexpr double kBassWidthHz = 120.0;
inline constexpr double kKickHz = 150.0;                                  // kick punch: below this
inline constexpr double kSnareLowHz = 1000.0, kSnareHighHz = 5000.0;      // snare punch: this band
inline constexpr double kBlockSeconds = 0.4;                              // punch is read in blocks this long
inline constexpr double kLoudPartDb = 8.0;                                // ... within this of the loudest block

struct Report
{
    double lufs = -INFINITY, truePeakDbtp = -INFINITY, rangeLu = 0.0;
    // Punch: how far the hits stick out above their own band's level (peak - RMS in 0.4 s blocks, median
    // over the loud part). Read per band, so a broadband ceiling (-1.0 vs -1.5, true peak or not) doesn't
    // set it: a master that keeps its drums gets a higher number than one that squashes them.
    double kickPunchDb = -INFINITY, snarePunchDb = -INFINITY;
    std::array<double, kBands> bandDb {};  // each band's level against the loudness (LUFS), dB
    double widthDb = -INFINITY;            // side vs mid power, dB (-inf = no side at all)
    double bassWidthDb = -INFINITY;        // the same below kBassWidthHz
};

namespace detail {

inline double powerDb (double p) { return p > 1.0e-20 ? 10.0 * std::log10 (p) : -INFINITY; }

/** A Linkwitz-Riley 4th-order low- or high-pass (two Butterworths in series). */
struct Lr4
{
    polisher::Biquad a, b;
    void set (bool highPass, double hz, double sr) noexcept
    {
        polisher::design::apply (a, polisher::design::butterworth (highPass, hz, sr));
        polisher::design::apply (b, polisher::design::butterworth (highPass, hz, sr));
    }
    double process (double x) noexcept { return b.process (a.process (x)); }
};

/** One band's punch over blocks: peak and power per block. */
struct Crest
{
    std::vector<double> peak, power;
    double blockPeak = 0.0, blockPower = 0.0;
    void add (double v) noexcept { blockPeak = std::max (blockPeak, std::abs (v)); blockPower += v * v; }
    void endBlock (int length)
    {
        peak.push_back (blockPeak);
        power.push_back (blockPower / std::max (1, length));
        blockPeak = blockPower = 0.0;
    }
    double median (const std::vector<size_t>& loud) const
    {
        std::vector<double> crest;
        for (size_t b : loud)
            if (power[b] > 1.0e-20 && peak[b] > 0.0)
                crest.push_back (20.0 * std::log10 (peak[b]) - 10.0 * std::log10 (power[b]));
        if (crest.empty()) return -INFINITY;
        std::nth_element (crest.begin(), crest.begin() + static_cast<long> (crest.size() / 2), crest.end());
        return crest[crest.size() / 2];
    }
};

} // namespace detail

/** Measures one exported song (one or two channels; a mono file counts as the same on both sides). */
inline Report analyse (const polisher::AudioClip& clip)
{
    Report r;
    r.bandDb.fill (-INFINITY);
    const int n = clip.numSamples(), nch = std::min (clip.numChannels(), 2);
    if (n == 0 || nch == 0)
        return r;
    const double sr = clip.sampleRate;
    const auto& left = clip.channels[0];
    const auto& right = clip.channels[static_cast<size_t> (nch - 1)];

    const auto stats = polisher::measure (clip);
    r.lufs = stats.integratedLufs;
    r.truePeakDbtp = stats.truePeakDbtp;
    r.rangeLu = stats.loudnessRangeLu;

    // One pass: the tone bands on both channels, mid / side (and in the bass), and the kick and snare
    // bands of the mid for punch.
    std::array<std::array<std::array<detail::Lr4, 2>, 2>, kBands> bandFilters {};   // [band][channel][0 = high-pass, 1 = low-pass]
    for (size_t k = 0; k < kBands; ++k)
        for (auto& f : bandFilters[k])
        {
            if (kBandEdgesHz[k] > 0.0) f[0].set (true, kBandEdgesHz[k], sr);
            if (kBandEdgesHz[k + 1] > 0.0) f[1].set (false, kBandEdgesHz[k + 1], sr);
        }
    detail::Lr4 bassMid, bassSide, kickLp, snareHp, snareLp;
    bassMid.set (false, kBassWidthHz, sr);
    bassSide.set (false, kBassWidthHz, sr);
    kickLp.set (false, kKickHz, sr);
    snareHp.set (true, kSnareLowHz, sr);
    snareLp.set (false, kSnareHighHz, sr);
    detail::Crest kick, snare;
    std::vector<double> blockPower;
    double broadband = 0.0;
    const int block = std::max (1, static_cast<int> (std::lround (kBlockSeconds * sr)));
    std::array<double, kBands> bandPower {};
    double midPower = 0.0, sidePower = 0.0, bassMidPower = 0.0, bassSidePower = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x[2] = { left[static_cast<size_t> (i)], right[static_cast<size_t> (i)] };
        const double m = 0.5 * (x[0] + x[1]), s = 0.5 * (x[0] - x[1]);
        midPower += m * m;
        sidePower += s * s;
        const double bm = bassMid.process (m), bs = bassSide.process (s);
        bassMidPower += bm * bm;
        bassSidePower += bs * bs;
        for (size_t k = 0; k < kBands; ++k)
            for (size_t c = 0; c < 2; ++c)
            {
                auto& f = bandFilters[k][c];
                double v = x[c];
                if (kBandEdgesHz[k] > 0.0) v = f[0].process (v);
                if (kBandEdgesHz[k + 1] > 0.0) v = f[1].process (v);
                bandPower[k] += v * v;
            }
        kick.add (kickLp.process (m));
        snare.add (snareLp.process (snareHp.process (m)));
        broadband += x[0] * x[0] + x[1] * x[1];
        const int done = i + 1;
        if (done % block == 0 || done == n)
        {
            const int length = done % block == 0 ? block : done % block;
            if (length == block || blockPower.empty())   // a short last block would skew the median: dropped
            {
                kick.endBlock (length);
                snare.endBlock (length);
                blockPower.push_back (broadband / length);
            }
            broadband = 0.0;
            kick.blockPeak = kick.blockPower = snare.blockPeak = snare.blockPower = 0.0;
        }
    }
    // Punch over the loud part (the hook, the drop), so quiet intros and breaks don't count.
    double top = 0.0;
    for (double p : blockPower) top = std::max (top, p);
    std::vector<size_t> loud;
    for (size_t b = 0; b < blockPower.size(); ++b)
        if (top > 0.0 && blockPower[b] >= top * std::pow (10.0, -kLoudPartDb / 10.0))
            loud.push_back (b);
    r.kickPunchDb = kick.median (loud);
    r.snarePunchDb = snare.median (loud);

    // Tone: each band's level against the loudness, as the loudness-matched test plays it.
    for (size_t k = 0; k < kBands; ++k)
        r.bandDb[k] = std::isfinite (r.lufs) ? detail::powerDb (bandPower[k] / n) - r.lufs : -INFINITY;
    r.widthDb = midPower > 0.0 ? detail::powerDb (sidePower / midPower) : -INFINITY;
    r.bassWidthDb = bassMidPower > 0.0 ? detail::powerDb (bassSidePower / bassMidPower) : -INFINITY;
    return r;
}

/** The measurements as a text table for the answer key, one row per sample (rows[k] = sample k's
    report), the tone balance shown against the average of all samples, then what each column means. */
inline std::string reportTable (const std::vector<Report>& rows)
{
    const auto num = [] (double v, bool sign)
    {
        if (! std::isfinite (v)) return std::string ("-inf");
        if (std::abs (v) < 0.05) v = 0.0;   // no "-0.0"
        char buf[32];
        std::snprintf (buf, sizeof (buf), sign && v != 0.0 ? "%+.1f" : "%.1f", v);
        return std::string (buf);
    };
    const auto pad = [] (std::string s, size_t w) { return s.size() >= w ? s + " " : std::string (w - s.size(), ' ') + s + " "; };

    std::array<double, kBands> average {};
    std::array<int, kBands> counted {};
    for (const auto& r : rows)
        for (size_t k = 0; k < kBands; ++k)
            if (std::isfinite (r.bandDb[k]))
            {
                average[k] += r.bandDb[k];
                ++counted[k];
            }
    for (size_t k = 0; k < kBands; ++k)
        if (counted[k] > 0) average[k] /= counted[k];

    std::string t = "MEASUREMENTS (each file as exported, before the test turned it down)\n\n";
    t += "Sample   LUFS    TP   Kick Snare  Range  Width  BassW |   Sub   Bass LowMid    Mid   Pres    Air\n";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        std::string line = std::string ("  ") + static_cast<char> ('A' + i) + "    ";
        line += pad (num (r.lufs, false), 6) + pad (num (r.truePeakDbtp, false), 5) + pad (num (r.kickPunchDb, false), 6)
              + pad (num (r.snarePunchDb, false), 5) + pad (num (r.rangeLu, false), 6) + pad (num (r.widthDb, false), 6)
              + pad (num (r.bassWidthDb, false), 6) + "|";
        for (size_t k = 0; k < kBands; ++k)
            line += pad (std::isfinite (r.bandDb[k]) ? num (r.bandDb[k] - average[k], true) : std::string ("-inf"), 6);
        while (! line.empty() && line.back() == ' ') line.pop_back();
        t += line + "\n";
    }
    t += "\nWhat the columns mean:\n"
         "  LUFS   Loudness as exported. For a fair test the masters should be within 0.5 of each other.\n"
         "  TP     True peak (dBTP). Should be the same ceiling for every master; above -1 can crackle after\n"
         "         Spotify / YouTube convert the file.\n"
         "  Kick   Kick and 808 punch (below 150 Hz): how far the hits stick out above that band's level, dB.\n"
         "  Snare  Snare and clap punch (1-5 kHz), the same way. Higher = punchier; a squashed master reads lower.\n"
         "         Punch doesn't hear distortion: a hard clipper can keep a high number and still crackle.\n"
         "  Range  Loudness range (LU): how much louder the loud parts are than the quiet ones.\n"
         "  Width  The stereo edges against the centre (dB). Closer to 0 = wider; above 0 = more edge than\n"
         "         centre (check it in mono).\n"
         "  BassW  The same below 120 Hz. Lower (-20 or less) = a tight, centred 808 and kick.\n"
         "  Sub ... Air  Tone balance as you hear it in the test: each band against the average of all samples\n"
         "         (dB, + = more of it). Sub below 60 Hz (808 sub), Bass 60-200 Hz (808 body, kick), LowMid\n"
         "         200-800 Hz (warmth, mud), Mid 0.8-3 kHz (vocal), Pres 3-8 kHz (vocal clarity, snare crack,\n"
         "         harshness), Air above 8 kHz (hats, sparkle).\n";
    return t;
}

} // namespace blindtest
