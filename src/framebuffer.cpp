#include "framebuffer.hpp"
#include "logger.hpp"

namespace vkBasalt
{
    std::vector<VkFramebuffer>
    createFramebuffers(LogicalDevice* pLogicalDevice, VkRenderPass renderPass, VkExtent2D& extent, std::vector<std::vector<VkImageView>> imageViews)
    {
        if (imageViews.empty() || imageViews[0].empty())
        {
            Logger::warn("createFramebuffers: empty imageViews");
            return {};
        }
        std::vector<VkFramebuffer> framebuffers(
            imageViews[0].size(), VK_NULL_HANDLE);
        std::vector<VkImageView>   perFrameImageViews;
        for (uint32_t i = 0; i < imageViews[0].size(); i++)
        {
            for (auto& iv : imageViews)
            {
                perFrameImageViews.push_back(iv[i]);
            }

            VkFramebufferCreateInfo framebufferCreateInfo;
            framebufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCreateInfo.pNext           = nullptr;
            framebufferCreateInfo.flags           = 0;
            framebufferCreateInfo.renderPass      = renderPass;
            framebufferCreateInfo.attachmentCount = perFrameImageViews.size();
            framebufferCreateInfo.pAttachments    = perFrameImageViews.data();
            framebufferCreateInfo.width           = extent.width;
            framebufferCreateInfo.height          = extent.height;
            framebufferCreateInfo.layers          = 1;

            VkResult result = pLogicalDevice->vkd.CreateFramebuffer(pLogicalDevice->device, &framebufferCreateInfo, nullptr, &(framebuffers[i]));
            if (result != VK_SUCCESS)
            {
                for (VkFramebuffer created : framebuffers)
                {
                    if (created != VK_NULL_HANDLE)
                        pLogicalDevice->vkd.DestroyFramebuffer(
                            pLogicalDevice->device, created, nullptr);
                }
                ASSERT_VULKAN(result);
                return {};
            }
            perFrameImageViews.clear();
        }
        return framebuffers;
    }
} // namespace vkBasalt
