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
#include "effect_collection_state.hpp"
#include "effect_collection_build_spec.hpp"

namespace vkBasalt
{
    class Config;
    class SharedReshadeTexturePool;

    struct EffectCollection
    {
        explicit EffectCollection(LogicalDevice* pLogicalDevice);
        ~EffectCollection();

        EffectCollection(const EffectCollection&) = delete;
        EffectCollection& operator=(const EffectCollection&) = delete;

        EffectCollectionRetirementStatus retirementStatus() const;
        VkResult waitForTrackedSubmissions();
        void release();

        LogicalDevice* pLogicalDevice;
        uint64_t generation = 0;
        EffectCollectionBuildSpec buildSpec;
        std::shared_ptr<Config> configSnapshot;
        std::vector<std::shared_ptr<Effect>> effects;
        std::shared_ptr<Effect> defaultTransfer;
        std::vector<VkCommandBuffer> commandBuffersEffect;
        std::vector<VkCommandBuffer> commandBuffersNoEffect;
        std::vector<VkFence> fences;
        std::vector<bool> submissionsInFlight;
        std::shared_ptr<SharedReshadeTexturePool> sharedTexturePool;

        bool hasCompleteSubmissionTracking(uint32_t imageCount) const;
        void markSubmissionComplete(uint32_t imageIndex);
        void markSubmissionStarted(uint32_t imageIndex);
    };

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
        std::vector<VkSemaphore>             semaphores;
        std::vector<VkSemaphore>             overlaySemaphores;
        std::vector<VkDeviceMemory>          fakeImageMemory;
        std::unique_ptr<EffectCollection>     activeEffectCollection;
        std::vector<std::unique_ptr<EffectCollection>> retiredEffectCollections;
        uint64_t                              nextEffectCollectionGeneration = 1;

        void destroy(bool queueAlreadyIdle = false);
        void reloadEffects(Config* pConfig);
    };
} // namespace vkBasalt

#endif // LOGICAL_SWAPCHAIN_HPP_INCLUDED
