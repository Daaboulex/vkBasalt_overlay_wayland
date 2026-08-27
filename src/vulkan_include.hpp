#ifndef VULKAN_INCLUDE_HPP_INCLUDED
#define VULKAN_INCLUDE_HPP_INCLUDED

#define VK_NO_PROTOTYPES

#pragma GCC system_header
#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"

#include <string>
#include <stdexcept>

#include "logger.hpp"

namespace vkBasalt
{
    inline thread_local bool g_failFastOnAssertVulkan = false;

    class ScopedAssertVulkanFailure
    {
    public:
        ScopedAssertVulkanFailure()
            : previous(g_failFastOnAssertVulkan)
        {
            g_failFastOnAssertVulkan = true;
        }

        ~ScopedAssertVulkanFailure()
        {
            g_failFastOnAssertVulkan = previous;
        }

    private:
        bool previous;
    };

    inline void resetAssertVulkanFailureAfterLongJump()
    {
        g_failFastOnAssertVulkan = false;
    }

    inline void reportAssertVulkanFailure(VkResult result, const char* file, int line)
    {
        const std::string message = "ASSERT_VULKAN failed in " + std::string(file)
            + " : " + std::to_string(line) + "; " + std::to_string(result);
        Logger::err(message);
        if (g_failFastOnAssertVulkan)
            throw std::runtime_error(message);
    }

    template<typename DispatchableType, typename SuperDispatchableType>
    inline void initializeDispatchTable(DispatchableType dispatchableObject, SuperDispatchableType source)
    {
        *reinterpret_cast<void**>(dispatchableObject) = *reinterpret_cast<void**>(source);
    }
} // namespace vkBasalt

#ifndef ASSERT_VULKAN
#define ASSERT_VULKAN(val) \
    if ((val) != VK_SUCCESS) \
    { \
        vkBasalt::reportAssertVulkanFailure((val), __FILE__, __LINE__); \
    }
#endif

#endif // VULKAN_INCLUDE_HPP_INCLUDED
