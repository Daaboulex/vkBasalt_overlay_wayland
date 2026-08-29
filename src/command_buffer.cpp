#include "command_buffer.hpp"

#include "format.hpp"
#include "util.hpp"
#include "effect_timing.hpp"

namespace vkBasalt
{
    std::vector<VkCommandBuffer> allocateCommandBuffer(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        std::vector<VkCommandBuffer> commandBuffers(count);

        VkCommandBufferAllocateInfo allocInfo;
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.pNext              = nullptr;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = pLogicalDevice->commandPool;
        allocInfo.commandBufferCount = count;

        VkResult result = pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, commandBuffers.data());
        ASSERT_VULKAN(result);
        for (uint32_t i = 0; i < count; i++)
        {
            // initialize dispatch tables for commandBuffers since the are dispatchable objects
            initializeDispatchTable(commandBuffers[i], pLogicalDevice->device);
        }

        return commandBuffers;
    }
    void writeCommandBuffers(LogicalDevice*                                 pLogicalDevice,
                             std::vector<std::shared_ptr<vkBasalt::Effect>> effects,
                             VkImage                                        depthImage,
                             VkImageView                                    depthImageView,
                             VkFormat                                       depthFormat,
                             std::vector<VkCommandBuffer>                   commandBuffers,
                             VkQueryPool                                    timingQueryPool,
                             uint32_t                                       timedEffectCount)
    {
        VkCommandBufferBeginInfo beginInfo = {};

        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext            = nullptr;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        for (auto& effect : effects)
        {
            effect->useDepthImage(depthImageView);
        }

        if (timedEffectCount > effects.size())
        {
            Logger::warn("timestamped effect count exceeds recorded effect count; disabling timing");
            timingQueryPool = VK_NULL_HANDLE;
            timedEffectCount = 0;
        }

        for (uint32_t i = 0; i < commandBuffers.size(); i++)
        {

            VkResult result = pLogicalDevice->vkd.BeginCommandBuffer(commandBuffers[i], &beginInfo);
            ASSERT_VULKAN(result);

            const uint32_t timingQueryBase = effectTimingQueryBase(
                i, timedEffectCount);
            if (timingQueryPool != VK_NULL_HANDLE && timedEffectCount != 0)
            {
                pLogicalDevice->vkd.CmdResetQueryPool(
                    commandBuffers[i], timingQueryPool,
                    timingQueryBase, timedEffectCount * 2u);
            }

            VkImageMemoryBarrier memoryBarrier;
            memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            memoryBarrier.pNext               = nullptr;
            memoryBarrier.image               = depthImage;
            // Assumes the application leaves depth in attachment-optimal; the layer tracks no transitions.
            memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memoryBarrier.subresourceRange.aspectMask =
                isStencilFormat(depthFormat) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
            memoryBarrier.subresourceRange.baseMipLevel   = 0;
            memoryBarrier.subresourceRange.levelCount     = 1;
            memoryBarrier.subresourceRange.baseArrayLayer = 0;
            memoryBarrier.subresourceRange.layerCount     = 1;

            if (depthImageView)
            {
                pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffers[i],
                                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                       0,
                                                       0,
                                                       nullptr,
                                                       0,
                                                       nullptr,
                                                       1,
                                                       &memoryBarrier);
            }

            for (uint32_t j = 0; j < effects.size(); j++)
            {
                if (timingQueryPool != VK_NULL_HANDLE && j < timedEffectCount)
                {
                    pLogicalDevice->vkd.CmdWriteTimestamp(
                        commandBuffers[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        timingQueryPool, timingQueryBase + j * 2u);
                }
                effects[j]->applyEffect(i, commandBuffers[i]);
                if (timingQueryPool != VK_NULL_HANDLE && j < timedEffectCount)
                {
                    pLogicalDevice->vkd.CmdWriteTimestamp(
                        commandBuffers[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        timingQueryPool, timingQueryBase + j * 2u + 1u);
                }
            }

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            if (depthImageView)
            {
                pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffers[i],
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                                       0,
                                                       0,
                                                       nullptr,
                                                       0,
                                                       nullptr,
                                                       1,
                                                       &memoryBarrier);
            }

            result = pLogicalDevice->vkd.EndCommandBuffer(commandBuffers[i]);
            ASSERT_VULKAN(result);
        }
    }

    std::vector<VkSemaphore> createSemaphores(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        std::vector<VkSemaphore> semaphores(count);
        VkSemaphoreCreateInfo    info;
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = 0;

        for (uint32_t i = 0; i < count; i++)
        {
            pLogicalDevice->vkd.CreateSemaphore(pLogicalDevice->device, &info, nullptr, &semaphores[i]);
        }
        return semaphores;
    }

} // namespace vkBasalt
