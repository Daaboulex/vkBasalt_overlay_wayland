#include "image.hpp"
#include "memory.hpp"
#include "buffer.hpp"
#include "format.hpp"

#include <stdexcept>

namespace vkBasalt
{
    // Waits for this one submission rather than draining the queue. A drain also waits out whatever
    // the application has already queued, measured at 45 percent of the wait. Falls back to a drain
    // if a fence cannot be had, which is slower but equally correct.
    static void submitAndWait(LogicalDevice* pLogicalDevice, VkCommandBuffer commandBuffer)
    {
        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fence = VK_NULL_HANDLE;
        if (pLogicalDevice->vkd.CreateFence(pLogicalDevice->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
            fence = VK_NULL_HANDLE;

        const VkResult submitted = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, fence);

        if (submitted == VK_SUCCESS && fence != VK_NULL_HANDLE)
            pLogicalDevice->vkd.WaitForFences(pLogicalDevice->device, 1, &fence, VK_TRUE, UINT64_MAX);
        else
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        if (fence != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, fence, nullptr);
    }

    std::vector<VkImage> createImages(LogicalDevice*        pLogicalDevice,
                                      uint32_t              count,
                                      VkExtent3D            extent,
                                      VkFormat              format,
                                      VkImageUsageFlags     usage,
                                      VkMemoryPropertyFlags properties,
                                      VkDeviceMemory&       imageMemory,
                                      uint32_t              mipLevels)
    {
        std::vector<VkImage> images(count, VK_NULL_HANDLE);
        imageMemory = VK_NULL_HANDLE;
        if (count == 0)
            return images;

        auto abandon = [&](VkResult result, const std::string& operation) -> void {
            for (VkImage image : images)
            {
                if (image != VK_NULL_HANDLE)
                    pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            }
            if (imageMemory != VK_NULL_HANDLE)
            {
                freeTrackedMemory(pLogicalDevice, imageMemory, nullptr);
                imageMemory = VK_NULL_HANDLE;
            }
            throw std::runtime_error(
                operation + " failed while creating effect images: " + std::to_string(result));
        };

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
        imageFormatListCreateInfo.pNext           = nullptr;
        imageFormatListCreateInfo.viewFormatCount = 2;
        imageFormatListCreateInfo.pViewFormats    = formats;

        VkImageCreateInfo imageCreateInfo;
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext = (unormFormat == srgbFormat) ? nullptr : &imageFormatListCreateInfo;
        imageCreateInfo.flags = (unormFormat == srgbFormat) ? 0 : VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (extent.depth == 1)
        {
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        }
        else
        {
            imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
        }
        imageCreateInfo.format                = format;
        imageCreateInfo.extent                = extent;
        imageCreateInfo.mipLevels             = mipLevels;
        imageCreateInfo.arrayLayers           = 1;
        imageCreateInfo.samples               = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling                = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage                 = usage;
        imageCreateInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.queueFamilyIndexCount = 0;       // Don't care
        imageCreateInfo.pQueueFamilyIndices   = nullptr; // Don't care
        imageCreateInfo.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result;
        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.CreateImage(pLogicalDevice->device, &imageCreateInfo, nullptr, &(images[i]));
            if (result != VK_SUCCESS)
                abandon(result, "vkCreateImage");
        }
        VkMemoryRequirements memoryRequirements;
        pLogicalDevice->vkd.GetImageMemoryRequirements(pLogicalDevice->device, images[0], &memoryRequirements);

        if (memoryRequirements.size % memoryRequirements.alignment != 0)
        {
            memoryRequirements.size = (memoryRequirements.size / memoryRequirements.alignment + 1) * memoryRequirements.alignment;
        }

        VkMemoryAllocateInfo memoryAllocateInfo;
        memoryAllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext           = nullptr;
        memoryAllocateInfo.allocationSize  = memoryRequirements.size * count;
        memoryAllocateInfo.memoryTypeIndex = findMemoryTypeIndex(pLogicalDevice, memoryRequirements.memoryTypeBits, properties);

        result = allocateTrackedMemory(pLogicalDevice, &memoryAllocateInfo, nullptr, &imageMemory);
        if (result != VK_SUCCESS)
            abandon(result, "vkAllocateMemory");

        for (uint32_t i = 0; i < count; i++)
        {
            result = pLogicalDevice->vkd.BindImageMemory(pLogicalDevice->device, images[i], imageMemory, memoryRequirements.size * i);
            if (result != VK_SUCCESS)
                abandon(result, "vkBindImageMemory");
        }
        return images;
    }

    void
    uploadToImage(LogicalDevice* pLogicalDevice, VkImage image, VkExtent3D extent, uint32_t size, const unsigned char* writeData, uint32_t mipLevels)
    {

        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingMemory;

        createBuffer(pLogicalDevice,
                     size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer,
                     stagingMemory);
        void*    data;
        VkResult result = pLogicalDevice->vkd.MapMemory(pLogicalDevice->device, stagingMemory, 0, size, 0, &data);
        ASSERT_VULKAN(result);
        std::memcpy(data, writeData, size);
        pLogicalDevice->vkd.UnmapMemory(pLogicalDevice->device, stagingMemory);

        VkCommandBufferAllocateInfo allocInfo = {};

        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = pLogicalDevice->commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, &commandBuffer);
        // initialize dispatch table for commandBuffer since it is a dispatchable object
        initializeDispatchTable(commandBuffer, pLogicalDevice->device);

        VkCommandBufferBeginInfo beginInfo = {};

        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        pLogicalDevice->vkd.BeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext                           = nullptr;
        memoryBarrier.srcAccessMask                   = 0;
        memoryBarrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        memoryBarrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image                           = image;
        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        VkBufferImageCopy region;
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = extent;

        pLogicalDevice->vkd.CmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

        generateMipMaps(pLogicalDevice, commandBuffer, image, extent, mipLevels);

        pLogicalDevice->vkd.EndCommandBuffer(commandBuffer);

        submitAndWait(pLogicalDevice, commandBuffer);

        pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool, 1, &commandBuffer);
        freeTrackedMemory(pLogicalDevice, stagingMemory, nullptr);
        pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, stagingBuffer, nullptr);
    }

    // ReShade guarantees effect textures start zeroed. Sampling an image that
    // was only layout-transitioned reads whatever the memory held before --
    // often NaN, which a temporal feedback loop then never recovers from.
    void clearAndReadyImages(LogicalDevice* pLogicalDevice, std::vector<VkImage> images, uint32_t mipLevels)
    {
        VkCommandBufferAllocateInfo allocInfo = {};

        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = pLogicalDevice->commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, &commandBuffer);
        // initialize dispatch table for commandBuffer since it is a dispatchable object
        initializeDispatchTable(commandBuffer, pLogicalDevice->device);

        VkCommandBufferBeginInfo beginInfo = {};

        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        pLogicalDevice->vkd.BeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext               = nullptr;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.srcAccessMask       = 0;
        memoryBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = mipLevels;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        for (auto& image : images)
        {
            memoryBarrier.image = image;
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        }

        const VkClearColorValue zero = {};
        for (auto& image : images)
        {
            pLogicalDevice->vkd.CmdClearColorImage(
                commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &memoryBarrier.subresourceRange);
        }

        memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        for (auto& image : images)
        {
            memoryBarrier.image = image;
            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        }

        pLogicalDevice->vkd.EndCommandBuffer(commandBuffer);

        submitAndWait(pLogicalDevice, commandBuffer);

        pLogicalDevice->vkd.FreeCommandBuffers(pLogicalDevice->device, pLogicalDevice->commandPool, 1, &commandBuffer);
    }

    void generateMipMaps(LogicalDevice* pLogicalDevice, VkCommandBuffer commandBuffer, VkImage image, VkExtent3D extent, uint32_t mipLevels)
    {
        if (mipLevels < 2)
        {
            return;
        }
        int32_t width  = extent.width;
        int32_t height = extent.height;
        int32_t depth  = extent.depth;

        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext               = nullptr;
        memoryBarrier.image               = image;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        memoryBarrier.srcAccessMask       = 0;
        memoryBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        for (uint32_t i = 1; i < mipLevels; i++)
        {
            VkImageBlit imageBlit;

            imageBlit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.srcSubresource.mipLevel       = i - 1;
            imageBlit.srcSubresource.baseArrayLayer = 0;
            imageBlit.srcSubresource.layerCount     = 1;
            imageBlit.srcOffsets[0]                 = {0, 0, 0};
            imageBlit.srcOffsets[1]                 = {width, height, depth};

            width  = (width == 1) ? 1 : width / 2;
            height = (height == 1) ? 1 : height / 2;
            depth  = (depth == 1) ? 1 : depth / 2;

            imageBlit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.dstSubresource.mipLevel       = i;
            imageBlit.dstSubresource.baseArrayLayer = 0;
            imageBlit.dstSubresource.layerCount     = 1;
            imageBlit.dstOffsets[0]                 = {0, 0, 0};
            imageBlit.dstOffsets[1]                 = {width, height, depth};

            memoryBarrier.subresourceRange.baseMipLevel = i - 1;

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            memoryBarrier.srcAccessMask = 0;
            memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

            memoryBarrier.subresourceRange.baseMipLevel = i;

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            memoryBarrier.srcAccessMask = 0;
            memoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

            pLogicalDevice->vkd.CmdBlitImage(commandBuffer,
                                             image,
                                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                             image,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             1,
                                             &imageBlit,
                                             VK_FILTER_LINEAR);

            memoryBarrier.subresourceRange.baseMipLevel = i - 1;

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

            memoryBarrier.subresourceRange.baseMipLevel = i;

            memoryBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            memoryBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            pLogicalDevice->vkd.CmdPipelineBarrier(
                commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        }
    }
} // namespace vkBasalt
