#ifndef EFFECT_RCAS_HPP_INCLUDED
#define EFFECT_RCAS_HPP_INCLUDED
#include <vector>
#include <string>
#include <memory>

#include "vulkan_include.hpp"

#include "../effect_simple.hpp"
#include "config.hpp"

namespace vkBasalt
{
    class RcasEffect : public SimpleEffect
    {
    public:
        RcasEffect(LogicalDevice*       pLogicalDevice,
                   VkFormat             format,
                   VkExtent2D           imageExtent,
                   std::vector<VkImage> inputImages,
                   std::vector<VkImage> outputImages,
                   Config*              pConfig);
        ~RcasEffect();
    };
} // namespace vkBasalt

#endif // EFFECT_RCAS_HPP_INCLUDED
