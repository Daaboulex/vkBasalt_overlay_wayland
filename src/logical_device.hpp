#ifndef LOGICAL_DEVICE_HPP_INCLUDED
#define LOGICAL_DEVICE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>

#include "depth_identity.hpp"
#include "vulkan_include.hpp"
#include "vkdispatch.hpp"

namespace vkBasalt
{
    struct OverlayPersistentState;  // Forward declaration
    class ImGuiOverlay;  // Forward declaration

    struct LogicalDevice
    {
        DeviceDispatch           vkd;
        InstanceDispatch         vki;
        VkDevice                 device;
        VkPhysicalDevice         physicalDevice;
        VkInstance               instance;
        VkQueue                  queue;
        uint32_t                 queueFamilyIndex;
        VkCommandPool            commandPool;
        bool                     supportsMutableFormat;
        bool                     supportsStorageImageWithoutFormat = false;
        std::vector<DepthImage> depthImages;
        DepthIdentity selectedDepthIdentity;
        bool depthSelectionDirty = false;
        std::chrono::steady_clock::time_point depthSelectionChangedAt{};
        uint32_t depthStablePresentCount = 0;
        bool depthRebindBypassLogged = false;
        uint64_t nextDepthCreationSerial = 1;

        void markDepthSelectionDirty()
        {
            depthSelectionDirty = true;
            depthSelectionChangedAt = std::chrono::steady_clock::now();
            depthStablePresentCount = 0;
            depthRebindBypassLogged = false;
        }

        std::unique_ptr<OverlayPersistentState> overlayPersistentState;

        std::unique_ptr<ImGuiOverlay> imguiOverlay;
    };
} // namespace vkBasalt

#endif // LOGICAL_DEVICE_HPP_INCLUDED
