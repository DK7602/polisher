#pragma once

// Sonicology Rack, stage 2: how a plug-in applies a request from a Rack view to its own host parameters.
//
// THIS FILE IS THE SAME IN ALL THREE REPOSITORIES (Voxology, Mixology, Masterology). Change it in Mixology (the master
// copy), copy it to the other two.
//
// The owner of a unit serves its queue from its timer (rack::Publisher::serve, suite/RackLink.h) and, for each request,
// turns the Rack's 0..1 back into a value with its own curve (each plug-in's rackunit fromKnob) and calls set() here:
// the parameter moves like a move of your own mouse, so the host records it (automation, the project) and a knob drag
// is one touch (its gesture stays open from begin to end).

#include "suite/RackLink.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstring>
#include <optional>
#include <string>

namespace rack::host
{
    /** Moves a host parameter to `value` (in its own units) with the request's gesture. */
    inline void set (juce::RangedAudioParameter& p, Gesture gesture, float value)
    {
        const auto& range = p.getNormalisableRange();
        const float n = p.convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, value)));
        if (gesture == Gesture::begin || gesture == Gesture::set) p.beginChangeGesture();
        if (std::abs (n - p.getValue()) > 1.0e-7f) p.setValueNotifyingHost (n);
        if (gesture == Gesture::end || gesture == Gesture::set) p.endChangeGesture();
    }

    /** Back to the parameter's default (a double-click on the Rack's knob), as one gesture. */
    inline void setDefault (juce::RangedAudioParameter& p)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    }

    /** Turns an on switch (a bool, or any parameter whose top is "on"). */
    inline void setSwitch (juce::RangedAudioParameter& p, bool on)
    {
        const auto& range = p.getNormalisableRange();
        set (p, Gesture::set, on ? range.end : range.start);
    }

    /** The parameter with this id, or nullptr. */
    inline juce::RangedAudioParameter* find (juce::AudioProcessorValueTreeState& apvts, const std::string& id)
    {
        return id.empty() ? nullptr : apvts.getParameter (juce::String::fromUTF8 (id.data(), static_cast<int> (id.size())));
    }

    // ---- the view's plug-in: what rack.js reads, and its rackRequest / rackAnswer native functions -----------------

    /** What an open Rack view shows (rack::viewJson), served as the file "rack-data/<track>.json" by the plug-in's
        resource provider (std::nullopt for any other file). <track> is rack.js's track key ("t<hash>", "fullmix",
        "master"; "_" = every track's details), in the path because JUCE on macOS hands the provider the path without the
        query. Not through a native function: JUCE hands a native function's result to the page escaped with
        String::replace, whose time grows with the square of the text, and the Rack's text (tens of KB, every quote
        escaped) took seconds of the DAW's window thread each time (Beta 16: Cubase froze and went while a Rack was
        open). A file goes to the page as it is. */
    inline std::optional<juce::WebBrowserComponent::Resource> viewResource (const juce::String& url, const std::string& host,
                                                                           std::uint32_t hostHash)
    {
        const auto file = url.trimCharactersAtStart ("/").upToFirstOccurrenceOf ("?", false, false);
        if (! file.startsWith ("rack-data/") || ! file.endsWith (".json")) return std::nullopt;
        auto track = juce::URL::removeEscapeChars (file.fromFirstOccurrenceOf ("/", false, false).dropLastCharacters (5));
        if (track == "_" || ! track.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789")) track.clear();
        const auto json = viewJson (host, hostHash, suite::nowMs(), track.toStdString());
        juce::WebBrowserComponent::Resource r;
        r.data.resize (json.size());
        std::memcpy (r.data.data(), json.data(), json.size());
        r.mimeType = "application/json";
        return r;
    }

    inline std::string text (const juce::var& v) { return v.toString().toStdString(); }

    /** rackRequest (unit, gen, change, module, control, value, gesture): JSON text {"ticket","why"}. */
    inline juce::String requestFrom (const juce::Array<juce::var>& args)
    {
        const auto at = [&args] (int i) { return i < args.size() ? args[i] : juce::var(); };
        const auto json = requestJson (Link::process(), text (at (0)), static_cast<std::uint32_t> (static_cast<juce::int64> (at (1))),
                                       text (at (2)), text (at (3)), text (at (4)), static_cast<float> (static_cast<double> (at (5))),
                                       text (at (6)), suite::nowMs());
        return juce::String::fromUTF8 (json.data(), static_cast<int> (json.size()));
    }

    /** rackAnswer (unit, gen, ticket): JSON text {"outcome","why"}. */
    inline juce::String answerFrom (const juce::Array<juce::var>& args)
    {
        const auto at = [&args] (int i) { return i < args.size() ? args[i] : juce::var(); };
        const auto json = answerJson (Link::process(), text (at (0)), static_cast<std::uint32_t> (static_cast<juce::int64> (at (1))),
                                      static_cast<std::uint64_t> (juce::jmax<juce::int64> (0, static_cast<juce::int64> (at (2)))));
        return juce::String::fromUTF8 (json.data(), static_cast<int> (json.size()));
    }

    /** getRack's reply (the fallback when rack-data/<track>.json can't be read): the same JSON as base64, so JUCE
        has no quote or backslash to escape in it (rack.js's SonicologyRack.fromBase64Json reads it). */
    inline juce::String viewBase64 (const std::string& host, std::uint32_t hostHash, const juce::var& track)
    {
        const auto json = viewJson (host, hostHash, suite::nowMs(), track.isVoid() ? std::string() : track.toString().toStdString());
        return juce::Base64::toBase64 (json.data(), json.size());
    }

    /** Any native function's reply that is JSON text (a report, a list): sent as the object it describes. JUCE escapes
        a text reply at a cost that grows with the square of its quotes (see viewResource); an object costs it almost
        nothing. The page's own text comes back with jsonText() in its app.js. */
    inline juce::var jsonObject (const juce::String& json)
    {
        return json.isEmpty() ? juce::var() : juce::JSON::parse (json);
    }

    /** The Rack shows a whole project, more than a plug-in's own page: while it is open the plug-in's window grows
        (to about 85 % of the screen's height, the same shape; never smaller than it was), and BACK puts it back as it
        was. The page calls the editor's rackWindow (true / false) native function, which calls open / close here.
        Message thread. A plug-in that remembers its size (Masterology) doesn't while `big`. */
    struct RackWindow
    {
        bool big = false;
        int width = 0, height = 0;   // the size before the Rack

        void open (juce::Component& editor)
        {
            if (big || editor.getWidth() <= 0 || editor.getHeight() <= 0) return;
            const auto screen = editor.getScreenBounds();
            const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (screen);
            if (d == nullptr) return;
            const auto area = d->userBounds.toNearestInt();   // the screen less the taskbar / menu bar
            // a host's scaling of the editor (its size on screen / its own size)
            const double k = screen.getHeight() > 0 ? screen.getHeight() / static_cast<double> (editor.getHeight()) : 1.0;
            const double aspect = editor.getWidth() / static_cast<double> (editor.getHeight());
            double h = area.getHeight() * 0.85;   // room for the DAW's bar above the plug-in
            double w = h * aspect;
            if (w > area.getWidth() - 40.0) { w = area.getWidth() - 40.0; h = w / aspect; }
            w /= k; h /= k;
            if (auto* editorWithLimits = dynamic_cast<juce::AudioProcessorEditor*> (&editor))
                if (auto* c = editorWithLimits->getConstrainer())
                {
                    if (w > c->getMaximumWidth()) { w = c->getMaximumWidth(); h = w / aspect; }
                    if (h > c->getMaximumHeight()) { h = c->getMaximumHeight(); w = h * aspect; }
                }
            if (w < editor.getWidth() + 8) return;   // already about as big
            width = editor.getWidth();
            height = editor.getHeight();
            big = true;
            editor.setSize (juce::roundToInt (w), juce::roundToInt (h));
        }

        void close (juce::Component& editor)
        {
            if (! big) return;
            editor.setSize (width, height);   // (still `big` here: a plug-in that remembers its size has it already)
            big = false;
        }
    };

    /** The usual answer when a knob can't be turned from the Rack. */
    inline std::string cannot (const char* product, const Request& r)
    {
        return std::string (product) + " can't change " + (r.control.empty() ? r.module : r.control) + " from the Rack.";
    }
}
