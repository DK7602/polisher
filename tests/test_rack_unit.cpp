// Sonicology Rack: what Masterology tells the Rack (polisher/RackUnit.h).

#include "polisher/RackUnit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace polisher;

namespace
{
    const rack::Module* find (const std::vector<rack::Module>& ms, const char* key)
    {
        for (const auto& m : ms)
            if (m.key == key) return &m;
        return nullptr;
    }
    const rack::Value* value (const rack::Module& m, const char* name)
    {
        for (const auto& v : m.values)
            if (v.name == name) return &v;
        return nullptr;
    }
}

TEST_CASE ("Rack unit (Masterology): the 12 modules in signal order; a new Masterology is idle, its clipper off", "[rack]")
{
    rackunit::State st;
    st.goal = "Loud release";
    const auto ms = rackunit::modules (st, true);
    const char* order[] = { "mas.unlimiter", "mas.tonaleq", "mas.stemeq", "mas.steady", "mas.resonance", "mas.bass",
                            "mas.glue", "mas.dynamics", "mas.imager", "mas.sat", "mas.clipper", "mas.max" };
    REQUIRE (ms.size() == 12);
    for (std::size_t i = 0; i < ms.size(); ++i)
    {
        CHECK (ms[i].key == order[i]);
        CHECK (ms[i].values.size() <= static_cast<std::size_t> (rack::kMaxValues));
    }
    for (std::size_t i = 0; i < 10; ++i) CHECK (ms[i].state == rack::State::idle);
    CHECK (ms[1].summary == "Flat");
    CHECK (find (ms, "mas.clipper")->state == rack::State::off);
    CHECK (find (ms, "mas.max")->state == rack::State::on);
    CHECK (find (ms, "mas.max")->summary == "Drive 0.0 dB \xC2\xB7 Ceiling \xE2\x88\x92" "1.0 dBTP \xC2\xB7 Loud release \xE2\x88\x92" "9 LUFS");
}

TEST_CASE ("Rack unit (Masterology): Tonal EQ bands draw its curve; Stem EQ and the Imager say what moved", "[rack]")
{
    rackunit::State st;
    st.eq.gainDb = { 1.0, 0.0, 0.0, 0.0, -0.5, 0.0, 1.5 };
    st.eq.channels[6] = EqChannels::side;
    st.stem.gainDb = { 1.0, 0.0, 0.0, 0.0 };
    st.imager.highWidth = 115.0;
    const auto ms = rackunit::modules (st, true);

    const auto* eq = find (ms, "mas.tonaleq");
    REQUIRE (eq != nullptr);
    CHECK (eq->state == rack::State::on);
    CHECK (eq->summary == "Low End +1.0 dB \xC2\xB7 Upper Mid \xE2\x88\x92" "0.5 dB \xC2\xB7 Air +1.5 dB (Side)");
    REQUIRE (eq->bands.size() == 3);
    CHECK (eq->bands[0].type == rack::BandType::lowShelf);
    CHECK (eq->bands[0].hz == Catch::Approx (45.0f));
    CHECK (eq->bands[2].type == rack::BandType::highShelf);
    CHECK (eq->values.size() == 7);
    CHECK (value (*eq, "Air")->norm == Catch::Approx ((1.5f + 9.0f) / 18.0f));
    CHECK (value (*eq, "Air")->bi);

    CHECK (find (ms, "mas.stemeq")->summary == "Vocals +1.0 dB");
    CHECK (find (ms, "mas.imager")->summary == "Highs 115 %");
    CHECK (find (ms, "mas.imager")->state == rack::State::on);
}

TEST_CASE ("Rack unit (Masterology): compressors and limiters show their gain reduction", "[rack]")
{
    rackunit::State st;
    st.glue.ratio = 2.0;
    st.glue.thresholdDb = -14.0;
    st.dynamics.bands[0].ratio = 2.0;
    st.dynamics.bands[3].impact = 20.0;
    st.clipOn = true;
    st.clipLevelDb = -1.0;
    st.driveDb = 4.5;
    st.hasMeters = true;
    st.meters.glue = -1.5f;
    st.meters.dynamics = { -2.0f, -0.5f, 0.0f, -1.0f };
    st.meters.clipper = -0.6f;
    st.meters.maximizer = -3.2f;
    const auto ms = rackunit::modules (st, true);

    const auto* glue = find (ms, "mas.glue");
    CHECK (glue->summary == "Bus \xC2\xB7 2.0:1 \xC2\xB7 Threshold \xE2\x88\x92" "14.0 dB");
    CHECK (glue->meterKind == rack::MeterKind::gr);
    CHECK (glue->meter == Catch::Approx (-1.5f));
    CHECK (value (*glue, "Release")->text == "Auto");

    const auto* dyn = find (ms, "mas.dynamics");
    CHECK (dyn->summary == "Low 2.0:1 \xC2\xB7 High Impact +20 %");
    CHECK (dyn->meter == Catch::Approx (-2.0f));   // the bands it works in (the Lo-Mid's -0.5 is left alone: off)
    CHECK (value (*dyn, "Low")->text == "\xE2\x88\x92" "2.0 dB");
    CHECK (value (*dyn, "Lo-Mid")->text == "Off");
    CHECK (value (*dyn, "High")->text == "Shaped");

    CHECK (find (ms, "mas.clipper")->state == rack::State::on);
    CHECK (find (ms, "mas.clipper")->summary == "Clip level \xE2\x88\x92" "1.0 dB");
    CHECK (find (ms, "mas.clipper")->meter == Catch::Approx (-0.6f));
    CHECK (find (ms, "mas.max")->meter == Catch::Approx (-3.2f));
    CHECK (value (*find (ms, "mas.max"), "Drive")->text == "+4.5 dB");

    st.glue.enabled = false;
    CHECK (find (rackunit::modules (st, true), "mas.glue")->meter == 0.0f);   // off: no meter
}

TEST_CASE ("Rack unit (Masterology): brief units carry keys, states and summaries only", "[rack]")
{
    rackunit::State st;
    st.eq.gainDb[0] = 1.0;
    st.bass.monoHz = 120.0;
    st.bass.punch = 0.2;
    const auto ms = rackunit::modules (st, false);
    for (const auto& m : ms)
    {
        CHECK (m.values.empty());
        CHECK (m.bands.empty());
        CHECK_FALSE (m.summary.empty());
    }
    CHECK (find (ms, "mas.bass")->summary == "Mono below 120 Hz \xC2\xB7 Punch 20 %");
}

TEST_CASE ("Rack unit (Masterology): with Mixology's Limiter on the Full Mix, the Rack warns", "[rack]")
{
    rackunit::State st;
    rack::Unit mas;
    mas.product = rack::Product::masterology;
    mas.role = rack::Role::master;
    mas.track = "Stereo Out";
    mas.version = suite::packVersion (0, 31, 0);
    mas.modules = rackunit::modules (st, false);

    rack::Unit mix;
    mix.product = rack::Product::mixology;
    mix.role = rack::Role::fullmix;
    mix.track = "Stereo Out";
    mix.version = suite::packVersion (0, 26, 0);
    rack::Module limit;
    limit.key = "mix.limit";
    limit.state = rack::State::on;
    mix.modules = { limit };

    auto mem = std::make_unique<unsigned char[]> (rack::Link::bytes() + 64);
    void* aligned = mem.get() + (64 - reinterpret_cast<std::uintptr_t> (mem.get()) % 64) % 64;
    rack::Link shared (aligned);
    const int a = shared.claim (1, 10), b = shared.claim (2, 10);
    REQUIRE (shared.publish (a, 1, mas, 10));
    REQUIRE (shared.publish (b, 2, mix, 10));
    bool warned = false;
    for (const auto& l : rack::together (shared.units (10)))
        warned |= l.level == rack::Level::warn && l.text.find ("Limiter") != std::string::npos;
    CHECK (warned);
}

TEST_CASE ("Rack unit (Masterology): every knob the Rack can turn names its parameter, and turns back to its value", "[rack]")
{
    rackunit::State st;
    st.unlimiter.lengthMs = 22.0;
    st.glue.ratio = 4.0;
    st.glue.attackMs = 10.0;
    st.glue.sidechainHz = 120.0;
    st.glue.mix = 0.6;
    st.glue.character = static_cast<decltype (st.glue.character)> (2);
    st.bass.tame = 0.35;
    st.eq.gainDb[3] = -2.5;
    st.limiter.releaseMs = 300.0;
    st.limiter.bassFirst = 0.4;
    st.clipOn = true;
    st.clipOversampling = 3;

    const auto ms = rackunit::modules (st, true);
    int turnable = 0;
    for (const auto& m : ms)
    {
        CAPTURE (m.key);
        CHECK (m.switchable == (m.key != "mas.max"));
        for (const auto& v : m.values)
        {
            CAPTURE (v.name);
            const bool shownOnly = m.key == "mas.dynamics" || (m.key == "mas.steady" && v.name == "Target") || (m.key == "mas.max" && v.name == "Target");
            CHECK (v.key.empty() == shownOnly);
            if (v.key.empty()) continue;
            double back = 0.0;
            CHECK (rackunit::fromKnob (v.key, v.norm, back));
            ++turnable;
        }
    }
    CHECK (turnable > 40);
    CHECK (rackunit::switchOf ("mas.max") == nullptr);
    CHECK (std::string (rackunit::switchOf ("mas.glue")) == "glueOn");

    auto back = [&] (const char* module, const char* name) {
        const auto* v = value (*find (ms, module), name);
        REQUIRE (v != nullptr);
        double x = -999.0;
        REQUIRE (rackunit::fromKnob (v->key, v->norm, x));
        return x;
    };
    CHECK (back ("mas.unlimiter", "Length") == Catch::Approx (22.0).margin (1e-3));      // JUCE skew factor
    CHECK (back ("mas.glue", "Ratio") == Catch::Approx (4.0).margin (1e-3));
    CHECK (back ("mas.glue", "Attack") == Catch::Approx (10.0).margin (1e-3));
    CHECK (back ("mas.glue", "Ignore Lows") == Catch::Approx (120.0).margin (1e-3));     // skew for a centre (90 Hz)
    CHECK (back ("mas.glue", "Mix") == Catch::Approx (60.0).margin (1e-3));              // the parameter's %, not 0..1
    CHECK (back ("mas.glue", "Character") == 2.0);
    CHECK (value (*find (ms, "mas.glue"), "Character")->steps == 3);
    CHECK (back ("mas.bass", "Sub Tame") == Catch::Approx (35.0).margin (1e-3));
    CHECK (back ("mas.tonaleq", "Mid") == Catch::Approx (-2.5).margin (1e-3));
    CHECK (value (*find (ms, "mas.tonaleq"), "Mid")->key == "eqGain4");
    CHECK (back ("mas.max", "Release") == Catch::Approx (300.0).margin (1e-2));
    CHECK (back ("mas.max", "Bass First") == Catch::Approx (40.0).margin (1e-3));
    CHECK (back ("mas.clipper", "Oversampling") == 3.0);

    double x = 0.0;
    REQUIRE (rackunit::fromKnob ("glueThr", 5.0f, x));
    CHECK (x == 0.0);
    REQUIRE (rackunit::fromKnob ("glueThr", std::nanf (""), x));
    CHECK (x == -40.0);
    CHECK_FALSE (rackunit::fromKnob ("dynThr1", 0.5f, x));
}

TEST_CASE ("Rack unit (Masterology): the Rack's knobs are Masterology's parameters", "[rack]")
{
    std::string params;
    for (const char* file : { "/plugin/PluginProcessor.cpp", "/plugin/ModuleParams.h" })
    {
        std::ifstream in (std::string (POLISHER_SOURCE_DIR) + file);
        REQUIRE (in.good());
        params += std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }
    rackunit::State st;
    st.clipOn = true;
    for (const auto& m : rackunit::modules (st, true))
    {
        if (const char* sw = rackunit::switchOf (m.key)) CHECK (params.find ("\"" + std::string (sw) + "\"") != std::string::npos);
        for (const auto& v : m.values)
        {
            if (v.key.empty()) continue;
            CAPTURE (v.key);
            const bool eq = v.key.rfind ("eqGain", 0) == 0;
            CHECK (params.find (eq ? std::string ("return \"eqGain\" + juce::String (band + 1)") : "\"" + v.key + "\"") != std::string::npos);
        }
    }
}
