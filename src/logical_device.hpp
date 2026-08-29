#ifndef LOGICAL_DEVICE_HPP_INCLUDED
#define LOGICAL_DEVICE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

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
        float                    timestampPeriodNanoseconds = 0.0f;
        uint32_t                 timestampValidBits = 0;
        bool                     supportsMutableFormat;
        bool                     supportsStorageImageWithoutFormat = false;
        // One record per depth image. Three parallel vectors indexed positionally used to drift
        // apart, because a view was only appended when the image being bound happened to be the
        // one created last, which is not true when an application creates images from several
        // threads. A record cannot drift from itself.
        struct DepthImage
        {
            VkImage     image  = VK_NULL_HANDLE;
            VkFormat    format = VK_FORMAT_UNDEFINED;
            VkImageView view   = VK_NULL_HANDLE;
        };
        std::vector<DepthImage>  depthImages;

        std::unique_ptr<OverlayPersistentState> overlayPersistentState;

        std::unique_ptr<ImGuiOverlay> imguiOverlay;
    };
} // namespace vkBasalt

#endif // LOGICAL_DEVICE_HPP_INCLUDED
