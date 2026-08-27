#ifndef DEPTH_REBIND_STATE_HPP_INCLUDED
#define DEPTH_REBIND_STATE_HPP_INCLUDED

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace vkBasalt
{
    inline int32_t sanitizeDepthRebindDebounceMs(int32_t configured)
    {
        return std::clamp(configured, int32_t{0}, int32_t{10000});
    }

    inline uint32_t sanitizeDepthRebindStablePresents(int32_t configured)
    {
        return static_cast<uint32_t>(
            std::clamp(configured, int32_t{0}, int32_t{10000}));
    }

    inline bool depthTargetStableForRebind(
        std::chrono::steady_clock::time_point changedAt,
        std::chrono::steady_clock::time_point now,
        uint32_t stablePresents,
        int32_t requiredStablePresents,
        int32_t debounceMs)
    {
        const bool enoughPresents = stablePresents
            >= sanitizeDepthRebindStablePresents(requiredStablePresents);
        const bool enoughTime = changedAt == std::chrono::steady_clock::time_point{}
            || now - changedAt >= std::chrono::milliseconds(
                sanitizeDepthRebindDebounceMs(debounceMs));
        return enoughPresents && enoughTime;
    }

    inline bool shouldSubmitDepthEffects(
        bool globallyEnabled, bool depthSelectionDirty)
    {
        return globallyEnabled && !depthSelectionDirty;
    }

    inline bool shouldAttemptDepthRebind(
        bool depthSelectionDirty, bool hasDepthIdentity)
    {
        return depthSelectionDirty && hasDepthIdentity;
    }
}

#endif
