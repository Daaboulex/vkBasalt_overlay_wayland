#include "effect_timing.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

int main()
{
    using namespace vkBasalt;

    assert(validEffectTimingQueryAllocation(2, 3));
    assert(!validEffectTimingQueryAllocation(0, 3));
    assert(!validEffectTimingQueryAllocation(2, 0));
    assert(!validEffectTimingQueryAllocation(
        std::numeric_limits<uint32_t>::max(), 3));
    assert(effectTimingQueryBase(0, 2) == 0);
    assert(effectTimingQueryBase(1, 2) == 4);
    assert(effectTimingQueryBase(2, 2) == 8);

    assert(effectTimestampDelta(100, 150, 64) == 50);
    assert(effectTimestampDelta(UINT64_MAX - 3, 2, 64) == 6);
    assert(effectTimestampDelta(250, 5, 8) == 11);
    assert(effectTimestampDelta(1, 2, 0) == 0);
    assert(effectTimestampDelta(1, 2, 65) == 0);

    const float milliseconds = effectTimestampMilliseconds(
        100, 2'000'100, 64, 0.5f);
    assert(std::abs(milliseconds - 1.0f) < 0.0001f);
    assert(effectTimestampMilliseconds(1, 2, 64, 0.0f) == 0.0f);
    assert(effectTimestampMilliseconds(
        1, 2, 64, std::numeric_limits<float>::infinity()) == 0.0f);

    assert(smoothEffectTiming(0.0f, 4.0f, false) == 4.0f);
    assert(std::abs(smoothEffectTiming(4.0f, 6.0f, true, 0.25f) - 4.5f)
           < 0.0001f);
    assert(smoothEffectTiming(4.0f, -1.0f, true) == 4.0f);
    return 0;
}
