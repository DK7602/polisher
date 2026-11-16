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
        const char* const kStemKeys[kStems] = { "stemVoc", "stemDrums", "stemBass", "stemOther" };   // plugin/ModuleParams.h
        const char* const kImagerKeys[3] = { "imgLow", "imgMid", "imgHigh" };

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

        // Every knob the Rack can turn: its parameter's id and range, as the host has it (plugin/PluginProcessor.cpp,
        // plugin/ModuleParams.h). skew: JUCE's skew factor (1 = linear); centre: JUCE's skew for a centre value instead;
        // steps: a choice of that many.
        struct Knob
        {
            const char* key;
            double lo, hi, skew, centre;
            int steps;
        };
        const Knob kKnobs[] = {
            { "ulAmount", 0, 100, 1, 0, 0 }, { "ulLength", 5, 50, 0.6, 0, 0 }, { "ulFocus", 0, 2, 1, 0, 3 },
            { "eqGain1", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 }, { "eqGain2", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 },
            { "eqGain3", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 }, { "eqGain4", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 },
            { "eqGain5", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 }, { "eqGain6", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 },
            { "eqGain7", -kEqMaxGainDb, kEqMaxGainDb, 1, 0, 0 },
            { "stemVoc", -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb, 1, 0, 0 }, { "stemDrums", -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb, 1, 0, 0 },
            { "stemBass", -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb, 1, 0, 0 }, { "stemOther", -StemEQ::kMaxGainDb, StemEQ::kMaxGainDb, 1, 0, 0 },
            { "tsAmount", 0, 100, 1, 0, 0 }, { "tsRange", 1, 6, 1, 0, 0 }, { "tsSpeed", 0, 2, 1, 0, 3 },
            { "rtAmount", 0, 100, 1, 0, 0 }, { "rtSens", 0, 100, 1, 0, 0 }, { "rtRelease", 20, 300, 0.5, 0, 0 }, { "rtFocus", 0, 2, 1, 0, 3 },
            { "bassMono", 0, kBassMonoMaxHz, 1, 0, 0 }, { "bassTame", 0, 100, 1, 0, 0 }, { "bassPunch", 0, 100, 1, 0, 0 },
            { "glueChar", 0, 2, 1, 0, 3 }, { "glueThr", -40, 0, 1, 0, 0 }, { "glueRatio", 1, 10, 0.5, 0, 0 }, { "glueAtk", 0.1, 30, 0.4, 0, 0 },
            { "glueRel", 50, 1200, 0.5, 0, 0 }, { "glueSc", 20, 250, 1, 90, 0 }, { "glueMix", 0, 100, 1, 0, 0 }, { "glueGain", -12, 12, 1, 0, 0 },
            { "imgLow", 0, 200, 1, 0, 0 }, { "imgMid", 0, 200, 1, 0, 0 }, { "imgHigh", 0, 200, 1, 0, 0 },
            { "satMode", 0, 2, 1, 0, 3 }, { "satFocus", 0, 3, 1, 0, 4 }, { "satDrive", 0, Saturation::kMaxDriveDb, 1, 0, 0 }, { "satMix", 0, 100, 1, 0, 0 },
            { "clipLevel", -9, 3, 1, 0, 0 }, { "clipSoftness", 0, 100, 1, 0, 0 }, { "clipOversampling", 0, 3, 1, 0, 4 },
            { "drive", 0, 24, 1, 0, 0 }, { "inTrim", kMinTrimDb, 0, 1, 0, 0 }, { "ceiling", -3, 0, 1, 0, 0 }, { "release", 10, 1000, 0.4, 0, 0 },
            { "maxBassFirst", 0, 100, 1, 0, 0 },
        };

        const Knob* knobSpec (const std::string& key)
        {
            for (const auto& k : kKnobs)
                if (key == k.key) return &k;
            return nullptr;
        }

        double skewOf (const Knob& k)
        {
            return k.centre > k.lo && k.centre < k.hi ? std::log (0.5) / std::log ((k.centre - k.lo) / (k.hi - k.lo)) : k.skew;
        }

        float knobNorm (const Knob& k, double value)
        {
            if (k.steps >= 2) return choice (static_cast<int> (std::lround (value - k.lo)), k.steps);
            return skewed (value, k.lo, k.hi, skewOf (k));
        }

        // Shown only (what the Dynamics bands do now, the loudness Masterology aims for).
        void add (rack::Module& m, const std::string& name, const std::string& text, float norm, bool bi = false)
        {
            if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues)) m.values.push_back ({ name, text, norm, bi });
        }
        // A knob the Rack can turn, at `value` in its parameter's units.
        void add (rack::Module& m, const std::string& name, const std::string& text, const std::string& key, double value, bool bi = false)
        {
            const auto* k = knobSpec (key);
            if (k == nullptr || m.values.size() >= static_cast<std::size_t> (rack::kMaxValues)) return;
            m.values.push_back ({ name, text, knobNorm (*k, value), bi, key, k->steps });
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

    const char* switchOf (const std::string& moduleKey)
    {
        static const std::pair<const char*, const char*> kSwitches[] = {
            { "mas.unlimiter", "ulOn" }, { "mas.tonaleq", "eqOn" }, { "mas.stemeq", "stemOn" }, { "mas.steady", "tsOn" },
            { "mas.resonance", "rtOn" }, { "mas.bass", "bassOn" }, { "mas.glue", "glueOn" }, { "mas.dynamics", "dynOn" },
            { "mas.imager", "imgOn" }, { "mas.sat", "satOn" }, { "mas.clipper", "clipOn" },
        };
        for (const auto& [module, param] : kSwitches)
            if (moduleKey == module) return param;
        return nullptr;
    }

    bool fromKnob (const std::string& key, float knob, double& value)
    {
        const auto* k = knobSpec (key);
        if (k == nullptr) return false;
        const double n = std::clamp (std::isfinite (knob) ? static_cast<double> (knob) : 0.0, 0.0, 1.0);
        if (k->steps >= 2)
            value = k->lo + std::round (n * (k->steps - 1));
        else
        {
            const double s = skewOf (*k);
            value = k->lo + (s > 0.0 && s != 1.0 ? std::pow (n, 1.0 / s) : n) * (k->hi - k->lo);
        }
        value = std::clamp (value, k->lo, k->hi);
        return true;
    }

    std::vector<rack::Module> modules (const State& s, bool full)
    {
        std::vector<rack::Module> out;
        auto finish = [&] (rack::Module& m, const char* idleText = "Set to do nothing") {
            m.switchable = switchOf (m.key) != nullptr;
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
            add (m, "Amount", pct (u.amount), "ulAmount", u.amount);
            add (m, "Length", ms (u.lengthMs), "ulLength", u.lengthMs);
            add (m, "Focus", kFocus[focus], "ulFocus", focus);
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
                add (m, kEqNames[b], signedDb (g), "eqGain" + std::to_string (b + 1), g, true);
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
                add (m, kStemNames[k], signedDb (g), kStemKeys[k], g, true);
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
            add (m, "Amount", pct (t.amount), "tsAmount", t.amount);
            add (m, "Range", db (t.rangeDb), "tsRange", t.rangeDb);
            add (m, "Speed", kSpeeds[speed], "tsSpeed", speed);
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
            add (m, "Amount", pct (r.amount), "rtAmount", r.amount);
            add (m, "Sensitivity", pct (r.sensitivity), "rtSens", r.sensitivity);
            add (m, "Release", ms (r.releaseMs), "rtRelease", r.releaseMs);
            add (m, "Focus", kFocus[focus], "rtFocus", focus);
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
            add (m, "Mono Below", mono ? hz (b.monoHz) : "Off", "bassMono", b.monoHz);
            add (m, "Sub Tame", pct (b.tame * 100.0), "bassTame", b.tame * 100.0);
            add (m, "Punch", pct (b.punch * 100.0), "bassPunch", b.punch * 100.0);
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
            add (m, "Character", kChar[character], "glueChar", character);
            add (m, "Threshold", db (g.thresholdDb), "glueThr", g.thresholdDb);
            add (m, "Ratio", ratio (g.ratio), "glueRatio", g.ratio);
            add (m, "Attack", ms (g.attackMs), "glueAtk", g.attackMs);
            add (m, "Release", g.autoRelease ? "Auto" : ms (g.releaseMs), "glueRel", g.releaseMs);
            add (m, "Ignore Lows", g.sidechainHz <= 20.0 ? "Off" : hz (g.sidechainHz), "glueSc", g.sidechainHz);
            add (m, "Mix", pct (g.mix * 100.0), "glueMix", g.mix * 100.0);
            add (m, "Gain", signedDb (g.gainDb), "glueGain", g.gainDb, true);
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
                add (m, kNames[i], pct (w[i]), kImagerKeys[i], w[i]);
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
            add (m, "Mode", kModes[mode], "satMode", mode);
            add (m, "Focus", kFocus[focus], "satFocus", focus);
            add (m, "Drive", db (t.driveDb), "satDrive", t.driveDb);
            add (m, "Mix", pct (t.mix), "satMix", t.mix);
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
            add (m, "Clip Level", signedDb (s.clipLevelDb), "clipLevel", s.clipLevelDb, true);
            add (m, "Softness", pct (s.clipSoftness), "clipSoftness", s.clipSoftness);
            add (m, "Oversampling", kOs[os], "clipOversampling", os);
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
            add (m, "Drive", signedDb (s.driveDb), "drive", s.driveDb);
            add (m, "Input Trim", signedDb (s.inputTrimDb), "inTrim", s.inputTrimDb);
            add (m, "Ceiling", ceiling, "ceiling", l.ceilingDbtp);
            add (m, "Release", l.adaptiveRelease ? ms (l.releaseMs) + " auto" : ms (l.releaseMs), "release", l.releaseMs);
            add (m, "Bass First", pct (l.bassFirst * 100.0), "maxBassFirst", l.bassFirst * 100.0);
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
