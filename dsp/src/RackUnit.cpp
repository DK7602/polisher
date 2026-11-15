#include "polisher/RackUnit.h"

#include <algorithm>
#include <cmath>

namespace polisher::rackunit
{
    namespace
    {
        const char* const kMinus = "\xE2\x88\x92";   // −
        const char* const kDot = " \xC2\xB7 ";       // " · "
        const char* const kEqNames[kEqBands] = { "Low End", "Bass", "Low Mid", "Mid", "Upper Mid", "Presence", "Air" };
        const char* const kStemNames[kStems] = { "Vocals", "Drums", "Bass", "Other" };
        const char* const kDynNames[kDynBands] = { "Low", "Lo-Mid", "Hi-Mid", "High" };

        // Fixed decimals without printf (whose decimal point follows the host's locale); no sign.
        std::string digits (double v, int decimals)
        {
            if (! std::isfinite (v)) v = 0.0;
            long long scale = 1;
            for (int i = 0; i < decimals; ++i) scale *= 10;
            const long long n = std::llround (std::abs (v) * static_cast<double> (scale));
            std::string s = std::to_string (n / scale);
            if (decimals > 0)
            {
                const std::string frac = std::to_string (n % scale);
                s += "." + std::string (static_cast<std::size_t> (decimals) - std::min (frac.size(), static_cast<std::size_t> (decimals)), '0') + frac;
            }
            return s;
        }
        bool showsNegative (double v, int d) { return v < 0.0 && std::llround (std::abs (v) * std::pow (10.0, d)) != 0; }
        std::string num (double v, int d = 1) { return (showsNegative (v, d) ? kMinus : "") + digits (v, d); }
        std::string db (double v, int d = 1) { return num (v, d) + " dB"; }
        std::string signedDb (double v) { return (v > 0.0 && std::llround (v * 10.0) != 0 ? "+" : "") + db (v); }
        std::string hz (double f) { return f >= 1000.0 ? digits (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : digits (f, 0) + " Hz"; }
        std::string pct (double v) { return digits (v, 0) + " %"; }
        std::string ms (double v) { return v < 10.0 ? digits (v, 1) + " ms" : digits (v, 0) + " ms"; }
        std::string ratio (double r) { return digits (r, 1) + ":1"; }

        // Where a value sits on its knob (0..1), as the parameter's range (JUCE's skew).
        float lin (double v, double lo, double hi) { return hi > lo ? static_cast<float> (std::clamp ((v - lo) / (hi - lo), 0.0, 1.0)) : 0.5f; }
        float skewed (double v, double lo, double hi, double skew)
        {
            const double n = std::pow (static_cast<double> (lin (v, lo, hi)), skew);
            return std::isfinite (n) ? static_cast<float> (std::clamp (n, 0.0, 1.0)) : 0.5f;
        }
        float choice (int i, int count) { return count > 1 ? static_cast<float> (std::clamp (i, 0, count - 1)) / static_cast<float> (count - 1) : 0.0f; }

        void add (rack::Module& m, const std::string& name, const std::string& text, float norm, bool bi = false)
        {
            if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues)) m.values.push_back ({ name, text, norm, bi });
        }
        void join (std::string& s, const std::string& part, std::size_t limit = 64)
        {
            if (part.empty() || s.size() >= limit) return;
            s += (s.empty() ? "" : kDot) + part;
        }
        void gr (rack::Module& m, const State& st, float db)
        {
            m.meterKind = rack::MeterKind::gr;
            m.meter = st.hasMeters && m.state == rack::State::on && std::isfinite (db) ? std::min (0.0f, db) : 0.0f;
        }
        rack::Module cell (const char* key, bool enabled, bool idle)
        {
            rack::Module m;
            m.key = key;
            m.state = ! enabled ? rack::State::off : idle ? rack::State::idle : rack::State::on;
            return m;
        }
        bool bandActive (const DynBandParams& b)
        {
            return b.ratio > 1.0 || std::abs (b.gainDb) >= kDynGainNeutralDb || std::abs (b.impact) >= kImpactNeutral || std::abs (b.sustain) >= kImpactNeutral;
        }
        std::string signedPct (double v) { return std::string (v > 0.0 ? "+" : showsNegative (v, 0) ? kMinus : "") + digits (v, 0) + " %"; }
    }

    std::vector<rack::Module> modules (const State& s, bool full)
    {
        std::vector<rack::Module> out;
        auto finish = [&] (rack::Module& m, const char* idleText = "Set to do nothing") {
            if (m.state == rack::State::off) m.summary = "Off";
            else if (m.state == rack::State::idle) m.summary = idleText;
            if (! full)
            {
                m.values.clear();
                m.bands.clear();
            }
            out.push_back (std::move (m));
        };

        // Unlimiter
        {
            const auto& u = s.unlimiter;
            static const char* const kFocus[] = { "Full", "Kick", "Snap" };
            const int focus = std::clamp (static_cast<int> (u.focus), 0, 2);
            auto m = cell ("mas.unlimiter", u.enabled, u.amount < 0.5);
            add (m, "Amount", pct (u.amount), lin (u.amount, 0.0, 100.0));
            add (m, "Length", ms (u.lengthMs), skewed (u.lengthMs, 5.0, 50.0, 0.6));
            add (m, "Focus", kFocus[focus], choice (focus, 3));
            m.summary = "Amount " + pct (u.amount);
            if (focus != 0) join (m.summary, std::string ("Focus ") + kFocus[focus]);
            finish (m);
        }

        // Tonal EQ
        {
            const auto& e = s.eq;
            bool flat = true;
            for (const double g : e.gainDb) flat &= std::abs (g) < 0.05;
            auto m = cell ("mas.tonaleq", e.enabled, flat);
            for (int b = 0; b < kEqBands; ++b)
            {
                const auto i = static_cast<std::size_t> (b);
                const auto& spec = kEqBandSpecs[i];
                const double g = e.gainDb[i];
                add (m, kEqNames[b], signedDb (g), lin (g, -kEqMaxGainDb, kEqMaxGainDb), true);
                if (std::abs (g) < 0.05) continue;
                const char* where = e.channels[i] == EqChannels::mid ? " (Mid)" : e.channels[i] == EqChannels::side ? " (Side)" : "";
                join (m.summary, std::string (kEqNames[b]) + " " + signedDb (g) + where);
                const auto type = spec.shape == EqBandSpec::Shape::lowShelf ? rack::BandType::lowShelf
                                : spec.shape == EqBandSpec::Shape::highShelf ? rack::BandType::highShelf : rack::BandType::bell;
                m.bands.push_back ({ type, static_cast<float> (e.freqHz[i]), static_cast<float> (g), static_cast<float> (spec.q), kEqNames[b], true });
            }
            finish (m, "Flat");
        }

        // Stem EQ
        {
            const auto& t = s.stem;
            bool flat = true;
            for (const double g : t.gainDb) flat &= std::abs (g) < 0.05;
            auto m = cell ("mas.stemeq", t.enabled, flat);
            for (int k = 0; k < kStems; ++k)
            {
                const double g = t.gainDb[static_cast<std::size_t> (k)];
                add (m, kStemNames[k], signedDb (g), lin (g, -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb), true);
                if (std::abs (g) >= 0.05) join (m.summary, std::string (kStemNames[k]) + " " + signedDb (g));
            }
            finish (m, "Flat");
        }

        // Tone Steady
        {
            const auto& t = s.steady;
            static const char* const kSpeeds[] = { "Slow", "Medium", "Fast" };
            const int speed = std::clamp (t.speed, 0, 2);
            auto m = cell ("mas.steady", t.enabled, t.amount < 0.5);
            add (m, "Amount", pct (t.amount), lin (t.amount, 0.0, 100.0));
            add (m, "Range", db (t.rangeDb), lin (t.rangeDb, 1.0, 6.0));
            add (m, "Speed", kSpeeds[speed], choice (speed, 3));
            add (m, "Target", s.steadyHasTarget ? "The song's tone" : "Follows the music", s.steadyHasTarget ? 1.0f : 0.0f);
            m.summary = "Amount " + pct (t.amount);
            join (m.summary, s.steadyHasTarget ? "holds the song's tone" : "follows the music");
            finish (m);
        }

        // Resonance Tamer
        {
            const auto& r = s.resonance;
            static const char* const kFocus[] = { "Full", "Boxiness", "Harshness" };
            const int focus = std::clamp (static_cast<int> (r.focus), 0, 2);
            auto m = cell ("mas.resonance", r.enabled, r.amount < 0.5);
            add (m, "Amount", pct (r.amount), lin (r.amount, 0.0, 100.0));
            add (m, "Sensitivity", pct (r.sensitivity), lin (r.sensitivity, 0.0, 100.0));
            add (m, "Release", ms (r.releaseMs), skewed (r.releaseMs, 20.0, 300.0, 0.5));
            add (m, "Focus", kFocus[focus], choice (focus, 3));
            m.summary = "Amount " + pct (r.amount);
            if (focus != 0) join (m.summary, kFocus[focus]);
            gr (m, s, s.meters.resonance);
            finish (m);
        }

        // Bass Control
        {
            const auto& b = s.bass;
            const bool mono = b.monoHz >= kBassMonoOffHz;
            auto m = cell ("mas.bass", b.enabled, ! mono && b.tame < 0.0005 && b.punch < 0.0005);
            add (m, "Mono Below", mono ? hz (b.monoHz) : "Off", lin (b.monoHz, 0.0, kBassMonoMaxHz));
            add (m, "Sub Tame", pct (b.tame * 100.0), lin (b.tame, 0.0, 1.0));
            add (m, "Punch", pct (b.punch * 100.0), lin (b.punch, 0.0, 1.0));
            if (mono) join (m.summary, "Mono below " + hz (b.monoHz));
            if (b.tame >= 0.0005) join (m.summary, "Sub tame " + pct (b.tame * 100.0));
            if (b.punch >= 0.0005) join (m.summary, "Punch " + pct (b.punch * 100.0));
            gr (m, s, s.meters.bassTame);
            finish (m);
        }

        // Glue
        {
            const auto& g = s.glue;
            static const char* const kChar[] = { "Bus", "Opto", "Tube" };
            const int character = std::clamp (static_cast<int> (g.character), 0, 2);
            auto m = cell ("mas.glue", g.enabled, GlueCompressor::isNeutral (g));
            add (m, "Character", kChar[character], choice (character, 3));
            add (m, "Threshold", db (g.thresholdDb), lin (g.thresholdDb, -40.0, 0.0));
            add (m, "Ratio", ratio (g.ratio), skewed (g.ratio, 1.0, 10.0, 0.5));
            add (m, "Attack", ms (g.attackMs), skewed (g.attackMs, 0.1, 30.0, 0.4));
            add (m, "Release", g.autoRelease ? "Auto" : ms (g.releaseMs), skewed (g.releaseMs, 50.0, 1200.0, 0.5));
            add (m, "Ignore Lows", g.sidechainHz <= 20.0 ? "Off" : hz (g.sidechainHz), lin (g.sidechainHz, 20.0, 250.0));
            add (m, "Mix", pct (g.mix * 100.0), lin (g.mix, 0.0, 1.0));
            add (m, "Gain", signedDb (g.gainDb), lin (g.gainDb, -12.0, 12.0), true);
            m.summary = kChar[character];
            if (g.ratio > kGlueRatioNeutral) join (m.summary, ratio (g.ratio) + kDot + "Threshold " + db (g.thresholdDb));
            if (g.mix < 0.995) join (m.summary, "Mix " + pct (g.mix * 100.0));
            gr (m, s, s.meters.glue);
            finish (m);
        }

        // Dynamics (four bands)
        {
            const auto& d = s.dynamics;
            auto m = cell ("mas.dynamics", d.enabled, MultibandDynamics::isNeutral (d));
            float deepest = 0.0f;
            for (int b = 0; b < kDynBands; ++b)
            {
                const auto& band = d.bands[static_cast<std::size_t> (b)];
                const bool active = bandActive (band);
                const float now = s.hasMeters ? std::min (0.0f, s.meters.dynamics[static_cast<std::size_t> (b)]) : 0.0f;
                if (active) deepest = std::min (deepest, now);
                // The knob: the band's gain reduction now (what it does), "Off" when the band is left alone.
                add (m, kDynNames[b], active ? (band.ratio > 1.0 ? db (now) : "Shaped") : "Off", active ? lin (-now, 0.0, 12.0) : 0.0f);
                if (! active) continue;
                std::string what = band.ratio > 1.0 ? ratio (band.ratio) : "";
                if (std::abs (band.impact) >= kImpactNeutral) what += (what.empty() ? "Impact " : ", impact ") + signedPct (band.impact);
                if (std::abs (band.sustain) >= kImpactNeutral) what += (what.empty() ? "Sustain " : ", sustain ") + signedPct (band.sustain);
                if (what.empty()) what = signedDb (band.gainDb);
                join (m.summary, std::string (kDynNames[b]) + " " + what);
            }
            gr (m, s, deepest);
            finish (m);
        }

        // Stereo Imager
        {
            const auto& im = s.imager;
            const double w[3] = { im.lowWidth, im.midWidth, im.highWidth };
            static const char* const kNames[] = { "Lows", "Mids", "Highs" };
            bool idle = true;
            for (const double x : w) idle &= std::abs (x - 100.0) < 0.5;
            auto m = cell ("mas.imager", im.enabled, idle);
            for (int i = 0; i < 3; ++i)
            {
                add (m, kNames[i], pct (w[i]), lin (w[i], 0.0, 200.0));
                if (std::abs (w[i] - 100.0) >= 0.5) join (m.summary, std::string (kNames[i]) + " " + pct (w[i]));
            }
            finish (m);
        }

        // Saturation
        {
            const auto& t = s.saturation;
            static const char* const kModes[] = { "Tape", "Tube", "Clip" };
            static const char* const kFocus[] = { "Full", "Bass only", "Highs only", "Bass + Highs" };
            const int mode = std::clamp (static_cast<int> (t.mode), 0, 2), focus = std::clamp (static_cast<int> (t.focus), 0, 3);
            auto m = cell ("mas.sat", t.enabled, Saturation::isNeutral (t));
            add (m, "Mode", kModes[mode], choice (mode, 3));
            add (m, "Focus", kFocus[focus], choice (focus, 4));
            add (m, "Drive", db (t.driveDb), lin (t.driveDb, 0.0, Saturation::kMaxDriveDb));
            add (m, "Mix", pct (t.mix), lin (t.mix, 0.0, 100.0));
            m.summary = kModes[mode];
            join (m.summary, "Drive " + db (t.driveDb));
            if (t.mix < 99.5) join (m.summary, "Mix " + pct (t.mix));
            if (focus != 0) join (m.summary, kFocus[focus]);
            finish (m);
        }

        // Soft Clipper
        {
            static const char* const kOs[] = { "1x", "2x", "4x", "8x" };
            const int os = std::clamp (s.clipOversampling, 0, 3);
            auto m = cell ("mas.clipper", s.clipOn, false);
            add (m, "Clip Level", signedDb (s.clipLevelDb), lin (s.clipLevelDb, -9.0, 3.0), true);
            add (m, "Softness", pct (s.clipSoftness), lin (s.clipSoftness, 0.0, 100.0));
            add (m, "Oversampling", kOs[os], choice (os, 4));
            m.summary = "Clip level " + signedDb (s.clipLevelDb);
            gr (m, s, s.meters.clipper);
            finish (m);
        }

        // Maximizer (Input Trim before the whole chain, Drive into it; the loudness it aims for)
        {
            const auto& l = s.limiter;
            auto m = cell ("mas.max", true, false);
            const std::string ceiling = num (l.ceilingDbtp, 1) + " dBTP";
            const std::string target = num (s.targetLufs, 0) + " LUFS";
            add (m, "Drive", signedDb (s.driveDb), lin (s.driveDb, 0.0, 24.0));
            add (m, "Input Trim", signedDb (s.inputTrimDb), lin (s.inputTrimDb, kMinTrimDb, 0.0));
            add (m, "Ceiling", ceiling, lin (l.ceilingDbtp, -3.0, 0.0));
            add (m, "Release", l.adaptiveRelease ? ms (l.releaseMs) + " auto" : ms (l.releaseMs), skewed (l.releaseMs, 10.0, 1000.0, 0.4));
            add (m, "Bass First", pct (l.bassFirst * 100.0), lin (l.bassFirst, 0.0, 1.0));
            add (m, "Target", target, lin (s.targetLufs, -16.0, -6.0));
            m.summary = "Drive " + signedDb (s.driveDb);
            join (m.summary, "Ceiling " + ceiling);
            join (m.summary, (s.goal.empty() ? std::string ("Target") : s.goal) + " " + target);
            gr (m, s, s.meters.maximizer);
            finish (m);
        }
        return out;
    }
}
