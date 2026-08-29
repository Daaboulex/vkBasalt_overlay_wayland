#ifndef EFFECT_SUBMISSION_WAIT_HPP_INCLUDED
#define EFFECT_SUBMISSION_WAIT_HPP_INCLUDED

#include "vulkan_include.hpp"

namespace vkBasalt
{
    // The effect command buffer may begin with barriers or transfer work and
    // may contain compute-only ReShade passes. Waiting at fragment shader is
    // therefore too late to consume the application's presentation semaphore.
    inline constexpr VkPipelineStageFlags effectSubmissionApplicationWaitStage()
    {
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

#endif
