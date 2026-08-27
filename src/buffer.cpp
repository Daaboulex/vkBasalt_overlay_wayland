#include "buffer.hpp"
#include "memory.hpp"

#include <stdexcept>

namespace vkBasalt
{
    void createBuffer(LogicalDevice*        pLogicalDevice,
                      VkDeviceSize          size,
                      VkBufferUsageFlags    usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer&             buffer,
                      VkDeviceMemory&       bufferMemory)
    {
        buffer = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
        VkBufferCreateInfo bufferInfo = {};

        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = size;
        bufferInfo.usage       = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = pLogicalDevice->vkd.CreateBuffer(pLogicalDevice->device, &bufferInfo, nullptr, &buffer);
        if (result != VK_SUCCESS)
            throw std::runtime_error(
                "vkCreateBuffer failed while constructing an effect: "
                + std::to_string(result));

        VkMemoryRequirements memRequirements;
        pLogicalDevice->vkd.GetBufferMemoryRequirements(pLogicalDevice->device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};

        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryTypeIndex(pLogicalDevice, memRequirements.memoryTypeBits, properties);

        result = allocateTrackedMemory(pLogicalDevice, &allocInfo, nullptr, &bufferMemory);
        if (result != VK_SUCCESS)
        {
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw std::runtime_error(
                "vkAllocateMemory failed while constructing an effect buffer: "
                + std::to_string(result));
        }

        result = pLogicalDevice->vkd.BindBufferMemory(pLogicalDevice->device, buffer, bufferMemory, 0);
        if (result != VK_SUCCESS)
        {
            freeTrackedMemory(pLogicalDevice, bufferMemory, nullptr);
            pLogicalDevice->vkd.DestroyBuffer(pLogicalDevice->device, buffer, nullptr);
            bufferMemory = VK_NULL_HANDLE;
            buffer = VK_NULL_HANDLE;
            throw std::runtime_error(
                "vkBindBufferMemory failed while constructing an effect: "
                + std::to_string(result));
        }
    }

} // namespace vkBasalt
