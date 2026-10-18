#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <vector>

/** Which controls belong to undo / redo and to snapshots: everything that shapes the sound. Not A/B (the
    global bypass), MATCH, the meter preferences, nor Auto-Master's genre / intensity / target (they say
    what Auto-Master aims for next time; they don't change what you hear), nor the hidden State Revision
    (it only tells the host the project changed). */
namespace SettingsScope
{
    inline bool isSetting (const juce::String& id)
    {
        return id != "bypass" && id != "levelMatch" && id != "autoreset" && id != "resetOnChange"
            && id != "amGenre" && id != "amIntensity" && id != "amTarget" && id != "stateRev";
    }

    inline const juce::String& minus() { static const auto s = juce::String::fromUTF8 ("\xE2\x88\x92"); return s; }
    inline const juce::String& arrow() { static const auto s = juce::String::fromUTF8 (" \xE2\x86\x92 "); return s; }

    /** A control's name as the window shows it (the parameter names are what a host's automation lanes
        show): Dynamics bands by the window's band names ("Dynamics LO-MID Threshold" for "Dynamics
        Threshold 2"), "Clip Level" as on the Soft Clipper's knob. */
    inline juce::String displayName (const juce::RangedAudioParameter& p)
    {
        static const char* const bands[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
        const auto id = p.getParameterID();
        if (id == "clipLevel")
            return "Clip Level";
        const int n = id.getTrailingIntValue();
        if (id.startsWith ("dynX") && n >= 1 && n <= 3)   // the split between two bands
            return juce::String ("Dynamics Split ") + bands[n - 1] + " / " + bands[n];
        static const char* const perBand[] = { "dynThr", "dynRatio", "dynAtk", "dynRel", "dynGain", "dynImpact", "dynSustain" };
        for (const char* prefix : perBand)
            if (id.startsWith (prefix) && n >= 1 && n <= 4)
            {
                // "Dynamics Threshold 2" -> "Dynamics LO-MID Threshold"
                const auto what = p.getName (64).upToLastOccurrenceOf (" ", false, false).fromFirstOccurrenceOf ("Dynamics ", false, false);
                return juce::String ("Dynamics ") + bands[n - 1] + " " + what;
            }
        return p.getName (64);
    }

    /** What one control did, in plain words, from the value before to the value after:
        "Drive 4.5 → 8.0 dB", "Ceiling −1.00 → −1.90 dBTP", "Clipper on", "Saturation Mode: Tube",
        "Bass Mono Below Off → 100 Hz". Values are the parameter's 0..1 values. */
    inline juce::String describe (const juce::RangedAudioParameter& p, float before, float after)
    {
        auto name = displayName (p);
        if (dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr)
        {
            if (name.endsWith (" On")) name = name.dropLastCharacters (3);
            return name + (after > 0.5f ? " on" : " off");
        }
        if (dynamic_cast<const juce::AudioParameterChoice*> (&p) != nullptr)
            return name + ": " + p.getText (after, 64);
        // A value a hair under zero reads "-0.0" (float rounding, e.g. on macOS): say 0.0.
        auto text = [&p] (float v)
        {
            auto t = p.getText (v, 64);
            if (t.startsWithChar ('-') && juce::CharacterFunctions::isDigit (t[1]) && t.substring (1).getDoubleValue() == 0.0)
                t = t.substring (1);
            return t.replace ("-", minus());
        };
        const auto a = text (before), b = text (after);
        // The same unit on both sides is said once: "4.5 → 8.0 dB" rather than "4.5 dB → 8.0 dB".
        const auto unitA = a.fromLastOccurrenceOf (" ", false, false), unitB = b.fromLastOccurrenceOf (" ", false, false);
        if (a.containsChar (' ') && b.containsChar (' ') && unitA == unitB && unitA.isNotEmpty())
            return name + " " + a.upToLastOccurrenceOf (" ", false, false) + arrow() + b.upToLastOccurrenceOf (" ", false, false) + " " + unitA;
        return name + " " + a + arrow() + b;
    }

    /** The value a control takes when a snapshot doesn't have it (the control was added after the slot
        was stored): its default, except Stem EQ Keep Loudness, which a song from before it existed plays
        without (the same rule as PolisherAudioProcessor::setStateInformation). 0..1 value. */
    inline float valueWhenMissing (const juce::RangedAudioParameter& p)
    {
        return p.getParameterID() == "stemKeep" ? 0.0f : p.getDefaultValue();
    }
}

/** Masterology's own undo / redo: one linear list of steps, each holding what changed (0..1 values before
    and after, by parameter). Lives in the processor, so it survives closing and reopening the window;
    it is not saved with the project (a project or preset load clears it). Message thread only.

    Steps come from
      - the user moving a control in Masterology's window. JUCE's attachments wrap every user change in a
        change gesture (a knob drag from press to release, a typed value, a switch, a choice), so one
        gesture = one step. Gestures that overlap (dragging an EQ point moves its gain and frequency) are
        one step, and quick repeats on the same control (mouse wheel, arrow keys) merge into one.
        Host automation and the DAW's own changes never come with a gesture here, so they never flood it;
      - changes made in code that undo as one: Auto-Master, Apply of the Stem EQ suggestion, a snapshot
        recall (beginGroup / endGroup). A group may also carry an "extra" state (the Auto-Master report),
        so undoing Auto-Master also brings back the report that was there before.
    A new change after an undo drops what could have been redone (a linear history). At most kMaxSteps;
    the oldest go first. */
class SettingsHistory final : private juce::AudioProcessorListener
{
public:
    static constexpr int kMaxSteps = 100;

    explicit SettingsHistory (juce::AudioProcessor& p) : processor (p)
    {
        for (auto* param : processor.getParameters())
            params.push_back (dynamic_cast<juce::RangedAudioParameter*> (param));
        processor.addListener (this);
    }

    ~SettingsHistory() override { processor.removeListener (this); }

    /** The extra state a group step carries (the Auto-Master report): how to read and restore it. */
    void setExtraState (std::function<juce::String()> get, std::function<void (const juce::String&)> set)
    {
        getExtra = std::move (get);
        setExtra = std::move (set);
    }
    juce::String captureExtra() const { return getExtra ? getExtra() : juce::String(); }
    void restoreExtra (const juce::String& s) { if (setExtra) setExtra (s); }

    /** Changes made in code between these two are one step called `label` (nesting is allowed: only the
        outermost pair counts). Nothing changed = no step. */
    void beginGroup (const juce::String& label)
    {
        syncPending();
        if (groupDepth++ > 0)
            return;
        groupLabel = label;
        groupBefore = captureAll();
        groupExtraBefore = captureExtra();
    }

    void endGroup()
    {
        if (groupDepth == 0 || --groupDepth > 0)
            return;
        syncPending();
        Step s;
        s.label = groupLabel;
        for (size_t i = 0; i < params.size() && i < groupBefore.size(); ++i)
            if (params[i] != nullptr && recordedInGroup (params[i]->getParameterID()) && ! juce::exactlyEqual (params[i]->getValue(), groupBefore[i]))
                s.changes.push_back ({ static_cast<int> (i), groupBefore[i], params[i]->getValue() });
        s.extraAfter = captureExtra();
        s.extraBefore = groupExtraBefore;
        s.hasExtra = s.extraAfter != s.extraBefore;
        if (! s.changes.empty() || s.hasExtra)
            push (std::move (s));
        canMerge = false;
    }

    bool canUndo()       { syncPending(); return pos > 0; }
    bool canRedo()       { syncPending(); return pos < static_cast<int> (steps.size()); }
    /** What Undo / Redo would do ("Drive +2.0 dB"), or "" when there is nothing. */
    juce::String undoLabel() { return canUndo() ? steps[static_cast<size_t> (pos - 1)].label : juce::String(); }
    juce::String redoLabel() { return canRedo() ? steps[static_cast<size_t> (pos)].label : juce::String(); }
    int size()                { syncPending(); return static_cast<int> (steps.size()); }

    /** Puts back what the last step changed. Returns its label ("" = nothing to undo). */
    juce::String undo()
    {
        if (! canUndo() || groupDepth > 0)
            return {};
        finishOpenGesture();
        const auto& s = steps[static_cast<size_t> (--pos)];
        apply (s, false);
        canMerge = false;
        return s.label;
    }

    /** Does the step that was undone again. Returns its label ("" = nothing to redo). */
    juce::String redo()
    {
        if (! canRedo() || groupDepth > 0)
            return {};
        finishOpenGesture();
        const auto& s = steps[static_cast<size_t> (pos++)];
        apply (s, true);
        canMerge = false;
        return s.label;
    }

    /** From any thread (a project or preset load): the steps no longer apply. Done on the next call
        from the message thread. */
    void requestClear() noexcept { clearPending.store (true); }

    /** Tests: how close together repeats of one control merge into one step (0 = never). */
    void setMergeWindowMs (double ms) noexcept { mergeWindowMs = ms; }

private:
    struct Change { int index; float before, after; };
    struct Step
    {
        juce::String label;
        std::vector<Change> changes;
        juce::String extraBefore, extraAfter;
        bool hasExtra = false, fromGesture = false;
        double time = 0.0;
    };

    static bool recordedInGroup (const juce::String& id)
    {
        // Auto-Master switches to B (bypass off) so you hear its result; undoing it puts back A or B as it was.
        return SettingsScope::isSetting (id) || id == "bypass";
    }

    static bool onMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
    }

    void syncPending()
    {
        if (clearPending.exchange (false))
        {
            steps.clear();
            pos = 0;
            gestureBefore.clear();   // a drag in progress during a project load is not recorded
            canMerge = false;
        }
    }

    std::vector<float> captureAll() const
    {
        std::vector<float> v (params.size(), 0.0f);
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i] != nullptr) v[i] = params[i]->getValue();
        return v;
    }

    void push (Step s)
    {
        s.time = juce::Time::getMillisecondCounterHiRes();
        steps.erase (steps.begin() + pos, steps.end());
        steps.push_back (std::move (s));
        if (steps.size() > static_cast<size_t> (kMaxSteps))
            steps.erase (steps.begin());
        pos = static_cast<int> (steps.size());
    }

    void apply (const Step& s, bool forward)
    {
        const juce::ScopedValueSetter<bool> svs (applying, true);
        auto set = [this] (const Change& c, float v)
        {
            auto* p = params[static_cast<size_t> (c.index)];
            if (p == nullptr || juce::exactlyEqual (p->getValue(), v))
                return;
            // As one gesture per control, like a knob move, so a host recording automation sees it.
            const bool held = std::find (openGestures.begin(), openGestures.end(), c.index) != openGestures.end();
            if (! held) p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            if (! held) p->endChangeGesture();
        };
        if (forward)
            for (const auto& c : s.changes) set (c, c.after);
        else
            for (auto it = s.changes.rbegin(); it != s.changes.rend(); ++it) set (*it, it->before);
        if (s.hasExtra)
            restoreExtra (forward ? s.extraAfter : s.extraBefore);
    }

    juce::String describe (const std::vector<Change>& changes) const
    {
        if (changes.size() == 1)
        {
            const auto& c = changes.front();
            return SettingsScope::describe (*params[static_cast<size_t> (c.index)], c.before, c.after);
        }
        // Several controls in one gesture (an EQ point): their shared name, "EQ Low End (2 controls)".
        juce::StringArray common;
        bool first = true;
        for (const auto& c : changes)
        {
            juce::StringArray words;
            words.addTokens (SettingsScope::displayName (*params[static_cast<size_t> (c.index)]), " ", "");
            if (first) { common = words; first = false; continue; }
            int n = 0;
            while (n < common.size() && n < words.size() && common[n] == words[n]) ++n;
            common.removeRange (n, common.size() - n);
        }
        const auto count = juce::String (static_cast<int> (changes.size())) + " controls";
        return common.isEmpty() ? count : common.joinIntoString (" ") + " (" + count + ")";
    }

    // --- Gestures from the window (AudioProcessorListener; parameter changes themselves are not needed) ----
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}   // may run on the audio thread
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override
    {
        if (applying || groupDepth > 0 || ! onMessageThread() || ! isUserSetting (index))
            return;
        syncPending();
        if (std::find (openGestures.begin(), openGestures.end(), index) == openGestures.end())
            openGestures.push_back (index);
        const bool known = std::any_of (gestureBefore.begin(), gestureBefore.end(), [index] (const auto& g) { return g.first == index; });
        if (! known)
            gestureBefore.emplace_back (index, params[static_cast<size_t> (index)]->getValue());
    }

    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override
    {
        if (applying || groupDepth > 0 || ! onMessageThread() || ! isUserSetting (index))
            return;
        openGestures.erase (std::remove (openGestures.begin(), openGestures.end(), index), openGestures.end());
        if (openGestures.empty())
            commitGesture();
    }

    bool isUserSetting (int index) const
    {
        return index >= 0 && static_cast<size_t> (index) < params.size() && params[static_cast<size_t> (index)] != nullptr
            && SettingsScope::isSetting (params[static_cast<size_t> (index)]->getParameterID());
    }

    /** Undo / redo pressed while a control is still held: what it changed so far becomes its own step first. */
    void finishOpenGesture()
    {
        if (! gestureBefore.empty())
            commitGesture();
    }

    void commitGesture()
    {
        syncPending();
        std::vector<Change> changes;
        for (const auto& [index, before] : gestureBefore)
        {
            const float after = params[static_cast<size_t> (index)]->getValue();
            if (! juce::exactlyEqual (after, before))
                changes.push_back ({ index, before, after });
        }
        gestureBefore.clear();
        if (changes.empty())
            return;

        // Repeats on the same control(s) in quick succession (mouse wheel, arrow keys) are one step.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (canMerge && pos == static_cast<int> (steps.size()) && ! steps.empty())
        {
            auto& top = steps.back();
            const bool same = top.fromGesture && top.changes.size() == changes.size()
                           && std::equal (changes.begin(), changes.end(), top.changes.begin(), [] (const Change& a, const Change& b) { return a.index == b.index; });
            if (same && now - top.time < mergeWindowMs)
            {
                for (size_t k = 0; k < changes.size(); ++k)
                    top.changes[k].after = changes[k].after;
                top.changes.erase (std::remove_if (top.changes.begin(), top.changes.end(), [] (const Change& c) { return juce::exactlyEqual (c.after, c.before); }),
                                   top.changes.end());
                top.time = now;
                if (top.changes.empty()) { steps.pop_back(); pos = static_cast<int> (steps.size()); canMerge = false; }
                else top.label = describe (top.changes);
                return;
            }
        }
        Step s;
        s.changes = std::move (changes);
        s.label = describe (s.changes);
        s.fromGesture = true;
        push (std::move (s));
        canMerge = true;
    }

    juce::AudioProcessor& processor;
    std::vector<juce::RangedAudioParameter*> params;          // by parameter index
    std::vector<Step> steps;
    int pos = 0;                                               // steps[0 .. pos) are done; the rest can be redone
    std::vector<int> openGestures;
    std::vector<std::pair<int, float>> gestureBefore;          // value of each control when its gesture began
    bool applying = false, canMerge = false;
    double mergeWindowMs = 1000.0;
    int groupDepth = 0;
    juce::String groupLabel, groupExtraBefore;
    std::vector<float> groupBefore;
    std::function<juce::String()> getExtra;
    std::function<void (const juce::String&)> setExtra;
    std::atomic<bool> clearPending { false };

    JUCE_DECLARE_NON_COPYABLE (SettingsHistory)
};

/** Snapshots S1..S4: four slots that each hold the whole sound (every setting in SettingsScope, as plain
    values by parameter ID, plus the Auto-Master report that explains them). Saved with the project.
    Recall goes through the parameters like a knob move (so every module glides as usual) and is one
    undo step. store / recall / getStatus: message thread; toXml / fromXml: any thread. */
class SettingsSnapshots
{
public:
    static constexpr int kSlots = 4;
    static constexpr int kFormat = 1;   // the "version" attribute of the saved element

    SettingsSnapshots (juce::AudioProcessor& p, SettingsHistory& h) : history (h)
    {
        for (auto* param : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                byId[rp->getParameterID()] = rp;
                numSettings += SettingsScope::isSetting (rp->getParameterID()) ? 1 : 0;
            }
    }

    /** The slot whose exact settings are in place changed (a recall, an undo back onto a slot, a change
        that leaves it; -1 = none). For MATCH's per-snapshot level. */
    std::function<void (int leaving, int entering)> onSwitch;
    /** A slot got new settings (they are in place now). */
    std::function<void (int slot)> onStore;

    void store (int slot)
    {
        if (! isSlot (slot))
            return;
        Slot s;
        s.filled = true;
        for (const auto& [id, p] : byId)
            if (SettingsScope::isSetting (id))
                s.values.emplace_back (id, p->convertFrom0to1 (p->getValue()));
        s.report = history.captureExtra();
        s.time = juce::Time::getCurrentTime().toISO8601 (true);
        s.note = history.undoLabel();      // what the settings came from: the last change before storing
        {
            const juce::ScopedLock sl (lock);
            slots[static_cast<size_t> (slot)] = std::move (s);
            active = playing = slot;
        }
        if (onStore) onStore (slot);
    }

    /** Recalls a stored slot (one undo step, "Recall Snapshot 2"). False for an empty slot. */
    bool recall (int slot)
    {
        if (! isSlot (slot))
            return false;
        Slot s;
        int leaving = -1;
        {
            const juce::ScopedLock sl (lock);
            s = slots[static_cast<size_t> (slot)];
            leaving = inPlaceLocked();
        }
        if (! s.filled)
            return false;

        history.beginGroup ("Recall Snapshot " + juce::String (slot + 1));
        // Every setting gets the slot's value: the stored one or, for a control added after the slot was
        // stored, the value a song from before that control plays with (so the slot sounds as it did).
        // Stored controls this version doesn't have are ignored.
        for (const auto& [id, p] : byId)
        {
            if (! SettingsScope::isSetting (id))
                continue;
            const float v = slotValue (s, id, *p);
            if (juce::exactlyEqual (p->getValue(), v))
                continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
        history.restoreExtra (s.report);
        history.endGroup();
        {
            const juce::ScopedLock sl (lock);
            active = playing = slot;
        }
        if (onSwitch && leaving != slot) onSwitch (leaving, slot);
        return true;
    }

    struct SlotInfo { bool filled = false; juce::String time, note; };
    struct Status
    {
        std::array<SlotInfo, kSlots> slots;
        int active = -1;          // the slot last stored or recalled (or whose settings are playing now)
        bool modified = false;    // the settings changed since
    };

    /** Also notices when the settings leave a slot or land back on one (undo, redo, the knobs moved back),
        and tells onSwitch. The editor calls it ~30 times a second. */
    Status getStatus()
    {
        Status st;
        int leaving = -1, entering = -1;
        {
            const juce::ScopedLock sl (lock);
            for (int k = 0; k < kSlots; ++k)
            {
                const auto& s = slots[static_cast<size_t> (k)];
                st.slots[static_cast<size_t> (k)] = { s.filled, s.time, s.note };
            }
            const int now = inPlaceLocked();
            if (now >= 0)
                active = now;   // back on a slot's exact settings: that slot is the one playing
            if (! isSlot (active) || ! slots[static_cast<size_t> (active)].filled)
                active = -1;
            st.active = active;
            st.modified = active >= 0 && now != active;
            if (now != playing)
            {
                leaving = playing;
                entering = playing = now;
            }
        }
        if (onSwitch && leaving != entering) onSwitch (leaving, entering);
        return st;
    }

    /** For the saved state: nullptr when every slot is empty (the state stays as it was before snapshots). */
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        const juce::ScopedLock sl (lock);
        if (std::none_of (slots.begin(), slots.end(), [] (const Slot& s) { return s.filled; }))
            return nullptr;
        auto xml = std::make_unique<juce::XmlElement> (kTag);
        xml->setAttribute ("version", kFormat);
        xml->setAttribute ("active", active);
        for (int k = 0; k < kSlots; ++k)
        {
            const auto& s = slots[static_cast<size_t> (k)];
            if (! s.filled)
                continue;
            auto* e = xml->createNewChildElement ("SLOT");
            e->setAttribute ("index", k);
            e->setAttribute ("time", s.time);
            e->setAttribute ("note", s.note);
            e->setAttribute ("report", s.report);
            for (const auto& [id, v] : s.values)
            {
                auto* ve = e->createNewChildElement ("V");
                ve->setAttribute ("id", id);
                ve->setAttribute ("v", static_cast<double> (v));
            }
        }
        return xml;
    }

    /** From a saved state; nullptr (a project from before snapshots) empties every slot. */
    void fromXml (const juce::XmlElement* xml)
    {
        std::array<Slot, kSlots> loaded {};
        int act = -1;
        if (xml != nullptr && xml->hasTagName (kTag))
        {
            act = xml->getIntAttribute ("active", -1);
            for (auto* e : xml->getChildWithTagNameIterator ("SLOT"))
            {
                const int k = e->getIntAttribute ("index", -1);
                if (! isSlot (k))
                    continue;
                auto& s = loaded[static_cast<size_t> (k)];
                s.filled = true;
                s.time = e->getStringAttribute ("time");
                s.note = e->getStringAttribute ("note");
                s.report = e->getStringAttribute ("report");
                for (auto* ve : e->getChildWithTagNameIterator ("V"))
                    if (const auto id = ve->getStringAttribute ("id"); id.isNotEmpty())
                        s.values.emplace_back (id, static_cast<float> (ve->getDoubleAttribute ("v")));
            }
        }
        const juce::ScopedLock sl (lock);
        slots = std::move (loaded);
        active = isSlot (act) && slots[static_cast<size_t> (act)].filled ? act : -1;
        playing = -1;
    }

    static constexpr const char* kTag = "POLISHER_SNAPSHOTS";

private:
    struct Slot
    {
        bool filled = false;
        std::vector<std::pair<juce::String, float>> values;   // plain values by parameter ID
        juce::String report, time, note;
    };

    static bool isSlot (int k) noexcept { return k >= 0 && k < kSlots; }

    /** The slot whose exact settings are in place now (the active one first), or -1. */
    int inPlaceLocked() const
    {
        if (isSlot (active) && slots[static_cast<size_t> (active)].filled && matchesLocked (slots[static_cast<size_t> (active)]))
            return active;
        for (int k = 0; k < kSlots; ++k)
            if (slots[static_cast<size_t> (k)].filled && matchesLocked (slots[static_cast<size_t> (k)]))
                return k;
        return -1;
    }

    /** A setting's plain value in a slot: stored, or SettingsScope::valueWhenMissing for a control the
        slot doesn't have. */
    static float slotPlain (const Slot& s, const juce::String& id, const juce::RangedAudioParameter& p)
    {
        const auto stored = std::find_if (s.values.begin(), s.values.end(), [&id] (const auto& v) { return v.first == id; });
        return stored != s.values.end() ? stored->second : p.convertFrom0to1 (SettingsScope::valueWhenMissing (p));
    }
    static float slotValue (const Slot& s, const juce::String& id, const juce::RangedAudioParameter& p)
    {
        return p.convertTo0to1 (slotPlain (s, id, p));
    }

    /** True when every setting is at the slot's value now (see slotPlain). Called ~30 times a second. */
    bool matchesLocked (const Slot& s) const
    {
        const auto near = [] (const juce::RangedAudioParameter& p, float want)
        {
            const auto& range = p.getNormalisableRange();
            return std::abs (p.convertFrom0to1 (p.getValue()) - want) <= 1.0e-5f * std::max (1.0f, range.end - range.start);
        };
        int stored = 0;
        for (const auto& [id, plain] : s.values)
        {
            const auto it = byId.find (id);
            if (it == byId.end() || ! SettingsScope::isSetting (id))
                continue;   // a control this version doesn't have (or no longer stores)
            ++stored;
            if (! near (*it->second, plain))
                return false;
        }
        if (stored == numSettings)
            return true;
        // A slot stored before some controls existed: those must be at the value recall gives them.
        for (const auto& [id, p] : byId)
            if (SettingsScope::isSetting (id) && ! near (*p, slotPlain (s, id, *p)))
                return false;
        return true;
    }

    SettingsHistory& history;
    std::map<juce::String, juce::RangedAudioParameter*> byId;
    int numSettings = 0;   // how many of them a slot holds (SettingsScope::isSetting)
    mutable juce::CriticalSection lock;
    std::array<Slot, kSlots> slots {};
    int active = -1;      // the slot last stored or recalled (shown lit), -1 = none
    int playing = -1;     // the slot onSwitch last reported as in place

    JUCE_DECLARE_NON_COPYABLE (SettingsSnapshots)
};
