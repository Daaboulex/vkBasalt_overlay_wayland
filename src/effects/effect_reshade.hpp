#ifndef EFFECT_RESHADE_HPP_INCLUDED
#define EFFECT_RESHADE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"

#include "effect.hpp"
#include "config.hpp"
#include "effect_config.hpp"
#include "effect_registry.hpp"
#include "reshade_uniforms.hpp"

#include "logical_device.hpp"

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    // The compile cache is keyed on these, so anything that wants a cache hit for an effect must
    // ask for them here rather than assemble its own copy.
    std::vector<std::pair<std::string, std::string>> reshadeCompileDefines(
        VkExtent2D extent, VkFormat unormFormat, VkColorSpaceKHR colorSpace,
        const std::vector<PreprocessorDefinition>& customDefs);

    class ReshadeEffect : public Effect
    {
    public:
        ReshadeEffect(LogicalDevice*       pLogicalDevice,
                      VkFormat             format,
                      VkExtent2D           imageExtent,
                      VkColorSpaceKHR      colorSpace,
                      std::vector<VkImage> inputImages,
                      std::vector<VkImage> outputImages,
                      EffectRegistry*      pEffectRegistry,
                      std::string          effectName,
                      std::string          effectPath = "",
                      std::vector<PreprocessorDefinition> customDefs = {});
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) override;
        void virtual updateEffect() override;
        void virtual useDepthImage(VkImageView depthImageView) override;
        std::vector<std::unique_ptr<EffectParam>> getParameters() const override;
        virtual ~ReshadeEffect();

    private:
        LogicalDevice*           pLogicalDevice;
        std::vector<VkImage>     inputImages;
        std::vector<VkImage>     outputImages;
        std::vector<VkImageView> inputImageViewsSRGB;
        std::vector<VkImageView> inputImageViewsUNORM;
        std::vector<VkImageView> outputImageViewsSRGB;
        std::vector<VkImageView> outputImageViewsUNORM;

        std::unordered_map<std::string, std::vector<VkImage>>     textureImages;
        std::unordered_map<std::string, std::vector<VkImageView>> textureImageViewsUNORM;
        std::unordered_map<std::string, std::vector<VkImageView>> textureImageViewsSRGB;
        std::unordered_map<std::string, std::vector<VkImageView>> renderImageViewsSRGB;
        std::unordered_map<std::string, std::vector<VkImageView>> renderImageViewsUNORM;

        std::unordered_map<std::string, VkFormat>   textureFormatsUNORM;
        std::unordered_map<std::string, VkFormat>   textureFormatsSRGB;
        std::unordered_map<std::string, uint32_t>   textureMipLevels;
        std::unordered_map<std::string, VkExtent3D> textureExtents;

        std::vector<VkDescriptorSet> inputDescriptorSets;
        std::vector<VkDescriptorSet> outputDescriptorSets;
        std::vector<VkDescriptorSet> backBufferDescriptorSets;

        std::vector<std::vector<VkFramebuffer>> framebuffers;

        VkDescriptorSetLayout                 uniformDescriptorSetLayout;
        VkDescriptorSetLayout                 imageSamplerDescriptorSetLayout;
        VkDescriptorSetLayout                 storageImageDescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet>          storageDescriptorSets;
        std::vector<std::vector<VkImageView>> storageImageViewVector;
        std::vector<std::string>              storageTextureNames;
        // This compiler assembles one module per entry point, each stripped of what the others
        // need, so a pipeline stage takes the module belonging to its own entry point.
        std::map<std::string, VkShaderModule>  shaderModules;
        VkShaderModule shaderModuleFor(const std::string& entryPoint) const
        {
            const auto it = shaderModules.find(entryPoint);
            return it != shaderModules.end() ? it->second : VK_NULL_HANDLE;
        }
        VkDescriptorPool                      descriptorPool;
        std::vector<VkRenderPass>             renderPasses;
        std::vector<std::vector<std::string>> renderTargets;
        std::vector<VkRenderPassBeginInfo>    renderPassBeginInfos;
        VkPipelineLayout                      pipelineLayout;
        std::vector<VkPipeline>               graphicsPipelines;
        std::vector<bool>                     switchSamplers;
        VkExtent2D                            imageExtent;
        std::vector<VkSampler>                samplers;
        EffectRegistry*                       pEffectRegistry;
        std::string                           effectName;
        std::string                           effectPath;  // Path to .fx file (may differ from effectName)
        std::vector<PreprocessorDefinition>   customPreprocessorDefs;  // User-defined macros
        reshadefx::effect_module                     module;

        std::vector<VkDeviceMemory>           textureMemory;

        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkFormat    inputOutputFormatUNORM;
        VkFormat    inputOutputFormatSRGB;
        VkFormat    stencilFormat;
        VkImage     stencilImage;
        VkImageView stencilImageView;
        // how often the shader writes to the reshade back buffer
        // we need to flip the "backbuffer" after each write if there is a next one
        int                      outputWrites = 0;
        std::vector<VkImage>     backBufferImages;
        std::vector<VkImageView> backBufferImageViewsUNORM;
        std::vector<VkImageView> backBufferImageViewsSRGB;
        VkBuffer                 stagingBuffer;
        VkDeviceMemory           stagingBufferMemory;
        uint32_t                 bufferSize;
        void*                    stagingBufferMapped = nullptr;  // Persistent map (HOST_COHERENT)
        VkDescriptorSet          bufferDescriptorSet;

        std::vector<std::shared_ptr<ReshadeUniform>> uniforms;

        void          createReshadeModule();
        VkFormat      convertReshadeFormat(reshadefx::texture_format texFormat);
        VkCompareOp   convertReshadeCompareOp(reshadefx::stencil_func compareOp);
        VkStencilOp   convertReshadeStencilOp(reshadefx::stencil_op stencilOp);
        VkBlendOp     convertReshadeBlendOp(reshadefx::blend_op blendOp);
        VkBlendFactor convertReshadeBlendFactor(reshadefx::blend_factor blendFactor);
    };
} // namespace vkBasalt

#endif // EFFECT_RESHADE_HPP_INCLUDED
