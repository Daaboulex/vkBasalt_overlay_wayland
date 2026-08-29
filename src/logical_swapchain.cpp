#include "memory.hpp"
#include "logical_swapchain.hpp"
#include "effect_timing.hpp"

#include <algorithm>

namespace vkBasalt
{
    bool LogicalSwapchain::initializeEffectTimings(
        const std::vector<std::string>& effectNames)
    {
        destroyEffectTimings();

        const bool allFencesValid = effectFences.size() == imageCount
            && std::all_of(effectFences.begin(), effectFences.end(),
                           [](VkFence fence) { return fence != VK_NULL_HANDLE; });
        if (effectNames.empty() || imageCount == 0 || !allFencesValid
            || pLogicalDevice->timestampValidBits == 0
            || !(pLogicalDevice->timestampPeriodNanoseconds > 0.0f)
            || pLogicalDevice->vkd.CreateQueryPool == nullptr
            || pLogicalDevice->vkd.DestroyQueryPool == nullptr
            || pLogicalDevice->vkd.GetQueryPoolResults == nullptr
            || pLogicalDevice->vkd.CmdResetQueryPool == nullptr
            || pLogicalDevice->vkd.CmdWriteTimestamp == nullptr)
            return false;

        if (!validEffectTimingQueryAllocation(effectNames.size(), imageCount))
        {
            Logger::warn("too many effects/images for timestamp query allocation");
            return false;
        }

        timingQueryStride = static_cast<uint32_t>(effectNames.size() * 2u);
        VkQueryPoolCreateInfo queryInfo = {};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = timingQueryStride * imageCount;

        const VkResult result = pLogicalDevice->vkd.CreateQueryPool(
            pLogicalDevice->device, &queryInfo, nullptr, &timingQueryPool);
        if (result != VK_SUCCESS)
        {
            timingQueryPool = VK_NULL_HANDLE;
            timingQueryStride = 0;
            Logger::warn("could not create optional per-effect timestamp query pool: "
                         + std::to_string(result));
            return false;
        }

        timingEffectNames = effectNames;
        timingMilliseconds.assign(effectNames.size(), 0.0f);
        timingValid.assign(effectNames.size(), false);
        timingSamplesPending.assign(imageCount, false);
        return true;
    }

    void LogicalSwapchain::destroyEffectTimings()
    {
        if (timingQueryPool != VK_NULL_HANDLE)
        {
            pLogicalDevice->vkd.DestroyQueryPool(
                pLogicalDevice->device, timingQueryPool, nullptr);
            timingQueryPool = VK_NULL_HANDLE;
        }
        timingQueryStride = 0;
        timingEffectNames.clear();
        timingMilliseconds.clear();
        timingValid.clear();
        timingSamplesPending.clear();
    }

    VkResult LogicalSwapchain::collectEffectTimings(uint32_t imageIndex)
    {
        if (timingQueryPool == VK_NULL_HANDLE
            || imageIndex >= timingSamplesPending.size()
            || !timingSamplesPending[imageIndex])
            return VK_SUCCESS;

        timingSamplesPending[imageIndex] = false;
        const uint32_t queryBase = effectTimingQueryBase(
            imageIndex, static_cast<uint32_t>(timingEffectNames.size()));
        std::vector<uint64_t> timestamps(timingQueryStride, 0);
        const VkResult result = pLogicalDevice->vkd.GetQueryPoolResults(
            pLogicalDevice->device, timingQueryPool, queryBase,
            timingQueryStride, timestamps.size() * sizeof(uint64_t),
            timestamps.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result != VK_SUCCESS)
            return result;

        for (size_t effectIndex = 0;
             effectIndex < timingEffectNames.size(); ++effectIndex)
        {
            const bool firstSample = !timingValid[effectIndex];
            const float sample = effectTimestampMilliseconds(
                timestamps[effectIndex * 2], timestamps[effectIndex * 2 + 1],
                pLogicalDevice->timestampValidBits,
                pLogicalDevice->timestampPeriodNanoseconds);
            timingMilliseconds[effectIndex] = smoothEffectTiming(
                timingMilliseconds[effectIndex], sample,
                timingValid[effectIndex]);
            timingValid[effectIndex] = true;
            if (firstSample)
            {
                Logger::debug("per-effect GPU timing active for "
                              + timingEffectNames[effectIndex] + ": "
                              + std::to_string(sample) + " ms");
            }
        }
        return VK_SUCCESS;
    }

    void LogicalSwapchain::markTimingSubmission(
        uint32_t imageIndex, bool effectCommandsSubmitted)
    {
        if (imageIndex < timingSamplesPending.size())
            timingSamplesPending[imageIndex] =
                effectCommandsSubmitted && timingQueryPool != VK_NULL_HANDLE;
    }

    void LogicalSwapchain::destroy()
    {
        if (imageCount > 0)
        {
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

            effects.clear();
            defaultTransfer.reset();

            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersEffect.size(), commandBuffersEffect.data());
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersNoEffect.size(), commandBuffersNoEffect.data());
            Logger::debug("after free commandbuffer");

            destroyEffectTimings();

            // An image bound to a memory object outlives its backing only as invalid usage, so the
            // images go before the block they were all suballocated from.
            for (VkImage image : fakeImages)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);

            for (VkDeviceMemory memory : fakeImageMemory)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);

            for (unsigned int i = 0; i < imageCount; i++)
            {
                if (i < effectFences.size() && effectFences[i] != VK_NULL_HANDLE)
                    pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, effectFences[i], nullptr);
                pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, semaphores[i], nullptr);
                pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, overlaySemaphores[i], nullptr);
            }
            Logger::debug("after DestroySemaphore");

            for (auto& view : imageViews)
            {
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, view, nullptr);
            }
            imageViews.clear();

        }
    }
} // namespace vkBasalt
