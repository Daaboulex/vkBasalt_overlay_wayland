#include "effect_rcas.hpp"

#include <cstring>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"

#include "shader_sources.hpp"

namespace vkBasalt
{
    RcasEffect::RcasEffect(LogicalDevice*       pLogicalDevice,
                           VkFormat             format,
                           VkExtent2D           imageExtent,
                           std::vector<VkImage> inputImages,
                           std::vector<VkImage> outputImages,
                           Config*              pConfig)
    {
        struct
        {
            float   sharpness;
            int32_t denoise;
        } spec;

        spec.sharpness = pConfig->getOption<float>("rcasSharpness", 0.4f);
        spec.denoise   = pConfig->getOption<bool>("rcasDenoise", true) ? 1 : 0;

        vertexCode   = full_screen_triangle_vert;
        fragmentCode = rcas_frag;

        static VkSpecializationMapEntry mapEntries[2];
        mapEntries[0].constantID = 0;
        mapEntries[0].offset     = offsetof(decltype(spec), sharpness);
        mapEntries[0].size       = sizeof(float);
        mapEntries[1].constantID = 1;
        mapEntries[1].offset     = offsetof(decltype(spec), denoise);
        mapEntries[1].size       = sizeof(int32_t);

        VkSpecializationInfo fragmentSpecializationInfo;
        fragmentSpecializationInfo.mapEntryCount = 2;
        fragmentSpecializationInfo.pMapEntries   = mapEntries;
        fragmentSpecializationInfo.dataSize      = sizeof(spec);
        fragmentSpecializationInfo.pData         = &spec;

        pVertexSpecInfo   = nullptr;
        pFragmentSpecInfo = &fragmentSpecializationInfo;

        init(pLogicalDevice, format, imageExtent, inputImages, outputImages, pConfig);
    }
    RcasEffect::~RcasEffect()
    {
    }
} // namespace vkBasalt
