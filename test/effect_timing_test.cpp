#include "effect_timing.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
    int failures = 0;

    void check(bool condition, const char* message)
    {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int main()
{
    using namespace vkBasalt;

    check(validEffectTimingQueryAllocation(2, 3), "ordinary query allocation");
    check(!validEffectTimingQueryAllocation(0, 3), "zero effects rejected");
    check(!validEffectTimingQueryAllocation(2, 0), "zero images rejected");
    check(!validEffectTimingQueryAllocation(std::numeric_limits<uint32_t>::max(), 3),
          "overflowing query allocation rejected");
    check(effectTimingQueryBase(0, 2) == 0, "first query base");
    check(effectTimingQueryBase(1, 2) == 4, "second query base");
    check(effectTimingQueryBase(2, 2) == 8, "third query base");

    check(effectTimestampDelta(100, 150, 64) == 50, "64-bit delta");
    check(effectTimestampDelta(UINT64_MAX - 3, 2, 64) == 6, "64-bit wrapped delta");
    check(effectTimestampDelta(250, 5, 8) == 11, "masked wrapped delta");
    check(effectTimestampDelta(1, 2, 0) == 0, "zero valid bits rejected");
    check(effectTimestampDelta(1, 2, 65) == 0, "excess valid bits rejected");

    const float milliseconds = effectTimestampMilliseconds(100, 2'000'100, 64, 0.5f);
    check(std::abs(milliseconds - 1.0f) < 0.0001f, "timestamp conversion");
    check(effectTimestampMilliseconds(1, 2, 64, 0.0f) == 0.0f, "zero period rejected");
    check(effectTimestampMilliseconds(1, 2, 64, std::numeric_limits<float>::infinity()) == 0.0f,
          "non-finite period rejected");

    check(smoothEffectTiming(0.0f, 4.0f, false) == 4.0f, "first timing sample");
    check(std::abs(smoothEffectTiming(4.0f, 6.0f, true, 0.25f) - 4.5f) < 0.0001f,
          "timing smoothing");
    check(smoothEffectTiming(4.0f, -1.0f, true) == 4.0f, "invalid sample ignored");

    check(effectTimingReadAction(VK_SUCCESS) == EffectTimingReadAction::Accept,
          "successful read accepted");
    check(effectTimingReadAction(VK_NOT_READY) == EffectTimingReadAction::Skip,
          "not-ready read skipped without blocking");
    check(effectTimingReadAction(VK_ERROR_DEVICE_LOST) == EffectTimingReadAction::Disable,
          "hard read error disables timing");

    return failures == 0 ? 0 : 1;
}
