// Polisher Blind Test: turns 2-8 exported songs (your mix, Polisher's master, another master...) into
// a fair blind listening test. Every file is played at the same loudness (EBU R128, turned down
// only, never clipping), the order is shuffled into "Sample A", "Sample B"..., and the answer key
// waits in its own file until you've written down what you heard.
//
//   PolisherBlindTest file1.wav file2.wav [...] [--start 60] [--length 30] [--no-wait]
//
// On Windows, drag the files onto PolisherBlindTest.exe. The test lands in a new folder next to the
// first file.

#include <juce_audio_formats/juce_audio_formats.h>

#include "BlindTestCore.h"
#include "polisher/LoudnessTargeting.h"

#include <iostream>

namespace {

void waitForEnter (bool wait)
{
    if (! wait) return;
    std::cout << "\nPress Enter to close." << std::endl;
    std::string line;
    std::getline (std::cin, line);
}

juce::String db (double v, int digits = 1)
{
    return std::isfinite (v) ? juce::String (v, digits) : juce::String ("-inf");
}

} // namespace

int run (const juce::StringArray& args)
{
    juce::StringArray paths;
    double start = 0.0, length = 0.0;
    bool wait = true;
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a == "--start" && i + 1 < args.size())        start = args[++i].getDoubleValue();
        else if (a == "--length" && i + 1 < args.size())  length = args[++i].getDoubleValue();
        else if (a == "--no-wait")                        wait = false;
        else                                              paths.add (a);
    }

    std::cout << "Polisher Blind Test\n";
    if (paths.size() < 2 || paths.size() > 8)
    {
        std::cout << "\nDrag 2 to 8 exported songs (WAV, AIFF or FLAC) onto this program, for example your mix,\n"
                     "Polisher's master and another master of the same song. It plays them back at the same\n"
                     "loudness in a shuffled order, so you judge the sound and not the volume or the name.\n\n"
                     "Command line: PolisherBlindTest a.wav b.wav [...] [--start seconds] [--length seconds]\n";
        waitForEnter (wait);
        return 1;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    struct Song { juce::File file; polisher::AudioClip clip; blindtest::Measured measured; };
    std::vector<Song> songs;
    for (const auto& p : paths)
    {
        const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (p.unquoted());
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            std::cout << "Can't read " << f.getFullPathName() << " (use WAV, AIFF or FLAC).\n";
            waitForEnter (wait);
            return 1;
        }
        const auto sr = reader->sampleRate;
        const auto first = (juce::int64) (start * sr);
        auto count = reader->lengthInSamples - first;
        if (length > 0.0) count = std::min (count, (juce::int64) (length * sr));
        if (count <= (juce::int64) (sr))
        {
            std::cout << f.getFileName() << " is shorter than the part to compare.\n";
            waitForEnter (wait);
            return 1;
        }
        // Always two channels: a mono file is played on both speakers, so it is measured (and written)
        // that way too; measured as one channel it would read 3 dB quieter and then play 3 dB louder.
        juce::AudioBuffer<float> buf (2, (int) count);
        if (! reader->read (&buf, 0, (int) count, first, true, true))
        {
            std::cout << "Can't read the audio in " << f.getFileName() << ".\n";
            waitForEnter (wait);
            return 1;
        }

        Song s { f, {}, {} };
        s.clip.sampleRate = sr;
        for (int c = 0; c < 2; ++c)
            s.clip.channels.emplace_back (buf.getReadPointer (c), buf.getReadPointer (c) + count);
        // A cut-out part gets 20 ms fades, so no sample starts or ends with a click.
        if (start > 0.0 || length > 0.0)
        {
            const int n = std::min ((int) (0.02 * sr), (int) count / 2);
            for (auto& ch : s.clip.channels)
                for (int i = 0; i < n; ++i)
                {
                    const float w = (float) i / (float) n;
                    ch[(size_t) i] *= w;
                    ch[ch.size() - 1 - (size_t) i] *= w;
                }
        }
        const auto stats = polisher::measure (s.clip);
        s.measured = { stats.integratedLufs, stats.truePeakDbtp };
        std::cout << "  " << f.getFileName() << ": " << db (stats.integratedLufs) << " LUFS, " << db (stats.truePeakDbtp) << " dBTP\n";
        songs.push_back (std::move (s));
    }

    std::vector<blindtest::Measured> measured;
    for (const auto& s : songs) measured.push_back (s.measured);
    const auto gains = blindtest::matchGains (measured);
    const auto order = blindtest::shuffledOrder ((int) songs.size(), (unsigned) juce::Time::currentTimeMillis());

    // A new folder every time (two runs in the same second must not mix their samples).
    const auto folder = songs.front().file.getParentDirectory()
                            .getChildFile ("Polisher blind test " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"))
                            .getNonexistentSibling (false);
    if (! folder.createDirectory())
    {
        std::cout << "Can't create " << folder.getFullPathName() << "\n";
        waitForEnter (wait);
        return 1;
    }

    juce::String key = "POLISHER BLIND TEST - ANSWER KEY\n"
                       "Open this only after you have filled in Score sheet.txt.\n\n";
    double matchedLufs = -INFINITY;
    for (size_t i = 0; i < measured.size(); ++i)
        if (std::isfinite (measured[i].integratedLufs))
            matchedLufs = measured[i].integratedLufs + gains[i];
    juce::String sheet = "POLISHER BLIND TEST - SCORE SHEET\n\n"
                         "Every sample plays at the same loudness (" + db (matchedLufs) + " LUFS), so louder can't win. How to listen:\n"
                         "1. Headphones or speakers at one comfortable volume; don't touch it during the test.\n"
                         "2. Play each sample, then jump between them at the same moment (the hook, a drop, a quiet verse).\n"
                         "3. Write down what you hear: punch of the kick and snare, 808 / bass, vocal clarity,\n"
                         "   harshness, width, anything that pumps or distorts. Score each 1-10.\n"
                         "4. Only then open Answer key.txt.\n\n";
    for (size_t k = 0; k < order.size(); ++k)
    {
        const auto& s = songs[(size_t) order[k]];
        const double g = gains[(size_t) order[k]];
        const juce::String name = juce::String ("Sample ") + (char) ('A' + k);

        juce::AudioBuffer<float> out ((int) s.clip.channels.size(), s.clip.numSamples());
        for (int c = 0; c < out.getNumChannels(); ++c)
            out.copyFrom (c, 0, s.clip.channels[(size_t) c].data(), out.getNumSamples(), (float) juce::Decibels::decibelsToGain (g));

        auto file = std::make_unique<juce::FileOutputStream> (folder.getChildFile (name + ".wav"));
        if (! file->openedOk() || ! file->setPosition (0) || file->truncate().failed())   // overwrite, never append
        {
            std::cout << "Can't write " << name << ".wav\n";
            waitForEnter (wait);
            return 1;
        }
        std::unique_ptr<juce::OutputStream> stream = std::move (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (s.clip.sampleRate)
                                                       .withNumChannels (out.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
        {
            std::cout << "Can't write " << name << ".wav\n";
            waitForEnter (wait);
            return 1;
        }

        // Two files can share a name (two "Mixdown.wav" from different folders): then show the full path.
        int sameName = 0;
        for (const auto& other : songs)
            sameName += other.file.getFileName() == s.file.getFileName() ? 1 : 0;
        key << name << " = " << (sameName > 1 ? s.file.getFullPathName() : s.file.getFileName()) << "\n"
            << "           measured " << db (s.measured.integratedLufs) << " LUFS, " << db (s.measured.truePeakDbtp)
            << " dBTP; turned down " << db (std::max (0.0, -g)) << " dB for the test\n";
        sheet << name << "   score (1-10): ____   notes: ________________________________________\n\n";
    }
    key << "\nThe louder files were turned down to the quietest one (never up), then all together if a peak\n"
           "would pass -1 dBTP, so every sample plays at the same loudness without clipping.\n";
    folder.getChildFile ("Answer key.txt").replaceWithText (key);
    folder.getChildFile ("Score sheet.txt").replaceWithText (sheet);

    std::cout << "\nDone: " << folder.getFullPathName() << "\n"
              << "Listen to the samples, fill in Score sheet.txt, then open Answer key.txt.\n";
    waitForEnter (wait);
    return 0;
}

// On Windows the command line arrives in UTF-16 through wmain, so song titles with accents, curly quotes
// or any other script open fine (main's argv would be in the ANSI code page).
#if JUCE_WINDOWS
int wmain (int argc, wchar_t* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));
    return run (args);
}
#else
int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));
    return run (args);
}
#endif
