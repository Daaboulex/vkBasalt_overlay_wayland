#include "renderpass.hpp"

namespace vkBasalt
{
    VkRenderPass createRenderPass(LogicalDevice* pLogicalDevice, VkFormat format)
    {
        VkRenderPass renderPass;

        VkAttachmentDescription attachmentDescription;
        attachmentDescription.flags          = 0;
        attachmentDescription.format         = format;
        attachmentDescription.samples        = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescription.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference attachmentReference;
        attachmentReference.attachment = 0;
        attachmentReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription;
        subpassDescription.flags                   = 0;
        subpassDescription.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.inputAttachmentCount    = 0;
        subpassDescription.pInputAttachments       = nullptr;
        subpassDescription.colorAttachmentCount    = 1;
        subpassDescription.pColorAttachments       = &attachmentReference;
        subpassDescription.pResolveAttachments     = nullptr;
        subpassDescription.pDepthStencilAttachment = nullptr;
        subpassDescription.preserveAttachmentCount = 0;
        subpassDescription.pPreserveAttachments    = nullptr;

        VkSubpassDependency subpassDependencies[2];

        // Incoming: wait for prior color writes before this render pass
        subpassDependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        subpassDependencies[0].dstSubpass      = 0;
        subpassDependencies[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependencies[0].srcAccessMask   = 0;
        subpassDependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDependencies[0].dependencyFlags = 0;

        // Outgoing: ensure writes + layout transition are visible before subsequent reads
        subpassDependencies[1].srcSubpass      = 0;
        subpassDependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        subpassDependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependencies[1].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDependencies[1].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
        subpassDependencies[1].dependencyFlags = 0;

        VkRenderPassCreateInfo renderPassCreateInfo;
        renderPassCreateInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.pNext           = nullptr;
        renderPassCreateInfo.flags           = 0;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments    = &attachmentDescription;
        renderPassCreateInfo.subpassCount    = 1;
        renderPassCreateInfo.pSubpasses      = &subpassDescription;
        renderPassCreateInfo.dependencyCount = 2;
        renderPassCreateInfo.pDependencies   = subpassDependencies;

        VkResult result = pLogicalDevice->vkd.CreateRenderPass(pLogicalDevice->device, &renderPassCreateInfo, nullptr, &renderPass);
        ASSERT_VULKAN(result);

        return renderPass;
    }
} // namespace vkBasalt
