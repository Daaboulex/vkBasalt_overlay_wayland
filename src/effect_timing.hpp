#ifndef EFFECT_TIMING_HPP_INCLUDED
#define EFFECT_TIMING_HPP_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vkBasalt
{
    inline bool validEffectTimingQueryAllocation(
        size_t effectCount, uint32_t imageCount)
    {
        if (effectCount == 0 || imageCount == 0
            || imageCount > std::numeric_limits<uint32_t>::max() / 2u)
            return false;
        return effectCount
            <= std::numeric_limits<uint32_t>::max() / (2u * imageCount);
    }

    inline uint32_t effectTimingQueryBase(
        uint32_t imageIndex, uint32_t effectCount)
    {
        return imageIndex * effectCount * 2u;
    }

    inline uint64_t effectTimestampDelta(
        uint64_t begin, uint64_t end, uint32_t validBits)
    {
        if (validBits == 0 || validBits > 64)
            return 0;
        if (validBits == 64)
            return end - begin;

        const uint64_t mask = (uint64_t{1} << validBits) - 1;
        return (end - begin) & mask;
    }

    inline float effectTimestampMilliseconds(
        uint64_t begin, uint64_t end, uint32_t validBits,
        float timestampPeriodNanoseconds)
    {
        if (!(timestampPeriodNanoseconds > 0.0f)
            || !std::isfinite(timestampPeriodNanoseconds))
            return 0.0f;

        const double nanoseconds = static_cast<double>(
            effectTimestampDelta(begin, end, validBits))
            * static_cast<double>(timestampPeriodNanoseconds);
        const double milliseconds = nanoseconds / 1'000'000.0;
        if (!std::isfinite(milliseconds) || milliseconds < 0.0)
            return 0.0f;
        return static_cast<float>(milliseconds);
    }

    inline float smoothEffectTiming(
        float previousMilliseconds, float sampleMilliseconds,
        bool initialized, float sampleWeight = 0.15f)
    {
        if (!std::isfinite(sampleMilliseconds) || sampleMilliseconds < 0.0f)
            return initialized ? previousMilliseconds : 0.0f;
        if (!initialized || !std::isfinite(previousMilliseconds)
            || previousMilliseconds < 0.0f)
            return sampleMilliseconds;

        const float weight = std::clamp(sampleWeight, 0.0f, 1.0f);
        return previousMilliseconds
            + weight * (sampleMilliseconds - previousMilliseconds);
    }
}

#endif
