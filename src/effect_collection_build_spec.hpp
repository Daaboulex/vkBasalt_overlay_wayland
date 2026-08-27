#ifndef EFFECT_COLLECTION_BUILD_SPEC_HPP_INCLUDED
#define EFFECT_COLLECTION_BUILD_SPEC_HPP_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "depth_identity.hpp"
#include "vulkan_include.hpp"

namespace vkBasalt
{
    // Immutable inputs that determine generation-owned resources and recorded
    // command buffers. A staged collection is publishable only while a fresh
    // capture of the desired state is identical.
    struct EffectCollectionBuildSpec
    {
        std::vector<std::string> orderedActiveEffects;
        std::string configState;
        std::string registryState;
        uint64_t configRevision = 0;
        uint64_t registryRevision = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkExtent2D extent{0, 0};
        uint32_t imageCount = 0;
        size_t maxEffectSlots = 0;
        std::vector<VkImage> realImages;
        std::vector<VkImage> intermediateImages;
        DepthIdentity depth;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        bool operator==(const EffectCollectionBuildSpec& other) const
        {
            return orderedActiveEffects == other.orderedActiveEffects
                && configState == other.configState
                && registryState == other.registryState
                && configRevision == other.configRevision
                && registryRevision == other.registryRevision
                && format == other.format
                && colorSpace == other.colorSpace
                && extent.width == other.extent.width
                && extent.height == other.extent.height
                && imageCount == other.imageCount
                && maxEffectSlots == other.maxEffectSlots
                && realImages == other.realImages
                && intermediateImages == other.intermediateImages
                && depth == other.depth
                && depthView == other.depthView
                && depthFormat == other.depthFormat;
        }

        bool operator!=(const EffectCollectionBuildSpec& other) const
        {
            return !(*this == other);
        }
    };

    template<typename Handle>
    inline bool appendOnlyHandleGrowthPreservesPrefix(
        const std::vector<Handle>& before, const std::vector<Handle>& after)
    {
        return after.size() >= before.size()
            && std::equal(before.begin(), before.end(), after.begin());
    }
}

#endif
