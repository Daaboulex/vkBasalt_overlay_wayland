#ifndef LOGICAL_SWAPCHAIN_HPP_INCLUDED
#define LOGICAL_SWAPCHAIN_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "effects/effect.hpp"

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    class Config;

    struct LogicalSwapchain
    {
        LogicalDevice*                       pLogicalDevice;
        VkSwapchainCreateInfoKHR             swapchainCreateInfo;
        VkExtent2D                           imageExtent;
        VkFormat                             format;
        uint32_t                             imageCount;
        std::vector<VkImage>                 images;
        std::vector<VkImageView>             imageViews;  // for overlay rendering
        std::vector<VkImage>                 fakeImages;
        size_t                               maxEffectSlots = 0;  // Max number of effects supported
        std::vector<VkCommandBuffer>         commandBuffersEffect;
        std::vector<VkCommandBuffer>         commandBuffersNoEffect;
        std::vector<VkSemaphore>             semaphores;
        std::vector<VkSemaphore>             overlaySemaphores;
        std::vector<std::shared_ptr<Effect>> effects;
        std::shared_ptr<Effect>              defaultTransfer;
        std::vector<VkDeviceMemory>          fakeImageMemory;
        // One per swapchain image, so a reload waits for the layer's own passes rather than draining
        // the whole queue, and so a command buffer is never re-recorded while it is still pending.
        std::vector<VkFence>                 effectFences;
        VkQueryPool                         timingQueryPool = VK_NULL_HANDLE;
        uint32_t                            timingQueryStride = 0;
        std::vector<std::string>            timingEffectNames;
        std::vector<float>                  timingMilliseconds;
        std::vector<bool>                   timingValid;
        std::vector<bool>                   timingSamplesPending;
        bool                                timingEnabled = false;
        bool                                timingReadErrorLogged = false;

        void destroy();
        void reloadEffects(Config* pConfig);
        bool initializeEffectTimings(const std::vector<std::string>& effectNames);
        void destroyEffectTimings();
        void collectEffectTimings(uint32_t imageIndex);
        void markTimingSubmission(uint32_t imageIndex, bool effectCommandsSubmitted);
    };
} // namespace vkBasalt

#endif // LOGICAL_SWAPCHAIN_HPP_INCLUDED
