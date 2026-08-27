#include <chrono>
#include <cstdio>

#include "depth_rebind_state.hpp"

using namespace vkBasalt;

int main()
{
    using clock = std::chrono::steady_clock;
    const clock::time_point changed{std::chrono::seconds(10)};

    if (sanitizeDepthRebindDebounceMs(-1) != 0
        || sanitizeDepthRebindDebounceMs(15000) != 10000
        || sanitizeDepthRebindStablePresents(-1) != 0
        || sanitizeDepthRebindStablePresents(15000) != 10000)
        return 1;

    // Time and present count are churn policy. When both are configured,
    // neither condition alone is enough to rebuild.
    if (depthTargetStableForRebind(
            changed, changed + std::chrono::milliseconds(2999), 30, 30, 3000)
        || depthTargetStableForRebind(
            changed, changed + std::chrono::milliseconds(3000), 29, 30, 3000)
        || !depthTargetStableForRebind(
            changed, changed + std::chrono::milliseconds(3000), 30, 30, 3000)
        || !depthTargetStableForRebind(changed, changed, 0, 0, 0))
        return 1;

    // Safety never waits for debounce: dirty identity always selects the
    // generation's depth-free bypass command buffer.
    if (shouldSubmitDepthEffects(true, true)
        || !shouldSubmitDepthEffects(true, false)
        || shouldSubmitDepthEffects(false, false))
        return 1;

    // A destroyed binding without a replacement remains cheaply bypassed and
    // cannot trigger an endless no-depth rebuild loop.
    if (shouldAttemptDepthRebind(true, false)
        || shouldAttemptDepthRebind(false, true)
        || !shouldAttemptDepthRebind(true, true))
        return 1;

    std::puts("Depth rebind safety and stability policy: all assertions passed");
    return 0;
}
