#include "ToneTargetStore.h"

#include <cmath>

void ToneTargetStore::set (const polisher::ToneTarget& t)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    target = t.valid ? t : polisher::ToneTarget {};
    version.fetch_add (1, std::memory_order_acq_rel);
}

polisher::ToneTarget ToneTargetStore::get() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return target;
}

bool ToneTargetStore::isSet() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return target.valid;
}

bool ToneTargetStore::fetch (polisher::ToneTarget& out, int& seenVersion) const noexcept
{
    if (version.load (std::memory_order_acquire) == seenVersion)
        return false;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked())
        return false;   // being written right now: next block
    out = target;
    seenVersion = version.load (std::memory_order_acquire);
    return true;
}

juce::String ToneTargetStore::toJson() const
{
    const auto t = get();
    if (! t.valid)
        return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", kFormat);
    o->setProperty ("seconds", t.seconds);
    if (t.hasLevel())
        o->setProperty ("level", t.levelDb);   // the song's level (optional: targets saved before it have none)
    juce::Array<juce::var> bands;
    for (float v : t.bandDb) bands.add (static_cast<double> (v));
    o->setProperty ("bands", bands);
    return juce::JSON::toString (juce::var (o), true);
}

void ToneTargetStore::restoreFromJson (const juce::String& json)
{
    polisher::ToneTarget t;
    const auto v = json.isNotEmpty() ? juce::JSON::parse (json) : juce::var();
    const auto bandsVar = v.getProperty ("bands", {});
    if (const auto* bands = bandsVar.getArray();
        bands != nullptr && static_cast<int> (v.getProperty ("format", 0)) == kFormat && bands->size() == polisher::kToneBands)
    {
        t.valid = true;
        for (int b = 0; b < polisher::kToneBands; ++b)
        {
            const auto& x = bands->getReference (b);
            const double d = x.isDouble() || x.isInt() || x.isInt64() ? static_cast<double> (x) : NAN;
            if (! std::isfinite (d)) { t.valid = false; break; }
            t.bandDb[static_cast<size_t> (b)] = static_cast<float> (d);
        }
        const auto secs = v.getProperty ("seconds", 0.0);
        t.seconds = secs.isDouble() || secs.isInt() || secs.isInt64() ? static_cast<double> (secs) : 0.0;
        const auto level = v.getProperty ("level", {});
        const double l = level.isDouble() || level.isInt() || level.isInt64() ? static_cast<double> (level) : NAN;
        t.levelDb = std::isfinite (l) && l > polisher::ToneTarget::kNoLevel && l < 100.0 ? l : polisher::ToneTarget::kNoLevel;
    }
    set (t);
}
