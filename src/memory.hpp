#ifndef MEMORY_HPP_INCLUDED
#define MEMORY_HPP_INCLUDED
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
    uint32_t findMemoryTypeIndex(LogicalDevice* pLogicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Every allocation the layer makes goes through these, so what it is holding is
    // known without the caller having to remember a size.
    VkResult allocateTrackedMemory(LogicalDevice*               pLogicalDevice,
                                   const VkMemoryAllocateInfo*  pAllocateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkDeviceMemory*              pMemory);

    void freeTrackedMemory(LogicalDevice*               pLogicalDevice,
                           VkDeviceMemory               memory,
                           const VkAllocationCallbacks* pAllocator);

    VkDeviceSize trackedMemoryBytes();
    VkDeviceSize trackedMemoryPeakBytes();
    void         setMemorySoftLimitBytes(VkDeviceSize bytes);

    // Public regression harness: fail one allocation deterministically without
    // trying to exhaust the test machine's real VRAM.
    void failNextTrackedAllocationForTest();
    void failTrackedAllocationForTest(uint32_t ordinal);
    void clearTrackedAllocationFailureForTest();
}

#endif // MEMORY_HPP_INCLUDED
