#include "memory.hpp"
#include "logger.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace vkBasalt
{
    uint32_t findMemoryTypeIndex(LogicalDevice* pLogicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
        pLogicalDevice->vki.GetPhysicalDeviceMemoryProperties(pLogicalDevice->physicalDevice, &physicalDeviceMemoryProperties);
        for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        Logger::err("Found no correct memory type");
        return 0x70AD;
    }
} // namespace vkBasalt

namespace vkBasalt
{
    namespace
    {
        std::mutex                                       g_memoryMutex;
        std::unordered_map<VkDeviceMemory, VkDeviceSize> g_memorySizes;
        VkDeviceSize                                     g_memoryBytes     = 0;
        VkDeviceSize                                     g_memoryPeakBytes = 0;
        VkDeviceSize                                     g_memorySoftLimit = 0;
        bool                                             g_softLimitWarned = false;
    } // namespace

    VkResult allocateTrackedMemory(LogicalDevice*               pLogicalDevice,
                                   const VkMemoryAllocateInfo*  pAllocateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkDeviceMemory*              pMemory)
    {
        const VkResult result = pLogicalDevice->vkd.AllocateMemory(pLogicalDevice->device, pAllocateInfo, pAllocator, pMemory);
        if (result != VK_SUCCESS)
            return result;

        VkDeviceSize total    = 0;
        bool         announce = false;
        {
            std::lock_guard<std::mutex> lock(g_memoryMutex);
            g_memorySizes[*pMemory] = pAllocateInfo->allocationSize;
            g_memoryBytes += pAllocateInfo->allocationSize;
            g_memoryPeakBytes = std::max(g_memoryPeakBytes, g_memoryBytes);
            total             = g_memoryBytes;

            if (g_memorySoftLimit != 0 && g_memoryBytes > g_memorySoftLimit && !g_softLimitWarned)
            {
                g_softLimitWarned = true;
                announce          = true;
            }
        }

        // The limit is a target, not a reservation: nothing is held back and nothing
        // is refused, so exceeding it is reported once and the frame carries on.
        if (announce)
            Logger::warn("effects are holding " + std::to_string(total / (1024 * 1024)) + " MiB of video memory, above the "
                         + std::to_string(g_memorySoftLimit / (1024 * 1024)) + " MiB asked for");

        return VK_SUCCESS;
    }

    void freeTrackedMemory(LogicalDevice* pLogicalDevice, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
    {
        if (memory != VK_NULL_HANDLE)
        {
            std::lock_guard<std::mutex> lock(g_memoryMutex);
            const auto                  it = g_memorySizes.find(memory);
            if (it != g_memorySizes.end())
            {
                g_memoryBytes -= std::min(g_memoryBytes, it->second);
                g_memorySizes.erase(it);
            }
        }

        pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, memory, pAllocator);
    }

    VkDeviceSize trackedMemoryBytes()
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        return g_memoryBytes;
    }

    VkDeviceSize trackedMemoryPeakBytes()
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        return g_memoryPeakBytes;
    }

    void setMemorySoftLimitBytes(VkDeviceSize bytes)
    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        g_memorySoftLimit = bytes;
        g_softLimitWarned = false;
    }
} // namespace vkBasalt
