#ifndef DEPTH_IDENTITY_HPP_INCLUDED
#define DEPTH_IDENTITY_HPP_INCLUDED

#include <cstdint>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    // Vulkan may reuse a non-dispatchable handle after destruction. Pair the
    // raw handle with an interception serial so a later allocation can never
    // validate command buffers recorded against the previous lifetime.
    struct DepthIdentity
    {
        VkImage image = VK_NULL_HANDLE;
        uint64_t creationSerial = 0;

        bool valid() const
        {
            return image != VK_NULL_HANDLE && creationSerial != 0;
        }

        bool operator==(const DepthIdentity& other) const
        {
            return image == other.image && creationSerial == other.creationSerial;
        }

        bool operator!=(const DepthIdentity& other) const
        {
            return !(*this == other);
        }
    };

    struct DepthImage
    {
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent3D extent{0, 0, 0};
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = 0;
        uint64_t creationSerial = 0;

        DepthIdentity identity() const
        {
            return {image, creationSerial};
        }
    };

    inline bool destroyingDepthIdentityInvalidatesBinding(
        const DepthIdentity& bound, const DepthIdentity& destroyed)
    {
        return destroyed.valid() && bound == destroyed;
    }
}

#endif
