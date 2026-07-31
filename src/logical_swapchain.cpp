#include "memory.hpp"
#include "logical_swapchain.hpp"

namespace vkBasalt
{
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

            for (VkDeviceMemory memory : fakeImageMemory)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);

            for (uint32_t i = 0; i < fakeImages.size(); i++)
            {
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, fakeImages[i], nullptr);
            }

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
