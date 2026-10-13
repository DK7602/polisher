#pragma once

// The decisions behind the blind listening test, kept free of JUCE so the unit tests can check them:
// how much to turn each file down so all play at the same loudness without clipping, and the
// shuffled order of the samples.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace blindtest {

struct Measured
{
    double integratedLufs = -INFINITY;   // BS.1770 / EBU R128
    double truePeakDbtp = -INFINITY;
};

/** Gain in dB for each file: every file lands on the quietest one's loudness (turned down, never
    up, so no file gains noise or clips from a boost), then all move down together if any would
    peak above `maxPeakDbtp`. Files with no measurable loudness (silence) get 0 dB. */
inline std::vector<double> matchGains (const std::vector<Measured>& files, double maxPeakDbtp = -1.0)
{
    std::vector<double> gains (files.size(), 0.0);
    double quietest = INFINITY;
    for (const auto& f : files)
        if (std::isfinite (f.integratedLufs))
            quietest = std::min (quietest, f.integratedLufs);
    if (! std::isfinite (quietest))
        return gains;

    double highestPeak = -INFINITY;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (! std::isfinite (files[i].integratedLufs))
            continue;
        gains[i] = quietest - files[i].integratedLufs;                      // <= 0
        highestPeak = std::max (highestPeak, files[i].truePeakDbtp + gains[i]);
    }
    if (std::isfinite (highestPeak) && highestPeak > maxPeakDbtp)
        for (size_t i = 0; i < files.size(); ++i)
            if (std::isfinite (files[i].integratedLufs))
                gains[i] -= highestPeak - maxPeakDbtp;
    return gains;
}

/** Were the masters made the same way? The test plays them equally loud, but a master pushed harder
    usually gives up some punch, and a higher ceiling leaves it more room, so for a fair fight they should
    be made within kFairLu of each other and with the same ceiling (true peaks within kFairPeakDb).
    The unmastered mix is the file with the word "mix" in its name (my mix.wav; not "Mixdown"); it is
    left out. The others are matched to the master named "masterology" (or "polisher", the old name; the
    shortest such name), or to the loudest one when no file is named so. */
inline constexpr double kFairLu = 0.5;
inline constexpr double kFairPeakDb = 0.3;
inline constexpr double kFarLu = 4.0;   // a master this far under is probably an unnamed mix

namespace detail {
inline std::string lower (std::string s)
{
    for (auto& ch : s) ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
    return s;
}
inline bool isLetter (char ch) { return std::isalpha (static_cast<unsigned char> (ch)) != 0; }
} // namespace detail

/** The word "mix" in a file name (my mix.wav, trap_mix_v2.wav), but not inside another word (Mixdown). */
inline bool namedAsMix (const std::string& fileName)
{
    const auto name = detail::lower (fileName);
    for (size_t at = name.find ("mix"); at != std::string::npos; at = name.find ("mix", at + 1))
        if ((at == 0 || ! detail::isLetter (name[at - 1])) && (at + 3 >= name.size() || ! detail::isLetter (name[at + 3])))
            return true;
    return false;
}

struct Fairness
{
    std::vector<size_t> masters, mixes;   // indexes into the files
    long reference = -1;                  // the master the others are matched to
    bool referenceNamed = false;          // ... found by its name ("masterology" / "polisher")
    double spreadLu = 0.0, peakSpreadDb = 0.0;
    bool loudnessFair() const noexcept { return std::round (spreadLu * 10.0) / 10.0 <= kFairLu; }
    bool peaksFair() const noexcept { return std::round (peakSpreadDb * 10.0) / 10.0 <= kFairPeakDb; }
    bool fair() const noexcept { return loudnessFair() && peaksFair(); }
};

inline Fairness checkFairness (const std::vector<Measured>& files, const std::vector<std::string>& names)
{
    Fairness f;
    size_t refNameLength = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (! std::isfinite (files[i].integratedLufs))
            continue;
        if (namedAsMix (names[i]))
        {
            f.mixes.push_back (i);
            continue;
        }
        f.masters.push_back (i);
        const auto lowerName = detail::lower (names[i]);
        const bool ours = lowerName.find ("masterology") != std::string::npos || lowerName.find ("polisher") != std::string::npos;
        if (ours && (! f.referenceNamed || names[i].size() < refNameLength))
        {
            f.reference = static_cast<long> (i);
            f.referenceNamed = true;
            refNameLength = names[i].size();
        }
    }
    if (f.masters.empty())
        return f;
    double loudest = -INFINITY, quietest = INFINITY, highPeak = -INFINITY, lowPeak = INFINITY;
    for (size_t i : f.masters)
    {
        if (! f.referenceNamed && files[i].integratedLufs > loudest)
            f.reference = static_cast<long> (i);
        loudest = std::max (loudest, files[i].integratedLufs);
        quietest = std::min (quietest, files[i].integratedLufs);
        highPeak = std::max (highPeak, files[i].truePeakDbtp);
        lowPeak = std::min (lowPeak, files[i].truePeakDbtp);
    }
    f.spreadLu = loudest - quietest;
    f.peakSpreadDb = std::isfinite (highPeak) && std::isfinite (lowPeak) ? highPeak - lowPeak : 0.0;
    return f;
}

/** What checkFairness found, in plain words, naming the files. It never mentions a sample letter, so it
    can go on the score sheet. */
inline std::string fairnessText (const Fairness& f, const std::vector<Measured>& files, const std::vector<std::string>& names)
{
    const auto num = [] (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.1f", std::abs (v) < 0.05 ? 0.0 : v);
        return std::string (buf);
    };
    std::string t;
    for (size_t i : f.mixes)
        t += names[i] + " (" + num (files[i].integratedLufs) + " LUFS): your unmastered mix (\"mix\" in its name), not checked.\n";
    if (f.masters.size() < 2 || f.reference < 0)
        return t;
    const auto ref = static_cast<size_t> (f.reference);
    if (f.fair())
        t += "Fair fight: the masters are within " + num (f.spreadLu) + " LU of each other, true peaks within "
           + num (f.peakSpreadDb) + " dB.\n";
    if (! f.loudnessFair())
    {
        t += "Not a fair fight yet: the masters were made " + num (f.spreadLu) + " LU apart (aim for " + num (kFairLu) + " or less).\n"
             "The test still plays them equally loud, but a master pushed harder usually loses some punch,\n"
             "so the quieter one has the easier job. Match them to " + names[ref] + " and export again:\n";
        for (size_t i : f.masters)
        {
            const double off = files[ref].integratedLufs - files[i].integratedLufs;
            t += "  " + names[i] + " (" + num (files[i].integratedLufs) + " LUFS): ";
            if (i == ref)
                t += "the one the others are matched to\n";
            else if (off > kFairLu)
                t += "needs " + num (off) + " LU more: push it about " + num (off) + " dB harder (a little more if it's\n"
                     "    already limiting hard)" + (off > kFarLu ? ". If this is your unmastered mix, put the word mix in its\n"
                                                                   "    name (my mix.wav) and run again" : "") + "\n";
            else if (off < -kFairLu)
                t += num (-off) + " LU too loud: back it off about " + num (-off) + " dB\n";
            else
                t += "matched\n";
        }
    }
    if (! f.peaksFair())
    {
        t += "The masters' ceilings differ: their true peaks are " + num (f.peakSpreadDb) + " dB apart. Use the same ceiling\n"
             "(and true-peak mode) for all; Auto-Master sets -1.5 on the Loud and Club targets:\n";
        for (size_t i : f.masters)
            t += "  " + names[i] + ": " + num (files[i].truePeakDbtp) + " dBTP\n";
    }
    return t;
}

/** A random order of 0..n-1 (sample A is files[order[0]], B is files[order[1]], ...). */
inline std::vector<int> shuffledOrder (int n, unsigned seed)
{
    std::vector<int> order (static_cast<size_t> (n));
    std::iota (order.begin(), order.end(), 0);
    std::mt19937 rng (seed);
    std::shuffle (order.begin(), order.end(), rng);
    return order;
}

} // namespace blindtest
