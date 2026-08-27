#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "effect_collection_build_spec.hpp"
#include "effect_collection_state.hpp"

using namespace vkBasalt;

template<typename Handle>
Handle fakeHandle(uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(value);
    else
        return static_cast<Handle>(value);
}

struct FakeGeneration
{
    FakeGeneration(uint64_t generation, std::string payload)
        : generation(generation), payload(std::move(payload))
    {
        ++liveCount;
    }

    ~FakeGeneration()
    {
        --liveCount;
    }

    uint64_t generation;
    std::string payload;
    static inline int liveCount = 0;
};

bool simulateTransaction(
    std::unique_ptr<FakeGeneration>& active,
    std::vector<std::unique_ptr<FakeGeneration>>& retired,
    const std::string& fault)
{
    // These checkpoints mirror the runtime's deterministic public fault
    // matrix. Every return before commit must destroy only staging state.
    if (fault == "image-growth-oom")
        return false;
    if (fault == "after-image-growth")
        return false;

    auto replacement = std::make_unique<FakeGeneration>(active->generation + 1, "new");
    if (fault == "effect-allocation-oom")
        return false;
    if (fault == "after-effects")
        return false;
    if (fault == "after-command-recording")
        return false;
    if (fault == "after-fence-creation")
        return false;
    if (fault == "stale-before-publish")
        return false;

    commitReplacementPreservingLastGood(active, retired, std::move(replacement));
    return true;
}

int main()
{
    if (classifyEffectCollectionFenceWait(false, VK_TIMEOUT)
            != EffectCollectionRetirementStatus::Ready
        || classifyEffectCollectionFenceWait(true, VK_SUCCESS)
            != EffectCollectionRetirementStatus::Ready
        || classifyEffectCollectionFenceWait(true, VK_TIMEOUT)
            != EffectCollectionRetirementStatus::Pending
        || classifyEffectCollectionFenceWait(true, VK_ERROR_DEVICE_LOST)
            != EffectCollectionRetirementStatus::Error)
        return 1;

    EffectCollectionReadiness ready;
    ready.requestedEffects = 2;
    ready.constructedRequestedEffects = 2;
    ready.runtimeEffectObjects = 3;
    ready.effectCommandBuffers = 3;
    ready.bypassCommandBuffers = 3;
    ready.fences = 3;
    ready.imageCount = 3;
    ready.hasDefaultTransfer = true;
    if (!effectCollectionUsable(ready, true))
        return 1;

    EffectCollectionReadiness fallback = ready;
    fallback.constructedRequestedEffects = 1;
    fallback.fallbackEffects = 1;
    if (effectCollectionUsable(fallback, true)
        || !effectCollectionUsable(fallback, false))
        return 1;

    const std::vector<VkFence> generationOne{
        fakeHandle<VkFence>(1), fakeHandle<VkFence>(2), fakeHandle<VkFence>(3)};
    const std::vector<VkFence> generationTwo{
        fakeHandle<VkFence>(4), fakeHandle<VkFence>(5), fakeHandle<VkFence>(6)};
    const std::vector<VkFence> reused{
        fakeHandle<VkFence>(4), fakeHandle<VkFence>(4), fakeHandle<VkFence>(7)};
    if (!nonNullHandlesDistinct(generationOne)
        || !handleSetsDisjoint(generationOne, generationTwo)
        || nonNullHandlesDistinct(reused)
        || handleSetsDisjoint(generationTwo, reused))
        return 1;

    const std::vector<VkImage> oldPool{
        fakeHandle<VkImage>(10), fakeHandle<VkImage>(11)};
    const std::vector<VkImage> grownPool{
        fakeHandle<VkImage>(10), fakeHandle<VkImage>(11), fakeHandle<VkImage>(12)};
    const std::vector<VkImage> replacedPool{
        fakeHandle<VkImage>(10), fakeHandle<VkImage>(99), fakeHandle<VkImage>(12)};
    if (!appendOnlyHandleGrowthPreservesPrefix(oldPool, grownPool)
        || appendOnlyHandleGrowthPreservesPrefix(oldPool, replacedPool))
        return 1;

    std::unique_ptr<FakeGeneration> active =
        std::make_unique<FakeGeneration>(41, "last-good");
    std::vector<std::unique_ptr<FakeGeneration>> retired;
    FakeGeneration* const exactLastGood = active.get();
    const std::array<std::string, 7> failures{
        "image-growth-oom",
        "after-image-growth",
        "effect-allocation-oom",
        "after-effects",
        "after-command-recording",
        "after-fence-creation",
        "stale-before-publish",
    };

    for (const std::string& fault : failures)
    {
        if (simulateTransaction(active, retired, fault)
            || active.get() != exactLastGood
            || active->generation != 41
            || active->payload != "last-good"
            || !retired.empty()
            || FakeGeneration::liveCount != 1)
            return 1;
    }

    if (!simulateTransaction(active, retired, "")
        || active->generation != 42
        || active->payload != "new"
        || retired.size() != 1
        || retired.front().get() != exactLastGood
        || retired.front()->payload != "last-good"
        || FakeGeneration::liveCount != 2)
        return 1;

    FakeGeneration* const secondLastGood = active.get();
    if (simulateTransaction(active, retired, "after-command-recording")
        || active.get() != secondLastGood
        || active->generation != 42
        || retired.size() != 1
        || FakeGeneration::liveCount != 2)
        return 1;

    std::puts("Transactional rollback preserves the exact last-good generation at every fault point");
    return 0;
}
