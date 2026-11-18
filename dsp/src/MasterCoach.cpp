#include "polisher/MasterCoach.h"

#include "polisher/AutoAnalyze.h"
#include "polisher/MixHandoff.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <cstdio>
#include <map>
#include <vector>

namespace polisher::coach
{
    namespace
    {
        const char* const kMinus = "\xE2\x88\x92";   // U+2212

        std::string num (double v, int decimals = 1)
        {
            // Locale-free (a host that sets a comma-decimal LC_NUMERIC would make printf write "4,1": invalid JSON).
            if (! std::isfinite (v)) v = 0.0;
            static constexpr long long kScale[] = { 1, 10, 100, 1000 };
            decimals = std::clamp (decimals, 0, 3);
            const double scaled = std::round (std::abs (v) * static_cast<double> (kScale[decimals]));
            if (scaled > 9.0e15) return "0";   // (nothing near this size is ever shown)
            const auto n = static_cast<long long> (scaled);
            std::string s = std::to_string (n / kScale[decimals]);
            if (decimals > 0)
            {
                const auto frac = std::to_string (n % kScale[decimals]);
                s += "." + std::string (static_cast<std::size_t> (decimals) - frac.size(), '0') + frac;
            }
            return v < 0.0 && n != 0 ? "-" + s : s;
        }
        /** For people: a real minus sign (and + when asked). */
        std::string text (double v, int decimals = 1, bool plus = false)
        {
            const auto s = num (std::abs (v), decimals);
            if (s == num (0.0, decimals)) return s;
            return v < 0.0 ? std::string (kMinus) + s : (plus ? "+" + s : s);
        }
        std::string esc (const std::string& s)
        {
            std::string o;
            for (const char ch : s)
            {
                const auto c = static_cast<unsigned char> (ch);
                if (c == '"' || c == '\\') { o += '\\'; o += ch; }
                else if (c == '\n') o += "\\n";
                else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof (b), "\\u%04x", c); o += b; }
                else o += ch;
            }
            return o;
        }
        std::string str (const std::string& s) { return "\"" + esc (s) + "\""; }
        std::string clip (const std::string& s, std::size_t n)
        {
            if (n == 0) return {};
            if (s.size() <= n) return s;
            std::size_t cut = n;
            while (cut > 0 && (static_cast<unsigned char> (s[cut]) & 0xC0) == 0x80) --cut;
            const auto space = s.rfind (' ', cut);
            if (space != std::string::npos && space > n / 2) cut = space;
            return s.substr (0, cut) + "\xE2\x80\xA6";
        }
        std::string firstLine (const std::string& s) { const auto nl = s.find ('\n'); return nl == std::string::npos ? s : s.substr (0, nl); }

        // Analysis bands (AutoMaster.cpp's kEdges / kBandNames)
        constexpr double kEdges[kEqBands + 1] { 20.0, 65.0, 180.0, 600.0, 1600.0, 3500.0, 8000.0, 20000.0 };
        const char* const kBand[kEqBands] { "Low end", "Bass", "Low mids", "Mids", "Upper mids", "Presence", "Air" };
        const char* const kBandKey[kEqBands] { "lowend", "bass", "lowmid", "mid", "uppermid", "presence", "air" };
        std::string range (int b)
        {
            auto hz = [] (double f) { return f >= 1000.0 ? num (f / 1000.0, f < 10000.0 ? 1 : 0) + "k" : num (f, 0); };
            return hz (kEdges[b]) + " - " + hz (kEdges[b + 1]) + " Hz";
        }

        struct ModuleInfo { const char* code; const char* key; const char* name; const char* caption; const char* onId; };
        const ModuleInfo kModules[] {
            { "ul",   "mas.unlimiter", "Unlimiter",       "Restore peaks",    "ulOn" },
            { "eq",   "mas.tonaleq",   "Tonal EQ",        "Balance tone",     "eqOn" },
            { "stem", "mas.stemeq",    "Stem EQ",         "Balance stems",    nullptr },
            { "ts",   "mas.steady",    "Tone Steady",     "Hold the tone",    "tsOn" },
            { "rt",   "mas.resonance", "Resonance Tamer", "Tame harsh spots", "rtOn" },
            { "bass", "mas.bass",      "Bass Control",    "Tighten low end",  "bassOn" },
            { "glue", "mas.glue",      "Glue",            "Glue",             nullptr },
            { "dyn",  "mas.dynamics",  "Dynamics",        "Control bands",    "dynOn" },
            { "img",  "mas.imager",    "Stereo Imager",   "Width",            "imgOn" },
            { "sat",  "mas.sat",       "Saturation",      "Add warmth",       "satOn" },
            { "clip", "mas.clipper",   "Soft Clipper",    "Catch peaks",      "clipOn" },
            { "max",  "mas.max",       "Maximizer",       "Loudness",         nullptr },
        };

        std::string aimName (const AutoMasterSettings& s)
        {
            if (s.reference.valid) return "your reference (" + s.reference.name + ")";
            const auto* style = handoff::mixStyle (s.styleKey);
            return style != nullptr ? std::string (style->name) : std::string (genreName (s.genre));
        }
        const char* intensityName (Intensity i) { return i == Intensity::gentle ? "Gentle" : i == Intensity::punchy ? "Punchy" : "Balanced"; }

        /** The tonal balance Auto-Master aims for, as analyseMix's bands read it (mean of bands 2..6 = 0). */
        std::array<double, kEqBands> aimBands (const AutoMasterSettings& s)
        {
            auto t = s.reference.valid ? s.reference.bandDb : styleTargetCurve (s.genre, s.styleKey);
            double ref = 0.0;
            for (int b = 1; b <= 5; ++b) ref += t[static_cast<std::size_t> (b)];
            ref /= 5.0;
            for (auto& v : t) v -= ref;
            return t;
        }

        struct Issue { std::string key, name; double from = 0.0, to = 0.0; bool warn = false; std::string text; };
        struct Result { std::string name; double before, after; std::string unit; bool better; int decimals; };
    }

    const char* moduleKey (const std::string& code) noexcept
    {
        for (const auto& m : kModules)
            if (code == m.code) return m.key;
        return "";
    }

    const char* rackKeyOfParam (const std::string& id) noexcept
    {
        static const std::pair<const char*, const char*> kPrefixes[] = {
            { "ul", "mas.unlimiter" }, { "eq", "mas.tonaleq" }, { "stem", "mas.stemeq" }, { "ts", "mas.steady" }, { "rt", "mas.resonance" },
            { "bass", "mas.bass" }, { "glue", "mas.glue" }, { "dyn", "mas.dynamics" }, { "img", "mas.imager" }, { "sat", "mas.sat" },
            { "clip", "mas.clipper" },
        };
        if (id == "bypass") return "";
        for (const auto& [prefix, key] : kPrefixes)
            if (id.rfind (prefix, 0) == 0) return key;
        return "mas.max";   // drive, ceiling, release, adaptive, input trim: the Maximizer's page
    }

    std::string planJson (const AutoMasterResult& r, const MixAnalysis* after, const AutoMasterSettings& s, const Heard& h, std::size_t maxBytes,
                          const std::vector<std::string>* changedIds)
    {
        const bool compared = changedIds != nullptr;
        const bool upToDate = compared && changedIds->empty();
        auto sameAsNow = [&] (const char* rackKey) {
            if (! compared) return false;
            for (const auto& id : *changedIds)
                if (std::string (rackKeyOfParam (id)) == rackKey) return false;
            return true;
        };
        const std::string summary = (upToDate ? std::string ("Masterology is already set the way I'd master this mix: nothing to change. ") : std::string())
                                  + r.summary;
        const auto aim = aimName (s);
        if (! r.ok)
        {
            std::string o = "{\"ok\":false,\"kind\":\"master\",\"summary\":" + str (clip (r.summary, 500)) + ",\"tipTitle\":"
                          + str (clip (r.tipTitle.empty() ? std::string ("Auto-Master couldn't build a plan") : r.tipTitle, 120)) + ",\"tip\":" + str (clip (r.tip, 400)) + "}";
            return o.size() <= maxBytes ? o : std::string ("{\"ok\":false,\"kind\":\"master\",\"summary\":\"No plan.\"}");
        }
        const auto& a = r.analysis;
        const auto target = aimBands (s);

        // What it heard.
        std::vector<std::string> heard;
        {
            const int mins = static_cast<int> (h.seconds) / 60, secs = static_cast<int> (h.seconds) % 60;
            heard.push_back ("Listened to " + (mins > 0 ? std::to_string (mins) + ":" + (secs < 10 ? "0" : "") + std::to_string (secs) : std::to_string (secs) + " s")
                             + " of the song" + (h.wholeSong ? " (the whole song)" : ""));
            if (h.songLufs > -70.0) heard.push_back ("The song: " + text (h.songLufs) + " LUFS, loudness range " + num (h.songLra) + " LU");
            heard.push_back ("Its loudest 12 s: " + text (a.loudness.integratedLufs) + " LUFS, peaks " + text (a.loudness.truePeakDbtp) + " dBTP");
            heard.push_back ("Punch (peak to loudness): " + num (a.peakToLoudnessDb) + " dB");
            if (a.hasLowEnd) heard.push_back ("Bass below 150 Hz: " + num (a.bassWidthPercent, 0) + " % stereo");
            heard.push_back ("Stereo: correlation " + num (a.correlation, 2) + (a.midSideDb > -90.0 ? ", sides of the mids " + text (a.midSideDb, 0) + " dB" : std::string()));
        }

        // What stands out: the tone against the aim, loudness, peaks, punch, the low end, phase.
        std::vector<Issue> issues;
        for (int b = 0; b < kEqBands; ++b)
        {
            const double d = a.bandDb[static_cast<std::size_t> (b)] - target[static_cast<std::size_t> (b)];
            if (std::abs (d) < 2.5) continue;
            Issue i;
            i.key = kBandKey[b];
            std::string band = kBand[b];
            band[0] = static_cast<char> (std::tolower (static_cast<unsigned char> (band[0])));
            i.name = (d > 0 ? "Too much " : "Not enough ") + band;
            i.from = kEdges[b];
            i.to = kEdges[b + 1];
            i.warn = std::abs (d) >= 4.0;
            i.text = std::string (kBand[b]) + " (" + range (b) + ") sits " + text (d, 1, true) + " dB against " + aim;
            issues.push_back (std::move (i));
        }
        if (a.loudness.integratedLufs < r.targetLufs - 3.0)
            issues.push_back ({ "quiet", "Quieter than the target", 0, 0, false,
                                "its loudest part comes in at " + text (a.loudness.integratedLufs) + " LUFS; the target is " + text (r.targetLufs) + " LUFS" });
        if (a.loudness.truePeakDbtp > -0.3)
            issues.push_back ({ "peaks", "Peaks at 0 dB", 0, 0, true, "true peaks reach " + text (a.loudness.truePeakDbtp) + " dBTP: they would clip on playback" });
        if (a.peakToLoudnessDb < 7.0)
            issues.push_back ({ "squashed", "Already squashed", 0, 0, true, "only " + num (a.peakToLoudnessDb) + " dB from the peaks to the loudness: the mix is limited already" });
        if (a.hasLowEnd && a.bassWidthPercent > 35.0)
            issues.push_back ({ "widelow", "Wide low end", 20, 150, true, num (a.bassWidthPercent, 0) + " % of the bass below 150 Hz is stereo: it weakens on mono speakers and clubs" });
        if (a.correlation < 0.15)
            issues.push_back ({ "phase", "Out of phase risk", 0, 0, true, "left and right correlate only " + num (a.correlation, 2) + ": parts may vanish in mono" });
        std::stable_sort (issues.begin(), issues.end(), [] (const Issue& x, const Issue& y) { return x.warn && ! y.warn; });
        if (issues.size() > 8) issues.resize (8);

        // The chain: every module the plan has on, with its reasons.
        const auto values = autoanalyze::chainValues (r.chain);
        auto valueOf = [&values] (const char* id, double fallback) { const auto it = values.find (id); return it != values.end() ? it->second : fallback; };
        std::map<std::string, std::vector<const AutoMasterReason*>> reasons;
        for (const auto& x : r.reasons) reasons[x.module].push_back (&x);
        // Per module, the moves first ("0 dB", "0 %": what it left alone, last).
        for (auto& [code, list] : reasons)
            std::stable_sort (list.begin(), list.end(), [] (const AutoMasterReason* a, const AutoMasterReason* b) {
                auto still = [] (const std::string& v) { return v.rfind ("0 ", 0) == 0 || v == "Off"; };
                return ! still (a->value) && still (b->value);
            });
        int modulesOn = 0;
        struct Row { const ModuleInfo* m; bool on; };
        std::vector<Row> rows;
        for (const auto& m : kModules)
        {
            bool on = m.onId != nullptr ? valueOf (m.onId, 0.0) >= 0.5 : std::string (m.code) == "max";
            if (std::string (m.code) == "ul") on = on && valueOf ("ulAmount", 0.0) >= 0.5;   // on at 0 % plays it through
            if (std::string (m.code) == "ts") on = on && valueOf ("tsAmount", 0.0) >= 0.5;
            if (std::string (m.code) == "rt") on = on && valueOf ("rtAmount", 0.0) >= 0.5;
            if (std::string (m.code) == "stem") on = s.stem.enabled && std::any_of (s.stem.gainDb.begin(), s.stem.gainDb.end(), [] (double g) { return std::abs (g) > 0.05; });
            if (std::string (m.code) == "glue") on = false;   // Auto-Master leaves Glue to you (it sets it neutral)
            if (on) ++modulesOn;
            if (on || reasons.count (m.code) > 0) rows.push_back ({ &m, on });
        }

        // The results: measured on the same 12 s, before and through the plan.
        std::vector<Result> results;
        const auto& out = r.output;
        results.push_back ({ "Loudness (loudest 12 s)", a.loudness.integratedLufs, out.integratedLufs, "LUFS",
                             std::abs (out.integratedLufs - r.targetLufs) < std::abs (a.loudness.integratedLufs - r.targetLufs), 1 });
        results.push_back ({ "True peak", a.loudness.truePeakDbtp, out.truePeakDbtp, "dBTP", out.truePeakDbtp <= r.chain.limiter.ceilingDbtp + 0.1, 1 });
        if (after != nullptr && after->usable)
        {
            results.push_back ({ "Punch (peak to loudness)", a.peakToLoudnessDb, after->peakToLoudnessDb, "dB", after->peakToLoudnessDb >= 6.0, 1 });
            if (a.hasLowEnd && std::max (a.bassWidthPercent, after->bassWidthPercent) >= 5.0)
                results.push_back ({ "Bass stereo (below 150 Hz)", a.bassWidthPercent, after->bassWidthPercent, "%", after->bassWidthPercent <= a.bassWidthPercent + 1.0, 0 });
            for (int b = 0; b < kEqBands; ++b)
            {
                const double was = a.bandDb[static_cast<std::size_t> (b)] - target[static_cast<std::size_t> (b)];
                const double now = after->bandDb[static_cast<std::size_t> (b)] - target[static_cast<std::size_t> (b)];
                if (std::abs (was) >= 1.5 && std::abs (now - was) >= 0.5)
                    results.push_back ({ std::string (kBand[b]) + " vs the aim", was, now, "dB", std::abs (now) < std::abs (was), 1 });
            }
        }
        if (results.size() > 8) results.resize (8);

        std::vector<std::string> first;
        for (const auto& n : r.notes)
            if (first.size() < 3) first.push_back (firstLine (n));
        first.push_back ("Compare with A / B and MATCH on: MATCH plays both at the same loudness, so you judge the master, not the volume.");

        // Shorter reasons, then fewer per module, then none, until it fits.
        struct Pass { std::size_t why, per, summary; };
        const Pass kPasses[] { { 220, 4, 600 }, { 150, 3, 500 }, { 110, 2, 400 }, { 70, 2, 300 }, { 50, 1, 240 }, { 0, 2, 200 } };
        for (const auto& pass : kPasses)
        {
            const auto why = pass.why;
            std::string o = "{\"ok\":true,\"kind\":\"master\",\"summary\":" + str (clip (summary, pass.summary))
                          + ",\"genre\":" + str (genreName (s.genre)) + ",\"intensity\":" + str (intensityName (s.intensity))
                          + ",\"target\":" + str (handoff::targetName (static_cast<int> (s.target))) + ",\"targetLufs\":" + num (r.targetLufs)
                          + ",\"aim\":" + str (aim) + ",\"reference\":" + str (s.reference.valid ? s.reference.name : std::string());
            o += ",\"heard\":[";
            for (std::size_t i = 0; i < heard.size(); ++i) o += (i ? "," : "") + str (heard[i]);
            o += "],\"issues\":[";
            for (std::size_t i = 0; i < issues.size(); ++i)
            {
                const auto& x = issues[i];
                o += (i ? "," : "") + std::string ("{\"key\":") + str (x.key) + ",\"name\":" + str (x.name) + ",\"from\":" + num (x.from, 0)
                   + ",\"to\":" + num (x.to, 0) + ",\"warn\":" + (x.warn ? "true" : "false") + ",\"text\":" + str (clip (x.text, why > 0 ? why : 80)) + "}";
            }
            o += "],\"chain\":[";
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                const auto& m = *rows[i].m;
                o += (i ? "," : "") + std::string ("{\"key\":") + str (m.key) + ",\"name\":" + str (m.name) + ",\"on\":" + (rows[i].on ? "true" : "false")
                   + ",\"caption\":" + str (m.caption) + ",\"kept\":" + (reasons.count (m.code) ? "false" : "true")
                   + (compared ? std::string (",\"same\":") + (sameAsNow (m.key) ? "true" : "false") : std::string()) + ",\"set\":[";
                const auto it = reasons.find (m.code);
                if (it != reasons.end())
                    for (std::size_t k = 0; k < it->second.size() && k < pass.per; ++k)
                    {
                        const auto& x = *it->second[k];
                        o += (k ? "," : "") + std::string ("{\"control\":") + str (clip (x.control, 40)) + ",\"value\":" + str (clip (x.value, 60))
                           + ",\"why\":" + str (clip (x.why, why)) + "}";
                    }
                o += "]}";
            }
            o += "],\"changes\":" + std::to_string (compared ? changedIds->size() : values.size()) + ",\"modulesOn\":" + std::to_string (modulesOn);
            if (compared) o += std::string (",\"upToDate\":") + (upToDate ? "true" : "false");
            o += ",\"bands\":{\"hz\":[";
            for (int b = 0; b < kEqBands; ++b) o += (b ? "," : "") + num (std::sqrt (kEdges[b] * kEdges[b + 1]), 0);
            auto curve = [] (const std::array<double, kEqBands>& v) {
                std::string c = "[";
                for (int b = 0; b < kEqBands; ++b) c += (b ? "," : "") + num (std::clamp (v[static_cast<std::size_t> (b)], -24.0, 24.0));
                return c + "]";
            };
            o += "],\"before\":" + curve (a.bandDb) + ",\"target\":" + curve (target);
            if (after != nullptr && after->usable) o += ",\"after\":" + curve (after->bandDb);
            o += "},\"results\":[";
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                const auto& x = results[i];
                o += (i ? "," : "") + std::string ("{\"name\":") + str (x.name) + ",\"before\":" + num (x.before, x.decimals) + ",\"after\":" + num (x.after, x.decimals)
                   + ",\"unit\":" + str (x.unit) + ",\"better\":" + (x.better ? "true" : "false") + "}";
            }
            o += "],\"moves\":[],\"first\":[";
            for (std::size_t i = 0; i < first.size(); ++i) o += (i ? "," : "") + str (clip (first[i], why > 0 ? why : 120));
            o += "]}";
            if (o.size() <= maxBytes) return o;
        }
        const std::string o = "{\"ok\":true,\"kind\":\"master\",\"summary\":" + str (clip (summary, 200)) + ",\"changes\":"
                            + std::to_string (compared ? changedIds->size() : values.size())
                            + ",\"modulesOn\":" + std::to_string (modulesOn) + ",\"chain\":[],\"issues\":[],\"results\":[],\"moves\":[],\"heard\":[],\"first\":[]}";
        return o.size() <= maxBytes ? o : std::string ("{\"ok\":true,\"kind\":\"master\",\"summary\":\"\"}");
    }
}
