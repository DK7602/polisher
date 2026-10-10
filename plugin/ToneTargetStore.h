#pragma once

#include <juce_core/juce_core.h>

#include "polisher/StemEQ.h"

#include <atomic>

/** Tone Steady's stored target: the song's own tone, learned by Auto-Master while the song played
    (polisher::SongListener::toneTarget, as the mix arrives at the plug-in). Saved with the plug-in
    state as JSON (XML attribute "toneTarget", with a format number); a state without it (every song
    saved before stored targets existed) has none, and Tone Steady follows the music as before.

    set / clear / restore come from the message thread; the audio thread picks the target up with
    fetch(), which never waits (when the store is being written it tries again on the next block). */
class ToneTargetStore final
{
public:
    static constexpr int kFormat = 1;

    ToneTargetStore() = default;

    void set (const polisher::ToneTarget& target);   // an invalid target clears
    void clear() { set ({}); }
    polisher::ToneTarget get() const;
    bool isSet() const;
    /** Goes up by one on every change: the editor and the audio thread watch it. */
    int getVersion() const noexcept { return version.load (std::memory_order_acquire); }

    /** Audio thread: copies the target into `out` if it changed since `seenVersion` (then updates it).
        Returns true when `out` was updated. Never blocks or allocates. */
    bool fetch (polisher::ToneTarget& out, int& seenVersion) const noexcept;

    /** For the plug-in state: JSON, or "" when there is none. */
    juce::String toJson() const;
    /** After the host restores a state: its target, or none ("", unreadable, or another format). */
    void restoreFromJson (const juce::String& json);

private:
    mutable juce::SpinLock lock;
    polisher::ToneTarget target;                  // guarded by lock
    std::atomic<int> version { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneTargetStore)
};
