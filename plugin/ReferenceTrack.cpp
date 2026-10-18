#include "ReferenceTrack.h"

#if JUCE_MAC
 #include <sys/stat.h>
#elif JUCE_WINDOWS
 extern "C" __declspec (dllimport) unsigned long __stdcall GetFileAttributesW (const wchar_t*);   // <windows.h>, without its macros
#endif

namespace
{
    constexpr int kChunk = 1 << 16;   // samples read at a time (~1.4 s at 48 kHz)
    constexpr double kMinRate = 8000.0, kMaxRate = 768000.0;   // a damaged header (NaN, 4 GHz) never sizes a buffer

    /** One reading thread for every Masterology in the process, living as long as the plug-in library. A plug-in that
        closes doesn't wait for it: a read that blocks finishes on its own, and its result is thrown away. */
    juce::ThreadPool& loader()
    {
        static juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Masterology reference").withNumberOfThreads (1) };
        return pool;
    }

    juce::String quoted (const juce::File& f)
    {
        return juce::String::fromUTF8 ("\xE2\x80\x9C") + f.getFileName() + juce::String::fromUTF8 ("\xE2\x80\x9D");
    }

    template <typename Array>
    juce::var arrayToVar (const Array& values)
    {
        juce::Array<juce::var> list;
        for (double v : values) list.add (v);
        return list;
    }

    double number (const juce::var& v, const char* key, double fallback)
    {
        const auto x = v.getProperty (key, fallback);
        return x.isDouble() || x.isInt() || x.isInt64() ? static_cast<double> (x) : fallback;
    }
}

struct ReferenceTrack::Shared
{
    std::atomic<int> job { 0 };
    std::atomic<bool> closed { false };
    std::mutex ownerLock;
    ReferenceTrack* owner = nullptr;   // under ownerLock; nullptr once the ReferenceTrack is gone
};

ReferenceTrack::ReferenceTrack() : shared (std::make_shared<Shared>())
{
    shared->owner = this;
}

ReferenceTrack::~ReferenceTrack()
{
    // A load still running stops at its next chunk (or when a blocking read returns), and never reaches this object.
    shared->closed.store (true);
    const std::lock_guard<std::mutex> g (shared->ownerLock);
    shared->owner = nullptr;
}

bool ReferenceTrack::isOnlineOnly (const juce::File& file)
{
   #if JUCE_WINDOWS
    // FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS (OneDrive "online only").
    const auto attributes = GetFileAttributesW (file.getFullPathName().toWideCharPointer());
    return attributes != 0xFFFFFFFFul && (attributes & (0x1000ul | 0x40000ul | 0x400000ul)) != 0;
   #elif JUCE_MAC && defined (SF_DATALESS)
    struct stat st {};
    return stat (file.getFullPathName().toRawUTF8(), &st) == 0 && (st.st_flags & SF_DATALESS) != 0;   // iCloud, not downloaded
   #else
    juce::ignoreUnused (file);
    return false;
   #endif
}

void ReferenceTrack::load (const juce::File& file)
{
    const int myJob = ++shared->job;
    {
        const juce::ScopedLock sl (lock);
        lastFolder = file.getParentDirectory();
        message = {};
    }
    state.store (loading);
    ++version;
    if (isOnlineOnly (file))
    {
        finish (myJob, {}, quoted (file) + " is only in the cloud (OneDrive or iCloud), not on this computer yet. Make it available "
                           "offline (OneDrive: right-click it > Always keep on this device; iCloud: Download Now), then load it again.");
        return;
    }
    loader().addJob ([s = shared, file, myJob]
    {
        const auto keepGoing = [&s, myJob] { return ! s->closed.load() && s->job.load() == myJob; };
        if (! keepGoing())
            return;   // closed or replaced before it started
        juce::String error;
        polisher::ReferenceProfile p;
        try
        {
            p = analyseFile (file, error, keepGoing);
        }
        catch (...)
        {
            p = {};
            error = "Something went wrong while reading " + quoted (file) + ". Please try another file.";
        }
        const std::lock_guard<std::mutex> g (s->ownerLock);
        if (s->owner != nullptr)
            s->owner->finish (myJob, p, error);
    });
}

void ReferenceTrack::finish (int myJob, const polisher::ReferenceProfile& p, const juce::String& error)
{
    if (shared->closed.load() || shared->job.load() != myJob)
        return;   // a newer load, clear or restore came in meanwhile
    {
        const juce::ScopedLock sl (lock);
        if (shared->job.load() != myJob)
            return;
        if (p.valid)
        {
            profile = p;
            message = {};
        }
        else
        {
            message = error;   // a failed load keeps the reference you had
        }
        state.store (profile.valid ? loaded : failed);
    }
    ++version;
}

void ReferenceTrack::clear()
{
    setProfile ({});
}

void ReferenceTrack::setProfile (const polisher::ReferenceProfile& p)
{
    ++shared->job;   // any load still running is now stale
    {
        const juce::ScopedLock sl (lock);
        profile = p.valid ? p : polisher::ReferenceProfile {};
        message = {};
        state.store (profile.valid ? loaded : empty);
    }
    ++version;
}

polisher::ReferenceProfile ReferenceTrack::getProfile() const
{
    const juce::ScopedLock sl (lock);
    return profile;
}

juce::String ReferenceTrack::getMessage() const
{
    const juce::ScopedLock sl (lock);
    return message;
}

juce::File ReferenceTrack::getLastFolder() const
{
    const juce::ScopedLock sl (lock);
    return lastFolder;
}

juce::String ReferenceTrack::getInfoJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("state", static_cast<int> (getState()));
    const juce::ScopedLock sl (lock);
    o->setProperty ("message", message);
    if (profile.valid)
        o->setProperty ("profile", profileToVar (profile));
    return juce::JSON::toString (juce::var (o), true);
}

juce::String ReferenceTrack::getJsonForSaving() const
{
    const juce::ScopedLock sl (lock);
    return profile.valid ? juce::JSON::toString (profileToVar (profile), true) : juce::String();
}

void ReferenceTrack::restoreFromJson (const juce::String& json)
{
    setProfile (json.isEmpty() ? polisher::ReferenceProfile {} : profileFromVar (juce::JSON::parse (json)));
}

juce::var ReferenceTrack::profileToVar (const polisher::ReferenceProfile& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", 1);
    o->setProperty ("name", juce::String::fromUTF8 (p.name.c_str()));
    o->setProperty ("trackSeconds", p.trackSeconds);
    o->setProperty ("startSeconds", p.startSeconds);
    o->setProperty ("partSeconds", p.partSeconds);
    o->setProperty ("trackLufs", p.trackLufs);
    o->setProperty ("lufs", p.lufs);
    o->setProperty ("hookLufs", p.hookLufs);         // its loudest 12 s: what Auto-Master aims for (absent in older saves)
    o->setProperty ("sampleRate", p.sampleRate);     // the file's: the spectrum curve is shifted for the project's rate
    o->setProperty ("truePeak", p.truePeakDbtp);
    o->setProperty ("plr", p.peakToLoudnessDb);
    o->setProperty ("bandDb", arrayToVar (p.bandDb));
    o->setProperty ("bassWidth", p.bassWidthPercent);
    o->setProperty ("midSide", p.midSideDb);
    o->setProperty ("highSide", p.highSideDb);
    o->setProperty ("correlation", p.correlation);
    o->setProperty ("subVsBass", p.subVsBassDb);
    o->setProperty ("displayDb", arrayToVar (p.displayDb));
    return juce::var (o);
}

polisher::ReferenceProfile ReferenceTrack::profileFromVar (const juce::var& v)
{
    polisher::ReferenceProfile p;
    const auto* bands = v.getProperty ("bandDb", {}).getArray();
    const double lufs = number (v, "lufs", -1000.0);
    if (! v.isObject() || bands == nullptr || bands->size() != polisher::kEqBands || lufs < -100.0 || lufs > 10.0)
        return p;   // not a reference this build can read: as if there were none
    p.name = v.getProperty ("name", "").toString().toStdString();
    p.trackSeconds = number (v, "trackSeconds", 0.0);
    p.startSeconds = number (v, "startSeconds", 0.0);
    p.partSeconds = number (v, "partSeconds", 0.0);
    p.trackLufs = number (v, "trackLufs", lufs);
    p.lufs = lufs;
    p.hookLufs = number (v, "hookLufs", -100.0);     // older saves: -100, so referenceLoudness() uses lufs
    p.sampleRate = number (v, "sampleRate", 0.0);    // older saves: unknown, the curve isn't shifted
    p.truePeakDbtp = number (v, "truePeak", -100.0);
    p.peakToLoudnessDb = number (v, "plr", 0.0);
    for (int b = 0; b < polisher::kEqBands; ++b)
        p.bandDb[static_cast<size_t> (b)] = static_cast<double> ((*bands)[b]);
    p.bassWidthPercent = number (v, "bassWidth", 0.0);
    p.midSideDb = number (v, "midSide", -100.0);
    p.highSideDb = number (v, "highSide", -100.0);
    p.correlation = number (v, "correlation", 1.0);
    p.subVsBassDb = number (v, "subVsBass", 0.0);
    if (const auto* curve = v.getProperty ("displayDb", {}).getArray())
        for (const auto& x : *curve)
            p.displayDb.push_back (static_cast<double> (x));
    p.valid = true;
    return p;
}

polisher::ReferenceProfile ReferenceTrack::analyseFile (const juce::File& file, juce::String& error,
                                                        const std::function<bool()>& keepGoing)
{
    error = {};
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg, MP3 (+ the system's own formats on Windows / macOS)
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Masterology can't read " + quoted (file) + ". Use a WAV, AIFF, FLAC or MP3 file.";
        return {};
    }
    const double sr = reader->sampleRate;
    if (! std::isfinite (sr) || sr < kMinRate || sr > kMaxRate || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        error = quoted (file) + " has no audio Masterology can use.";
        return {};
    }

    // Pass 1: the whole song (up to the limit), streamed through in chunks.
    const auto total = std::min (reader->lengthInSamples,
                                 static_cast<juce::int64> (sr * 60.0 * polisher::kMaxReferenceMinutes));
    juce::AudioBuffer<float> buffer (2, kChunk);
    auto readChunk = [&] (juce::int64 pos, int n)
    {
        buffer.clear();
        reader->read (&buffer, 0, n, pos, true, true);
        if (reader->numChannels == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);   // a mono file plays on both sides
    };
    polisher::LoudestPartFinder finder;
    finder.prepare (sr, 2);
    for (juce::int64 pos = 0; pos < total; pos += kChunk)
    {
        if (! keepGoing())
            return {};
        const int n = static_cast<int> (std::min<juce::int64> (kChunk, total - pos));
        readChunk (pos, n);
        finder.process (buffer.getArrayOfReadPointers(), 2, n);
    }

    // Pass 2: read back only the loudest part and measure it.
    const juce::int64 start = finder.loudestStart (polisher::kReferencePartSeconds);
    const auto len = static_cast<int> (std::min (total - start, static_cast<juce::int64> (sr * polisher::kReferencePartSeconds)));
    polisher::AudioClip part;
    part.sampleRate = sr;
    part.channels.assign (2, std::vector<float> (static_cast<size_t> (len)));
    for (int done = 0; done < len; done += kChunk)
    {
        if (! keepGoing())
            return {};
        const int n = std::min (kChunk, len - done);
        readChunk (start + done, n);
        for (int c = 0; c < 2; ++c)
            std::copy (buffer.getReadPointer (c), buffer.getReadPointer (c) + n, part.channels[static_cast<size_t> (c)].begin() + done);
    }
    // Its loudest kHookSeconds over the whole song: Auto-Master compares it with the loudest 12 s of your song.
    auto p = polisher::analyseReference (part, finder.seconds(), finder.integratedLufs(), static_cast<double> (start) / sr,
                                         finder.loudestLufs (polisher::kHookSeconds));
    if (! p.valid)
    {
        error = "There isn't enough music in " + quoted (file) + " to measure (at least a few seconds of sound).";
        return {};
    }
    p.name = file.getFileNameWithoutExtension().toStdString();
    return p;
}
