#ifndef DEPTH_SOURCE_LAYOUT_HPP_INCLUDED
#define DEPTH_SOURCE_LAYOUT_HPP_INCLUDED

#include <optional>
#include <string_view>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    inline std::optional<VkImageLayout> parseDepthSourceLayout(std::string_view configured)
    {
        if (configured.empty() || configured == "attachment"
            || configured == "attachment-optimal"
            || configured == "depth-stencil-attachment-optimal")
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        if (configured == "general")
            return VK_IMAGE_LAYOUT_GENERAL;
        return std::nullopt;
    }

    inline VkPipelineStageFlags depthSourceProducerStages(VkImageLayout layout)
    {
        return layout == VK_IMAGE_LAYOUT_GENERAL
            ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
            : VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }

    inline VkAccessFlags depthSourceProducerAccess(VkImageLayout layout)
    {
        return layout == VK_IMAGE_LAYOUT_GENERAL
            ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT
            : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    inline constexpr VkPipelineStageFlags depthShaderConsumerStages()
    {
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
}

#endif
