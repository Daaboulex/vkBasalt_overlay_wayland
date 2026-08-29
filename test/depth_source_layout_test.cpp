#include "depth_source_layout.hpp"

#include <cassert>

using namespace vkBasalt;

int main()
{
    assert(parseDepthSourceLayout("")
           == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    assert(parseDepthSourceLayout("attachment")
           == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    assert(parseDepthSourceLayout("attachment-optimal")
           == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    assert(parseDepthSourceLayout("general") == VK_IMAGE_LAYOUT_GENERAL);
    assert(!parseDepthSourceLayout("undefined"));

    assert(depthSourceProducerStages(VK_IMAGE_LAYOUT_GENERAL)
           == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(depthSourceProducerAccess(VK_IMAGE_LAYOUT_GENERAL)
           == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
    assert(depthSourceProducerStages(
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
           == (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
               | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT));
    assert(depthSourceProducerAccess(
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
           == (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
               | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
    assert(depthShaderConsumerStages()
           == (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
               | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
}
