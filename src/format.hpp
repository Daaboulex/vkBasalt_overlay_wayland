#ifndef FORMAT_HPP_INCLUDED
#define FORMAT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    VkFormat convertToSRGB(VkFormat format);
    VkFormat convertToUNORM(VkFormat format);
    bool isSRGB(VkFormat format);
    // TODO currently return false if format is UNORM and no matching sRGB format exist
    bool isUNORM(VkFormat format);

    VkFormat getSupportedFormat(LogicalDevice*        pLogicalDevice,
                                std::vector<VkFormat> formats,
                                VkFormatFeatureFlags  features,
                                VkImageTiling         tiling = VK_IMAGE_TILING_OPTIMAL);

    VkFormat getStencilFormat(LogicalDevice* pLogicalDevice);

    bool isDepthFormat(VkFormat format);

    bool isStencilFormat(VkFormat format);
} // namespace vkBasalt

#endif // FORMAT_HPP_INCLUDED
