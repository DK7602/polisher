#pragma once

#include "LoudnessTargeting.h"
#include "SongListener.h"

#include <map>
#include <string>
#include <vector>

/** Auto Analyze: after Auto-Master, Masterology keeps listening while the song plays. Whenever it hears the
    part the master was set from again (the hook or drop, or a repeat of it), it runs Auto-Master's decisions
    once more on the mix as it arrives now, and offers what changed as edits in Learn ("now -> new", per
    module). It never changes anything by itself.

    An edit moves a control by what Auto-Master would change (its new choice minus the one it made before),
    starting from where the control is now: a knob you turned yourself keeps your offset. Framework-free and
    unit-tested (tests/test_autoanalyze.cpp); the plug-in's AutoMasterController does the listening. */
namespace polisher::autoanalyze
{
    /** Control values by the plug-in's parameter id, in the units the controls show (dB, %, ms, Hz, a
        choice's index, 0 / 1 for a switch). */
    using Values = std::map<std::string, double>;

    /** The values Auto-Master sets for a chain: every control it decides (the plug-in sets exactly these,
        and keeps them as its report's "values"), plus ulSquashSet (a hidden control that goes with the
        Unlimiter). */
    Values chainValues (const MasterChainParams& chain);

    /** One control an edit moves. */
    struct Change
    {
        std::string id;
        double from = 0.0, to = 0.0;
        bool shown = true;   // false: a move too small to list (it still goes with the edit), or a hidden control
    };

    /** The edits for one module ("ul", "eq", "stem", "ts", "rt", "bass", "dyn", "img", "sat", "clip", "max"). */
    struct Edit
    {
        std::string module;
        std::vector<Change> changes;
    };

    /** The module a control belongs to ("" for one Auto Analyze never moves: the module switches, the
        controls Auto-Master always sets the same way, such as band frequencies, Glue, Impact and Sustain). */
    std::string moduleOf (const std::string& id);

    /** Per control, the values Auto-Master has already chosen for this mix that you declined with KEEP MINE (the
        plan before, and what it offered then). */
    using Kept = std::map<std::string, std::vector<double>>;

    /** What to offer. planned: what Auto-Master chose last time (its report's values, updated as edits are
        applied or kept); fresh: what it chooses for the mix now; current: the controls now. Per control:
        a switch or choice Auto-Master changed its mind on goes to the new choice (so does a Stem EQ
        suggestion: planned is the suggestion already on offer); a knob moves by
        fresh - planned (frequencies and times by the ratio), from where it is now. A module is offered when
        at least one of its controls moves by a step worth hearing (e.g. 0.75 dB of EQ or Drive, 1 dB of Clip
        Level, 1.5 dB of a threshold: each larger than what the same mix measures from one playback to the
        next); its smaller moves go with it. A control missing from planned goes to fresh; one missing from
        fresh stays. Modules in signal-chain order.

        kept (KEEP MINE): a knob counts from the nearest edge of the range its kept values and the plan span (a
        new choice inside it moves nothing), and a choice or switch that goes back to a kept value moves
        nothing: declined advice doesn't come back, nor its opposite, unless the mix really changed. */
    std::vector<Edit> planEdits (const Values& planned, const Values& fresh, const Values& current, const Kept* kept = nullptr);

    /** Whether two lists offer the same edits (same modules, same targets within a small tolerance): Learn's
        cards change only when the advice really changed. */
    bool sameAdvice (const std::vector<Edit>& a, const std::vector<Edit>& b);

    /** Seconds two sets of timeline spans share. */
    double overlapSeconds (const std::vector<TimeSpan>& a, const std::vector<TimeSpan>& b);

    // ---- When to judge: like with like ------------------------------------------------------------------
    // Auto-Master set the master from the song's loudest 12 s (the hook or drop). A check on a part that is
    // partly verse hears a quieter mix and asks for more Drive although nothing changed, so a check judges
    // only a loudest part that is over (not still growing at the playhead into the hook) and lies almost
    // wholly inside the report's hook spans.
    inline constexpr double kEnoughSeconds = 20.0;       // music heard on this playback before the first check
    inline constexpr double kSettledSeconds = 3.0;       // the loudest part heard ended at least this much music ago
    inline constexpr double kHookInsideSeconds = 11.0;   // of the 12 s judged, at least this much inside the hook spans

    /** Seconds of `part` that lie inside `hook`: each of part's spans counts on its own (a cycle loop over the
        hook heard twice is all hook), hook's spans merged. */
    double insideSeconds (const std::vector<TimeSpan>& part, const std::vector<TimeSpan>& hook);

    /** The loudest part a listener heard is ready to be judged: a whole window of it, at least kEnoughSeconds
        of music heard, and the part ended at least kSettledSeconds of music ago. Safe while it listens. */
    bool loudestSettled (const SongListener& listener) noexcept;

    /** A part heard (heard.loudestSpans) plays where the report's hook does: at least kHookInsideSeconds of it
        inside the hook spans. True when there's nothing to compare with (no hook spans, or no timeline
        positions while it listened). */
    bool atHook (const SongStats& heard, const std::vector<TimeSpan>& hook);
}
