#include "effect_submission_wait.hpp"

using namespace vkBasalt;

int main()
{
    const VkPipelineStageFlags stage = effectSubmissionApplicationWaitStage();
    if (stage != VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
        return 1;
    if (stage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        return 2;
    return 0;
}
