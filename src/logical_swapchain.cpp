#include "memory.hpp"
#include "logical_swapchain.hpp"
#include "effects/effect_reshade.hpp"

#include <algorithm>

namespace vkBasalt
{
    EffectCollection::EffectCollection(LogicalDevice* pLogicalDevice)
        : pLogicalDevice(pLogicalDevice)
    {
    }

    EffectCollection::~EffectCollection()
    {
        release();
    }

    EffectCollectionRetirementStatus EffectCollection::retirementStatus() const
    {
        if (fences.size() != submissionsInFlight.size())
            return EffectCollectionRetirementStatus::Error;

        std::vector<VkFence> pending;
        pending.reserve(fences.size());
        for (size_t i = 0; i < fences.size(); ++i)
        {
            if (!submissionsInFlight[i])
                continue;
            if (fences[i] == VK_NULL_HANDLE)
                return EffectCollectionRetirementStatus::Error;
            pending.push_back(fences[i]);
        }

        if (pending.empty())
            return EffectCollectionRetirementStatus::Ready;

        const VkResult result = pLogicalDevice->vkd.WaitForFences(
            pLogicalDevice->device, static_cast<uint32_t>(pending.size()),
            pending.data(), VK_TRUE, 0);
        return classifyEffectCollectionFenceWait(true, result);
    }

    bool EffectCollection::hasCompleteSubmissionTracking(uint32_t imageCount) const
    {
        return fences.size() == imageCount
            && submissionsInFlight.size() == imageCount
            && nonNullHandlesDistinct(fences);
    }

    void EffectCollection::markSubmissionComplete(uint32_t imageIndex)
    {
        if (imageIndex < submissionsInFlight.size())
            submissionsInFlight[imageIndex] = false;
    }

    void EffectCollection::markSubmissionStarted(uint32_t imageIndex)
    {
        if (imageIndex < submissionsInFlight.size())
            submissionsInFlight[imageIndex] = true;
    }

    void EffectCollection::release()
    {
        if (pLogicalDevice == nullptr)
            return;

        if (!commandBuffersEffect.empty())
        {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool,
                static_cast<uint32_t>(commandBuffersEffect.size()), commandBuffersEffect.data());
            commandBuffersEffect.clear();
        }
        if (!commandBuffersNoEffect.empty())
        {
            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool,
                static_cast<uint32_t>(commandBuffersNoEffect.size()), commandBuffersNoEffect.data());
            commandBuffersNoEffect.clear();
        }

        for (VkFence fence : fences)
        {
            if (fence != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, fence, nullptr);
        }
        fences.clear();
        submissionsInFlight.clear();

        // Effect-local views and descriptors must be destroyed before their
        // collection-owned shared images.
        effects.clear();
        defaultTransfer.reset();
        sharedTexturePool.reset();
        configSnapshot.reset();
        pLogicalDevice = nullptr;
    }

    void LogicalSwapchain::destroy(bool queueAlreadyIdle)
    {
        if (imageCount > 0)
        {
            if (!queueAlreadyIdle)
                pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

            activeEffectCollection.reset();
            retiredEffectCollections.clear();
            Logger::debug("after free commandbuffer");

            for (VkImage image : fakeImages)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            for (VkDeviceMemory memory : fakeImageMemory)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);

            for (unsigned int i = 0; i < imageCount; i++)
            {
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
