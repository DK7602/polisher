#include "polisher/AutoAnalyze.h"

#include <algorithm>
#include <cmath>

namespace polisher::autoanalyze
{
    namespace
    {
        std::string num (const char* prefix, int index) { return prefix + std::to_string (index + 1); }

        bool starts (const std::string& s, const char* prefix) { return s.rfind (prefix, 0) == 0; }

        /** How Auto Analyze moves a control. */
        enum class Kind
        {
            additive,         // by fresh - planned (dB, %)
            ratio,            // by fresh / planned (Hz, ms)
            choice,           // to fresh, when Auto-Master changed its mind
            hidden,           // to fresh, never listed and never a reason for an edit on its own
        };

        struct Rule
        {
            Kind kind = Kind::additive;
            double worth = 0.0;   // a move this big makes the module worth offering (additive: units; ratio: factor - 1)
            double step = 0.1;    // the new value is rounded to this (additive)
        };

        Rule ruleFor (const std::string& id)
        {
            if (id == "ulSquashSet") return { Kind::hidden };
            if (id == "ulFocus" || id == "rtFocus" || id == "tsSpeed" || id == "satMode" || id == "satFocus" || id == "clipOn"
                || id == "clipOversampling" || id == "dynAdaptive" || id == "adaptive")
                return { Kind::choice };
            if (id == "bassMono") return { Kind::ratio, 0.2, 1.0 };   // 0 = off: handled as a choice (see moveOf)
            if (starts (id, "dynX") || starts (id, "dynAtk") || starts (id, "dynRel") || id == "rtRelease" || id == "release" || id == "ulLength")
                return { Kind::ratio, 0.25, 1.0 };
            if (starts (id, "eqGain")) return { Kind::additive, 0.75, 0.1 };
            if (starts (id, "dynThr")) return { Kind::additive, 1.5, 0.1 };
            if (starts (id, "dynGain")) return { Kind::additive, 0.75, 0.1 };
            if (starts (id, "dynRatio")) return { Kind::additive, 0.3, 0.05 };
            if (starts (id, "stem")) return { Kind::choice };   // Auto-Master's suggestion as it is (0.5 dB steps)
            if (id == "drive" || id == "inTrim") return { Kind::additive, 0.75, 0.1 };
            if (id == "ceiling") return { Kind::additive, 0.25, 0.1 };
            if (id == "satDrive") return { Kind::additive, 1.5, 0.1 };
            if (id == "clipLevel") return { Kind::additive, 0.5, 0.1 };
            if (id == "tsRange") return { Kind::additive, 1.0, 0.1 };
            if (starts (id, "img")) return { Kind::additive, 5.0, 1.0 };
            // Percent knobs: Unlimiter / Tone Steady / Resonance Tamer amounts, sensitivity, Bass Tame / Punch,
            // Saturation Mix, Clip softness.
            return { Kind::additive, 10.0, 1.0 };
        }

        double roundTo (double v, double step) { return step > 0.0 ? std::round (v / step) * step : v; }

        const char* const kOrder[] = { "ul", "eq", "stem", "ts", "rt", "bass", "dyn", "img", "sat", "clip", "max" };
    }

    Values chainValues (const MasterChainParams& c)
    {
        Values v;
        v["bypass"] = 0.0;
        v["eqOn"] = 1.0;
        for (int b = 0; b < kEqBands; ++b)
        {
            v[num ("eqGain", b)] = c.eq.gainDb[static_cast<size_t> (b)];
            v[num ("eqFreq", b)] = kEqBandSpecs[static_cast<size_t> (b)].defaultHz;
            v[num ("eqMode", b)] = 0.0;   // Stereo: Auto-Master's tone plan works on both channels
        }
        v["ulOn"] = 1.0;
        v["ulAmount"] = c.unlimiter.amount;
        v["ulLength"] = c.unlimiter.lengthMs;
        v["ulFocus"] = static_cast<double> (static_cast<int> (c.unlimiter.focus));
        v["ulSquashSet"] = c.unlimiter.squashDb;
        v["tsOn"] = 1.0;
        v["tsAmount"] = c.steady.amount;
        v["tsRange"] = c.steady.rangeDb;
        v["tsSpeed"] = static_cast<double> (c.steady.speed);
        v["rtOn"] = 1.0;
        v["rtAmount"] = c.resonance.amount;
        v["rtSens"] = c.resonance.sensitivity;
        v["rtRelease"] = c.resonance.releaseMs;
        v["rtFocus"] = static_cast<double> (static_cast<int> (c.resonance.focus));
        v["bassOn"] = 1.0;
        v["bassMono"] = c.bass.monoHz >= kBassMonoOffHz ? c.bass.monoHz : 0.0;
        v["bassTame"] = c.bass.tame * 100.0;
        v["bassPunch"] = c.bass.punch * 100.0;
        // Glue: off (1:1, 0 dB). Auto-Master plans the chain without it, so its loudness plan stays right.
        v["glueRatio"] = 1.0;
        v["glueGain"] = 0.0;
        v["dynOn"] = 1.0;
        v["dynAdaptive"] = c.dynamics.adaptiveRelease ? 1.0 : 0.0;
        v["dynChannels"] = static_cast<double> (static_cast<int> (c.dynamics.channels));
        for (int x = 0; x < kDynBands - 1; ++x)
            v[num ("dynX", x)] = c.dynamics.crossoverHz[static_cast<size_t> (x)];
        for (int b = 0; b < kDynBands; ++b)
        {
            const auto& d = c.dynamics.bands[static_cast<size_t> (b)];
            v[num ("dynThr", b)] = d.thresholdDb;
            v[num ("dynRatio", b)] = d.ratio;
            v[num ("dynAtk", b)] = d.attackMs;
            v[num ("dynRel", b)] = d.releaseMs;
            v[num ("dynGain", b)] = d.gainDb;
            v[num ("dynImpact", b)] = d.impact;     // 0: Auto-Master plans without Impact
            v[num ("dynSustain", b)] = d.sustain;   // or Sustain
        }
        v["imgOn"] = 1.0;
        v["imgLow"] = c.imager.lowWidth;
        v["imgMid"] = c.imager.midWidth;
        v["imgHigh"] = c.imager.highWidth;
        v["satOn"] = 1.0;
        v["satMode"] = static_cast<double> (static_cast<int> (c.saturation.mode));
        v["satFocus"] = static_cast<double> (static_cast<int> (c.saturation.focus));
        v["satChannels"] = static_cast<double> (static_cast<int> (c.saturation.channels));
        v["satDrive"] = c.saturation.driveDb;
        v["satMix"] = c.saturation.mix;
        v["clipOn"] = c.clipper.enabled ? 1.0 : 0.0;
        if (c.clipper.enabled)
        {
            v["clipLevel"] = c.clipper.thresholdDb - c.limiter.ceilingDbtp;
            v["clipSoftness"] = c.clipper.softness * 100.0;
            v["clipOversampling"] = c.clipper.oversampling >= 8 ? 3.0 : c.clipper.oversampling >= 4 ? 2.0 : c.clipper.oversampling >= 2 ? 1.0 : 0.0;
        }
        v["inTrim"] = c.trimDb;
        v["drive"] = c.driveDb;
        v["ceiling"] = c.limiter.ceilingDbtp;
        v["release"] = c.limiter.releaseMs;
        v["maxBassFirst"] = c.limiter.bassFirst * 100.0;   // 0: Auto-Master plans the plain limiter
        v["adaptive"] = c.limiter.adaptiveRelease ? 1.0 : 0.0;
        return v;
    }

    std::string moduleOf (const std::string& id)
    {
        // Never moved: the module switches and bypass (yours), and what Auto-Master always sets the same way.
        if (id == "clipOn") return "clip";
        if (id == "bypass" || (id.size() > 2 && id.compare (id.size() - 2, 2, "On") == 0) || starts (id, "eqFreq") || starts (id, "eqMode")
            || starts (id, "glue") || starts (id, "dynImpact") || starts (id, "dynSustain") || id == "dynChannels" || id == "satChannels"
            || id == "maxBassFirst" || id == "stemKeep")
            return {};
        for (const char* m : { "ul", "stem", "ts", "rt", "eq", "bass", "dyn", "img", "sat", "clip" })
            if (starts (id, m)) return m;
        if (id == "drive" || id == "ceiling" || id == "release" || id == "adaptive" || id == "inTrim") return "max";
        return {};
    }

    std::vector<Edit> planEdits (const Values& planned, const Values& fresh, const Values& current)
    {
        std::map<std::string, Edit> byModule;
        std::map<std::string, bool> worth;

        for (const auto& [id, f] : fresh)
        {
            const auto module = moduleOf (id);
            const auto curIt = current.find (id);
            if (module.empty() || curIt == current.end())
                continue;
            const double cur = curIt->second;
            const auto planIt = planned.find (id);
            const bool known = planIt != planned.end();
            const double plan = known ? planIt->second : f;
            const auto rule = ruleFor (id);

            double to = cur;
            bool significant = false;
            // Mono Below (0 = off): Auto-Master switching it on or off is a choice; while it keeps it on, the
            // frequency moves by the ratio, unless you switched it off yourself (then it stays off).
            const bool offSwitch = id == "bassMono" && ((plan < 1.0) != (f < 1.0));
            if (id == "bassMono" && ! offSwitch && (plan < 1.0 || cur < 1.0))
                continue;
            if (! known || rule.kind == Kind::hidden)
            {
                to = f;
                significant = rule.kind != Kind::hidden && std::abs (f - cur) > 1.0e-6;
            }
            else if (rule.kind == Kind::choice || offSwitch)
            {
                if (std::abs (f - plan) > 1.0e-6)
                {
                    to = f;
                    significant = std::abs (f - cur) > 1.0e-6;
                }
            }
            else if (rule.kind == Kind::ratio)
            {
                if (plan > 0.0 && f > 0.0)
                {
                    const double factor = f / plan;
                    to = std::round (cur * factor);
                    // The same step up or down (80 -> 100 Hz and 100 -> 80 Hz), whatever the rounding.
                    significant = std::abs (std::log (factor)) >= std::log (1.0 + rule.worth) - 1.0e-4 && std::abs (to - cur) >= 1.0;
                }
            }
            else
            {
                const double delta = f - plan;
                to = roundTo (cur + delta, rule.step);
                // Planned values come back from the report as floats (-20.3 reads -20.2999992): a step of
                // exactly the worth must count.
                significant = std::abs (delta) >= rule.worth - 1.0e-4 && std::abs (to - cur) >= rule.worth * 0.5 - 1.0e-4;
            }
            if (std::abs (to - cur) <= 1.0e-6)
                continue;

            Change ch { id, cur, to, true };
            if (rule.kind == Kind::hidden)
                ch.shown = false;
            else if (rule.kind == Kind::additive && ! offSwitch && known)
                ch.shown = std::abs (to - cur) >= rule.worth * 0.5 - 1.0e-4;
            auto& e = byModule[module];
            e.module = module;
            e.changes.push_back (ch);
            worth[module] = worth[module] || significant;
        }

        std::vector<Edit> out;
        for (const char* m : kOrder)
        {
            const auto it = byModule.find (m);
            if (it != byModule.end() && worth[m])
                out.push_back (it->second);
        }
        return out;
    }

    bool sameAdvice (const std::vector<Edit>& a, const std::vector<Edit>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].module != b[i].module) return false;
            std::map<std::string, double> ta, tb;
            for (const auto& c : a[i].changes) if (c.shown) ta[c.id] = c.to;
            for (const auto& c : b[i].changes) if (c.shown) tb[c.id] = c.to;
            if (ta.size() != tb.size()) return false;
            for (const auto& [id, to] : ta)
            {
                const auto it = tb.find (id);
                if (it == tb.end()) return false;
                const double tol = ruleFor (id).kind == Kind::ratio ? std::max (1.0, 0.1 * std::abs (to)) : ruleFor (id).worth * 0.5;
                if (std::abs (it->second - to) > tol + 1.0e-9) return false;
            }
        }
        return true;
    }

    namespace
    {
        std::vector<TimeSpan> merged (std::vector<TimeSpan> v)
        {
            std::sort (v.begin(), v.end(), [] (const TimeSpan& x, const TimeSpan& y) { return x.start < y.start; });
            std::vector<TimeSpan> out;
            for (const auto& s : v)
                if (! out.empty() && s.start <= out.back().end) out.back().end = std::max (out.back().end, s.end);
                else out.push_back (s);
            return out;
        }
    }

    double overlapSeconds (const std::vector<TimeSpan>& a, const std::vector<TimeSpan>& b)
    {
        // Each side merged first: a stretch heard twice (a cycle loop) counts once.
        double total = 0.0;
        for (const auto& x : merged (a))
            for (const auto& y : merged (b))
                total += std::max (0.0, std::min (x.end, y.end) - std::max (x.start, y.start));
        return total;
    }

    double insideSeconds (const std::vector<TimeSpan>& part, const std::vector<TimeSpan>& hook)
    {
        const auto h = merged (hook);
        double total = 0.0;
        for (const auto& x : part)
            for (const auto& y : h)
                total += std::max (0.0, std::min (x.end, y.end) - std::max (x.start, y.start));
        return total;
    }

    bool loudestSettled (const SongListener& l) noexcept
    {
        return l.isPrepared() && l.hasWindow() && l.heardSeconds() >= kEnoughSeconds && l.secondsAfterLoudest() >= kSettledSeconds - 1.0e-9;
    }

    bool atHook (const SongStats& heard, const std::vector<TimeSpan>& hook)
    {
        if (hook.empty() || ! heard.positionFromHost || heard.loudestSpans.empty())
            return true;
        return insideSeconds (heard.loudestSpans, hook) >= std::min (kHookInsideSeconds, heard.loudestSeconds - 1.0) - 1.0e-6;
    }
}
