#ifndef EFFECT_COLLECTION_STATE_HPP_INCLUDED
#define EFFECT_COLLECTION_STATE_HPP_INCLUDED

#include <cstddef>
#include <memory>
#include <vector>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    enum class EffectCollectionRetirementStatus
    {
        Ready,
        Pending,
        Error,
    };

    inline EffectCollectionRetirementStatus classifyEffectCollectionFenceWait(
        bool hasFences, VkResult result)
    {
        if (!hasFences || result == VK_SUCCESS)
            return EffectCollectionRetirementStatus::Ready;
        if (result == VK_TIMEOUT)
            return EffectCollectionRetirementStatus::Pending;
        return EffectCollectionRetirementStatus::Error;
    }

    struct EffectCollectionReadiness
    {
        size_t requestedEffects = 0;
        size_t constructedRequestedEffects = 0;
        size_t fallbackEffects = 0;
        size_t runtimeEffectObjects = 0;
        size_t effectCommandBuffers = 0;
        size_t bypassCommandBuffers = 0;
        size_t fences = 0;
        size_t nullFences = 0;
        size_t imageCount = 0;
        bool hasDefaultTransfer = false;
    };

    struct EffectCreationSummary
    {
        size_t requestedEffects = 0;
        size_t constructedRequestedEffects = 0;
        size_t fallbackEffects = 0;

        bool allRequestedEffectsConstructed() const
        {
            return constructedRequestedEffects == requestedEffects
                && fallbackEffects == 0;
        }
    };

    inline bool effectCollectionUsable(
        const EffectCollectionReadiness& state, bool requireAllRequestedEffects)
    {
        if (state.imageCount == 0 || !state.hasDefaultTransfer || state.runtimeEffectObjects == 0)
            return false;
        if (state.effectCommandBuffers != state.imageCount
            || state.bypassCommandBuffers != state.imageCount
            || state.fences != state.imageCount
            || state.nullFences != 0)
            return false;
        if (state.constructedRequestedEffects + state.fallbackEffects != state.requestedEffects)
            return false;
        if (requireAllRequestedEffects
            && (state.constructedRequestedEffects != state.requestedEffects
                || state.fallbackEffects != 0))
            return false;
        return true;
    }

    template<typename Handle>
    inline bool nonNullHandlesDistinct(const std::vector<Handle>& handles)
    {
        for (size_t i = 0; i < handles.size(); ++i)
        {
            if (handles[i] == VK_NULL_HANDLE)
                return false;
            for (size_t j = i + 1; j < handles.size(); ++j)
            {
                if (handles[i] == handles[j])
                    return false;
            }
        }
        return true;
    }

    template<typename Handle>
    inline bool handleSetsDisjoint(
        const std::vector<Handle>& first, const std::vector<Handle>& second)
    {
        for (Handle left : first)
        {
            if (left == VK_NULL_HANDLE)
                continue;
            for (Handle right : second)
            {
                if (left == right && right != VK_NULL_HANDLE)
                    return false;
            }
        }
        return true;
    }

    // The pointer move is the transaction's only commit point. Callers fully
    // construct and validate 'replacement' first. A failure before this call
    // cannot mutate either the active generation or the retirement list.
    template<typename Generation>
    inline void commitReplacementPreservingLastGood(
        std::unique_ptr<Generation>& active,
        std::vector<std::unique_ptr<Generation>>& retired,
        std::unique_ptr<Generation> replacement)
    {
        if (active)
            retired.push_back(std::move(active));
        active = std::move(replacement);
    }
}

#endif
