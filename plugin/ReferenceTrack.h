#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "polisher/AutoMaster.h"

#include <atomic>

/** The reference song for "Match ref".

    load() reads the file on a background thread in two passes, so a long song never sits in memory:
    first the whole song (up to polisher::kMaxReferenceMinutes) streams through a LoudestPartFinder,
    then only its loudest kReferencePartSeconds are read back and measured. What's kept is the
    measurement (polisher::ReferenceProfile) and the file's name, never the audio; it is saved with
    the plug-in state as JSON, so the reference is still there when the project is reopened, even if
    the file has moved.

    Every load / clear / restore has a job number; a slower load that finishes after a newer request
    is thrown away. All public functions are thread-safe; none may be called from the audio thread. */
class ReferenceTrack final
{
public:
    enum State { empty = 0, loading = 1, loaded = 2, failed = 3 };

    ReferenceTrack();
    ~ReferenceTrack();

    /** Starts reading and measuring a file in the background (returns at once). */
    void load (const juce::File& file);
    /** Forgets the reference: Auto-Master goes back to the genre targets. */
    void clear();

    State getState() const noexcept           { return static_cast<State> (state.load()); }
    /** Goes up by one on every change (load finished or failed, clear, restore): the editor polls it. */
    int getVersion() const noexcept           { return version.load(); }
    polisher::ReferenceProfile getProfile() const;
    /** Why the last load failed, in plain words ("" otherwise). */
    juce::String getMessage() const;
    /** Where the last file was picked, so the next file chooser opens there. */
    juce::File getLastFolder() const;

    /** For the editor: { state, message, profile fields... } as JSON. */
    juce::String getInfoJson() const;
    /** For the plug-in state: the profile as JSON, or "" when there is no reference. */
    juce::String getJsonForSaving() const;
    /** After the host restores a state: the saved reference, or none ("" or unreadable). */
    void restoreFromJson (const juce::String& json);
    /** Sets a measured profile directly (tests, restore). An invalid profile clears. */
    void setProfile (const polisher::ReferenceProfile& profile);

    static juce::var profileToVar (const polisher::ReferenceProfile& p);
    static polisher::ReferenceProfile profileFromVar (const juce::var& v);

    /** The whole job, on the calling thread: decode, find the loudest part, measure it. Stops early
        (returning an invalid profile) when keepGoing() turns false. error explains a failure. */
    static polisher::ReferenceProfile analyseFile (const juce::File& file, juce::String& error,
                                                   const std::function<bool()>& keepGoing = [] { return true; });

private:
    void finish (int job, const polisher::ReferenceProfile& profile, const juce::String& error);

    mutable juce::CriticalSection lock;
    polisher::ReferenceProfile profile;           // guarded by lock
    juce::String message;                         // guarded by lock
    juce::File lastFolder;                        // guarded by lock
    std::atomic<int> state { empty }, version { 0 }, job { 0 };
    std::atomic<bool> shuttingDown { false };

    juce::ThreadPool pool { 1 };                  // last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceTrack)
};
