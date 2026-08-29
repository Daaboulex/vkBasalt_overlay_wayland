#include "memory.hpp"
#include "effect_reshade.hpp"
#include "reshade_pass_utils.hpp"
#include "reshade_texture_utils.hpp"
#include "crash_guard.hpp"

#include <cstring>
#include <climits>
#include <cstdlib>
#include <cassert>

#include <set>
#include <utility>
#include <variant>
#include <algorithm>
#include <filesystem>

#include "image_view.hpp"
#include "descriptor_set.hpp"
#include "buffer.hpp"
#include "renderpass.hpp"
#include "graphics_pipeline.hpp"
#include "framebuffer.hpp"
#include "shader.hpp"
#include "sampler.hpp"
#include "image.hpp"
#include "lut_cube.hpp"
#include "format.hpp"
#include "config_serializer.hpp"
#include "shader_cache.hpp"

#include "util.hpp"

#include "stb_image.h"
#include "stb_image_dds.h"
#include "stb_image_resize.h"

namespace vkBasalt
{
    std::vector<std::pair<std::string, std::string>> reshadeCompileDefines(
        VkExtent2D extent, VkFormat unormFormat, VkColorSpaceKHR colorSpace,
        const std::vector<PreprocessorDefinition>& customDefs)
    {
        // ReShade reports the swapchain's colour space to the shader. Shaders branch on it, so
        // leaving it undefined silently compiles the wrong path: it evaluates to 0, which is their
        // CSP_UNKNOWN. The numbers are ReShade's, not ours.
        const char* colorSpaceValue = "0";
        switch (colorSpace)
        {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:        colorSpaceValue = "1"; break;
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:  colorSpaceValue = "2"; break;
            case VK_COLOR_SPACE_HDR10_ST2084_EXT:          colorSpaceValue = "3"; break;
            case VK_COLOR_SPACE_HDR10_HLG_EXT:             colorSpaceValue = "4"; break;
            default: break;
        }

        std::vector<std::pair<std::string, std::string>> defines = {
            {"BUFFER_WIDTH", std::to_string(extent.width)},
            {"BUFFER_HEIGHT", std::to_string(extent.height)},
            {"BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)"},
            {"BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)"},
            {"BUFFER_COLOR_DEPTH", (unormFormat == VK_FORMAT_A2R10G10B10_UNORM_PACK32) ? "10" : "8"},
            {"BUFFER_COLOR_BIT_DEPTH", "BUFFER_COLOR_DEPTH"},
            {"BUFFER_COLOR_SPACE", colorSpaceValue},
        };

        for (const auto& def : customDefs)
            defines.push_back({def.name, def.value});

        return defines;
    }

    SharedReshadeTexturePool::SharedReshadeTexturePool(LogicalDevice* pLogicalDevice)
        : pLogicalDevice(pLogicalDevice)
    {
    }

    SharedReshadeTexturePool::~SharedReshadeTexturePool()
    {
        for (auto& [_, texture] : textures)
        {
            if (texture.image != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, texture.image, nullptr);
            if (texture.memory != VK_NULL_HANDLE)
                freeTrackedMemory(pLogicalDevice, texture.memory, nullptr);
        }
    }

    SharedReshadeTexture* SharedReshadeTexturePool::acquire(const std::string& uniqueName,
                                                            VkExtent3D        extent,
                                                            VkFormat          format,
                                                            VkImageUsageFlags usage,
                                                            uint32_t          mipLevels,
                                                            bool&             created)
    {
        created = false;

        const auto existing = textures.find(uniqueName);
        if (existing != textures.end())
        {
            SharedReshadeTexture& texture = existing->second;
            if (texture.format != format
                || texture.extent.width != extent.width
                || texture.extent.height != extent.height
                || texture.extent.depth != extent.depth
                || texture.mipLevels != mipLevels)
            {
                throw std::runtime_error("incompatible shared effect texture declaration: " + uniqueName);
            }
            // The image is already live and other effects hold views of it, so it cannot gain a
            // usage flag now. Sharing is an optimisation; the effect gets its own image instead.
            if ((usage & ~texture.usage) != 0)
            {
                Logger::warn("effect texture " + uniqueName
                             + " needs usage the shared image was not created with; using a private copy");
                return nullptr;
            }
            return &texture;
        }

        SharedReshadeTexture texture;
        std::vector<VkImage> images = createImages(
            pLogicalDevice, 1, extent, format, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.memory, mipLevels);
        texture.image = images[0];
        texture.format = format;
        texture.extent = extent;
        texture.mipLevels = mipLevels;
        texture.usage = usage;

        created = true;
        Logger::debug("created shared effect texture " + uniqueName);
        return &textures.emplace(uniqueName, texture).first->second;
    }

    ReshadeEffect::ReshadeEffect(LogicalDevice*       pLogicalDevice,
                                 VkFormat             format,
                                 VkExtent2D           imageExtent,
                                 VkColorSpaceKHR      colorSpace,
                                 std::vector<VkImage> inputImages,
                                 std::vector<VkImage> outputImages,
                                 EffectRegistry*      pEffectRegistry,
                                 std::shared_ptr<SharedReshadeTexturePool> texturePool,
                                 std::string          effectName,
                                 std::string          effectPath,
                                 std::vector<PreprocessorDefinition> customDefs)
        : ReshadeEffect(pLogicalDevice)
    {
        Logger::debug("in creating ReshadeEffect");

        this->imageExtent           = imageExtent;
        this->colorSpace            = colorSpace;
        this->inputImages           = inputImages;
        this->outputImages          = outputImages;
        this->pEffectRegistry       = pEffectRegistry;
        this->sharedTexturePool     = std::move(texturePool);
        this->effectName            = effectName;
        this->effectPath            = effectPath;
        this->customPreprocessorDefs = customDefs;
        inputOutputFormatUNORM = convertToUNORM(format);
        inputOutputFormatSRGB  = convertToSRGB(format);

        inputImageViewsSRGB  = createImageViews(pLogicalDevice, inputOutputFormatSRGB, inputImages);
        inputImageViewsUNORM = createImageViews(pLogicalDevice, inputOutputFormatUNORM, inputImages);
        Logger::debug("created input ImageViews");
        outputImageViewsSRGB  = createImageViews(pLogicalDevice, inputOutputFormatSRGB, outputImages);
        outputImageViewsUNORM = createImageViews(pLogicalDevice, inputOutputFormatUNORM, outputImages);
        Logger::debug("created ImageViews");

        // The compiler's signal recovery belongs inside a fully formed object: the delegating
        // constructor has already completed, so throwing here runs this object's destructor and
        // releases the views acquired above.
        installCrashHandlers();
        if (sigsetjmp(crashJmpBuf, 1) != 0)
        {
            crashJmpActive = 0;
            const std::string signalName =
                crashCaughtSignal == SIGFPE ? "SIGFPE" : "SIGABRT";
            throw std::runtime_error(
                signalName + " during ReShade compilation: " + effectName);
        }
        crashJmpActive = 1;
        try
        {
            createReshadeModule();
        }
        catch (...)
        {
            crashJmpActive = 0;
            throw;
        }
        crashJmpActive = 0;

        for (const auto& storage : module.storages)
        {
            if (storage.level != 0)
            {
                Logger::err(effectName + ": storage '" + storage.unique_name + "' binds mip level " + std::to_string(storage.level)
                            + ", which this build does not bind");
                throw std::runtime_error("unsupported storage mip level: " + effectName);
            }
            if (std::find(storageTextureNames.begin(), storageTextureNames.end(), storage.texture_name) == storageTextureNames.end())
            {
                storageTextureNames.push_back(storage.texture_name);
            }
        }

        if (!module.storages.empty() && !pLogicalDevice->supportsStorageImageWithoutFormat)
        {
            Logger::err(effectName + ": compute passes need shaderStorageImageReadWithoutFormat and"
                        " shaderStorageImageWriteWithoutFormat, which this device does not support");
            throw std::runtime_error("device lacks format-less storage image access: " + effectName);
        }

        const auto usedAsStorage = [this](const std::string& textureName) -> VkImageUsageFlags {
            return std::find(storageTextureNames.begin(), storageTextureNames.end(), textureName) != storageTextureNames.end()
                       ? VK_IMAGE_USAGE_STORAGE_BIT
                       : 0;
        };

        enumerateReshadeUniforms(module);

        uniforms = createReshadeUniforms(
            module, pEffectRegistry, effectName);

        bufferSize = module.total_uniform_size;
        if (bufferSize)
        {
            uniformBuffers.assign(inputImages.size(), VK_NULL_HANDLE);
            uniformBufferMemory.assign(inputImages.size(), VK_NULL_HANDLE);
            uniformBuffersMapped.assign(inputImages.size(), nullptr);
            for (size_t i = 0; i < inputImages.size(); ++i)
            {
                createBuffer(
                    pLogicalDevice, bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uniformBuffers[i], uniformBufferMemory[i]);
                const VkResult mapResult = pLogicalDevice->vkd.MapMemory(
                    pLogicalDevice->device, uniformBufferMemory[i], 0,
                    bufferSize, 0, &uniformBuffersMapped[i]);
                if (mapResult != VK_SUCCESS)
                {
                    Logger::err("MapMemory failed for effect " + effectName
                                + " image " + std::to_string(i) + ": "
                                + std::to_string(mapResult));
                    uniformBuffersMapped[i] = nullptr;
                    throw std::runtime_error(
                        "vkMapMemory failed for effect " + effectName + ": "
                        + std::to_string(mapResult));
                }

                std::memset(uniformBuffersMapped[i], 0, bufferSize);
                for (auto& uniform : uniforms)
                {
                    if (dynamic_cast<ParameterUniform*>(uniform.get()) != nullptr)
                        uniform->update(uniformBuffersMapped[i]);
                }
            }
        }

        stencilFormat = getStencilFormat(pLogicalDevice);
        Logger::debug("Stencil Format: " + std::to_string(stencilFormat));
        textureMemory.push_back(VK_NULL_HANDLE);
        stencilImage = createImages(pLogicalDevice,
                                    1,
                                    {imageExtent.width, imageExtent.height, 1},
                                    stencilFormat,
                                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    textureMemory.back())[0];

        stencilImageView = createImageViews(
            pLogicalDevice, stencilFormat, {stencilImage}, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)[0];

        std::vector<std::vector<VkImageView>> imageViewVector;

        ShaderManagerConfig cachedShaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();

        for (size_t i = 0; i < module.textures.size(); i++)
        {
            textureMipLevels[module.textures[i].unique_name] = module.textures[i].levels;
            textureExtents[module.textures[i].unique_name]   = {module.textures[i].width, module.textures[i].height, 1};
            if (module.textures[i].semantic == "COLOR")
            {
                textureImageViewsUNORM[module.textures[i].unique_name] = inputImageViewsUNORM;
                renderImageViewsUNORM[module.textures[i].unique_name]  = inputImageViewsUNORM;

                textureImageViewsSRGB[module.textures[i].unique_name] = inputImageViewsSRGB;
                renderImageViewsSRGB[module.textures[i].unique_name]  = inputImageViewsSRGB;

                textureFormatsUNORM[module.textures[i].unique_name] = inputOutputFormatUNORM;
                textureFormatsSRGB[module.textures[i].unique_name]  = inputOutputFormatSRGB;
                continue;
            }
            if (module.textures[i].semantic == "DEPTH")
            {
                textureImageViewsUNORM[module.textures[i].unique_name] = inputImageViewsUNORM;
                renderImageViewsUNORM[module.textures[i].unique_name]  = inputImageViewsUNORM;

                textureImageViewsSRGB[module.textures[i].unique_name] = inputImageViewsSRGB;
                renderImageViewsSRGB[module.textures[i].unique_name]  = inputImageViewsSRGB;

                textureFormatsUNORM[module.textures[i].unique_name] = inputOutputFormatUNORM;
                textureFormatsSRGB[module.textures[i].unique_name]  = inputOutputFormatSRGB;
                continue;
            }
            VkExtent3D textureExtent = {module.textures[i].width, module.textures[i].height, module.textures[i].depth};
            const VkImageViewType textureViewType = textureExtent.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
            // TODO handle mip map levels correctly
            // TODO handle pooled textures better
            if (const auto source = std::find_if(
                    module.textures[i].annotations.begin(), module.textures[i].annotations.end(), [](const auto& a) { return a.name == "source"; });
                source == module.textures[i].annotations.end())
            {
                const VkFormat textureFormat = convertReshadeFormat(module.textures[i].format);
                const VkImageUsageFlags textureUsage =
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | usedAsStorage(module.textures[i].unique_name);
                std::vector<VkImage> images;
                bool initializeImages = true;

                SharedReshadeTexture* shared = nullptr;
                if (isGeneratedSharedReshadeTexture(module.textures[i]))
                {
                    bool created = false;
                    shared = sharedTexturePool->acquire(
                        module.textures[i].unique_name, textureExtent, textureFormat,
                        textureUsage, module.textures[i].levels, created);
                    if (shared != nullptr)
                    {
                        images = {shared->image};
                        initializeImages = created;
                        sharedTextureNames.insert(module.textures[i].unique_name);
                    }
                }
                if (shared == nullptr)
                {
                    textureMemory.push_back(VK_NULL_HANDLE);
                    images = createImages(pLogicalDevice,
                                          1,
                                          textureExtent,
                                          textureFormat,
                                          textureUsage,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                          textureMemory.back(),
                                          module.textures[i].levels);
                }

                textureImages[module.textures[i].unique_name] = images;
                std::vector<VkImageView> imageViewsUNORM =
                    std::vector<VkImageView>(inputImages.size(),
                                             createImageViews(pLogicalDevice,
                                                              convertToUNORM(convertReshadeFormat(module.textures[i].format)),
                                                              images,
                                                              textureViewType,
                                                              VK_IMAGE_ASPECT_COLOR_BIT,
                                                              module.textures[i].levels)[0]);

                std::vector<VkImageView> imageViewsSRGB =
                    std::vector<VkImageView>(inputImages.size(),
                                             createImageViews(pLogicalDevice,
                                                              convertToSRGB(convertReshadeFormat(module.textures[i].format)),
                                                              images,
                                                              textureViewType,
                                                              VK_IMAGE_ASPECT_COLOR_BIT,
                                                              module.textures[i].levels)[0]);

                textureImageViewsUNORM[module.textures[i].unique_name] = imageViewsUNORM;
                textureImageViewsSRGB[module.textures[i].unique_name]  = imageViewsSRGB;

                if (module.textures[i].levels > 1)
                {

                    renderImageViewsUNORM[module.textures[i].unique_name] = std::vector<VkImageView>(
                        inputImages.size(),
                        createImageViews(pLogicalDevice, convertToUNORM(convertReshadeFormat(module.textures[i].format)), images)[0]);

                    renderImageViewsSRGB[module.textures[i].unique_name] = std::vector<VkImageView>(
                        inputImages.size(),
                        createImageViews(pLogicalDevice, convertToSRGB(convertReshadeFormat(module.textures[i].format)), images)[0]);
                }
                else
                {
                    renderImageViewsUNORM[module.textures[i].unique_name] = imageViewsUNORM;
                    renderImageViewsSRGB[module.textures[i].unique_name]  = imageViewsSRGB;
                }

                textureFormatsUNORM[module.textures[i].unique_name] = convertToUNORM(convertReshadeFormat(module.textures[i].format));
                textureFormatsSRGB[module.textures[i].unique_name]  = convertToSRGB(convertReshadeFormat(module.textures[i].format));
                if (initializeImages)
                    clearAndReadyImages(pLogicalDevice, images, module.textures[i].levels);
                continue;
            }
            else
            {
                textureMemory.push_back(VK_NULL_HANDLE);
                std::vector<VkImage> images =
                    createImages(pLogicalDevice,
                                 1,
                                 textureExtent,
                                 convertReshadeFormat(module.textures[i].format), // TODO search for format and save it
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                     | usedAsStorage(module.textures[i].unique_name),
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 textureMemory.back(),
                                 module.textures[i].levels);

                textureImages[module.textures[i].unique_name] = images;

                // A source texture that fails to load below must sample as black,
                // not as whatever this memory last held.
                clearAndReadyImages(pLogicalDevice, images, module.textures[i].levels);

                std::vector<VkImageView> imageViews = createImageViews(pLogicalDevice,
                                                                       convertToUNORM(convertReshadeFormat(module.textures[i].format)),
                                                                       images,
                                                                       textureViewType,
                                                                       VK_IMAGE_ASPECT_COLOR_BIT,
                                                                       module.textures[i].levels);

                std::vector<VkImageView> imageViewsUNORM = std::vector<VkImageView>(inputImages.size(), imageViews[0]);

                imageViews = createImageViews(pLogicalDevice,
                                              convertToSRGB(convertReshadeFormat(module.textures[i].format)),
                                              images,
                                              textureViewType,
                                              VK_IMAGE_ASPECT_COLOR_BIT,
                                              module.textures[i].levels);

                std::vector<VkImageView> imageViewsSRGB = std::vector<VkImageView>(inputImages.size(), imageViews[0]);

                textureImageViewsUNORM[module.textures[i].unique_name] = imageViewsUNORM;
                textureImageViewsSRGB[module.textures[i].unique_name]  = imageViewsSRGB;

                renderImageViewsUNORM[module.textures[i].unique_name] = imageViewsUNORM;
                renderImageViewsSRGB[module.textures[i].unique_name]  = imageViewsSRGB;

                textureFormatsUNORM[module.textures[i].unique_name] = convertToUNORM(convertReshadeFormat(module.textures[i].format));
                textureFormatsSRGB[module.textures[i].unique_name]  = convertToSRGB(convertReshadeFormat(module.textures[i].format));

                int desiredChannels;
                switch (textureFormatsUNORM[module.textures[i].unique_name])
                {
                    case VK_FORMAT_R8_UNORM: desiredChannels = STBI_grey; break;
                    case VK_FORMAT_R8G8_UNORM:
                        desiredChannels = STBI_rgb_alpha; // TODO why doesn't STBI_grey_alpha work?
                        break;
                    case VK_FORMAT_R8G8B8A8_UNORM: desiredChannels = STBI_rgb_alpha; break;
                    case VK_FORMAT_R8G8B8A8_SRGB: desiredChannels = STBI_rgb_alpha; break;
                    default:
                        Logger::err("unsupported texture upload format" + std::to_string(textureFormatsUNORM[module.textures[i].unique_name]));
                        desiredChannels = 4;
                        break;
                }

                std::string textureName = source->value.string_data;
                std::string filePath;
                FILE* file = nullptr;

                for (const auto& texPath : cachedShaderMgrConfig.discoveredTexturePaths)
                {
                    filePath = texPath + "/" + textureName;
                    file = fopen(filePath.c_str(), "rb");
                    if (file != nullptr)
                        break;
                }

                stbi_uc*             pixels;
                std::vector<stbi_uc> resizedPixels;
                uint32_t             size;
                int                  width;
                int                  height;

                size = textureExtent.width * textureExtent.height * desiredChannels;

                if (file == nullptr)
                {
                    Logger::err("couldn't open texture: " + textureName + " (searched " +
                        std::to_string(cachedShaderMgrConfig.discoveredTexturePaths.size()) + " directories)");
                    continue;
                }

                if (textureName.size() > 5 && textureName.compare(textureName.size() - 5, 5, ".cube") == 0)
                {
                    fclose(file);

                    LutCube lut(filePath);
                    const uint32_t side = static_cast<uint32_t>(lut.size);

                    if (lut.size <= 0 || lut.colorCube.size() != static_cast<size_t>(side) * side * side * 4)
                    {
                        Logger::err("failed to parse cube LUT: " + textureName + " from " + filePath);
                        continue;
                    }

                    if (side != textureExtent.width || side != textureExtent.height || side != textureExtent.depth)
                    {
                        Logger::err("cube LUT " + textureName + " is " + std::to_string(side) + " per side, but the shader declares "
                            + std::to_string(textureExtent.width) + "x" + std::to_string(textureExtent.height) + "x"
                            + std::to_string(textureExtent.depth));
                        continue;
                    }

                    if (textureFormatsUNORM[module.textures[i].unique_name] != VK_FORMAT_R8G8B8A8_UNORM)
                    {
                        Logger::err("cube LUT " + textureName + " needs an RGBA8 texture, but the shader declares another format");
                        continue;
                    }

                    uploadToImage(pLogicalDevice,
                                  images[0],
                                  textureExtent,
                                  static_cast<uint32_t>(lut.colorCube.size()),
                                  lut.colorCube.data(),
                                  module.textures[i].levels);
                    continue;
                }

                if (stbi_dds_test_file(file))
                {
                    int channels;
                    pixels = stbi_dds_load_from_file(file, &width, &height, &channels, desiredChannels);
                }
                else
                {
                    int channels;
                    pixels = stbi_load_from_file(file, &width, &height, &channels, desiredChannels);
                }
                fclose(file);

                if (pixels == nullptr)
                {
                    Logger::err("failed to decode texture: " + textureName + " from " + filePath);
                    continue;
                }

                if (textureFormatsUNORM[module.textures[i].unique_name] == VK_FORMAT_R8G8_UNORM)
                {
                    uint32_t pos = 0;
                    for (uint32_t j = 0; j < size; j += 4)
                    {
                        pixels[pos] = pixels[j];
                        pos++;
                        pixels[pos] = pixels[j + 1];
                        pos++;
                    }
                    size /= 2;
                    desiredChannels /= 2;
                }

                if (static_cast<uint32_t>(width) != textureExtent.width || static_cast<uint32_t>(height) != textureExtent.height)
                {
                    resizedPixels.resize(size);
                    stbir_resize_uint8(pixels, width, height, 0, resizedPixels.data(), textureExtent.width, textureExtent.height, 0, desiredChannels);
                }

                uploadToImage(
                    pLogicalDevice, images[0], textureExtent, size, resizedPixels.size() ? resizedPixels.data() : pixels, module.textures[i].levels);
                stbi_image_free(pixels);
            }
        }

        for (size_t i = 0; i < module.samplers.size(); i++)
        {
            reshadefx::sampler info = module.samplers[i];

            VkSampler sampler = createReshadeSampler(pLogicalDevice, info);

            samplers.push_back(sampler);

            imageViewVector.push_back(info.srgb ? textureImageViewsSRGB[info.texture_name] : textureImageViewsUNORM[info.texture_name]);
        }

        for (const auto& storage : module.storages)
        {
            const auto imagesIt = textureImages.find(storage.texture_name);
            const auto formatIt = textureFormatsUNORM.find(storage.texture_name);
            if (imagesIt == textureImages.end() || imagesIt->second.empty() || formatIt == textureFormatsUNORM.end())
            {
                Logger::err(effectName + ": storage '" + storage.unique_name + "' binds texture '" + storage.texture_name
                            + "', which has no image backing it");
                throw std::runtime_error("storage bound to a texture with no image: " + effectName);
            }

            std::vector<VkImageView> views = createImageViews(pLogicalDevice, formatIt->second, imagesIt->second);
            storageImageViewVector.push_back(std::vector<VkImageView>(inputImages.size(), views[0]));
        }

        imageSamplerDescriptorSetLayout = createImageSamplerDescriptorSetLayout(pLogicalDevice, module.samplers.size());
        uniformDescriptorSetLayout      = createUniformBufferDescriptorSetLayout(pLogicalDevice);
        if (!module.storages.empty())
        {
            storageImageDescriptorSetLayout = createStorageImageDescriptorSetLayout(pLogicalDevice, module.storages.size());
        }
        Logger::debug("created descriptorSetLayouts");

        VkDescriptorPoolSize imagePoolSize;
        imagePoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imagePoolSize.descriptorCount = inputImages.size() * module.samplers.size() * 3;

        VkDescriptorPoolSize bufferPoolSize;
        bufferPoolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferPoolSize.descriptorCount = bufferSize
            ? static_cast<uint32_t>(inputImages.size()) : 0;

        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.reserve(3);
        poolSizes.push_back(imagePoolSize);
        poolSizes.push_back(bufferPoolSize);

        if (!module.storages.empty())
        {
            VkDescriptorPoolSize storagePoolSize;
            storagePoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storagePoolSize.descriptorCount = inputImages.size() * module.storages.size();
            poolSizes.push_back(storagePoolSize);
        }

        descriptorPool = createDescriptorPool(pLogicalDevice, poolSizes);
        Logger::debug("created descriptorPool");

        if (!module.storages.empty())
        {
            storageDescriptorSets = allocateAndWriteStorageImageDescriptorSets(
                pLogicalDevice, descriptorPool, storageImageDescriptorSetLayout, storageImageViewVector);
        }

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        descriptorSetLayouts.reserve(3);
        descriptorSetLayouts.push_back(uniformDescriptorSetLayout);
        descriptorSetLayouts.push_back(imageSamplerDescriptorSetLayout);
        if (storageImageDescriptorSetLayout != VK_NULL_HANDLE)
        {
            descriptorSetLayouts.push_back(storageImageDescriptorSetLayout);
        }

        pipelineLayout = createGraphicsPipelineLayout(pLogicalDevice, descriptorSetLayouts);

        Logger::debug("created Pipeline layout");

        if (bufferSize)
        {
            bufferDescriptorSets = allocateAndWriteBufferDescriptorSets(
                pLogicalDevice, descriptorPool, uniformDescriptorSetLayout,
                uniformBuffers);
        }

        inputDescriptorSets =
            allocateAndWriteImageSamplerDescriptorSets(pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout, samplers, imageViewVector);

        outputWrites = static_cast<int>(countReshadeBackBufferWrites(module.techniques[0]));
        Logger::debug("output writes: " + std::to_string(outputWrites));

        if (outputWrites > 1)
        {
            textureMemory.push_back(VK_NULL_HANDLE);
            backBufferImages = createImages(pLogicalDevice,
                                            inputImages.size(),
                                            {imageExtent.width, imageExtent.height, 1},
                                            format, // TODO search for format and save it
                                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            textureMemory.back());

            backBufferImageViewsSRGB  = createImageViews(pLogicalDevice, inputOutputFormatSRGB, backBufferImages);
            backBufferImageViewsUNORM = createImageViews(pLogicalDevice, inputOutputFormatUNORM, backBufferImages);

            std::replace(imageViewVector.begin(), imageViewVector.end(), inputImageViewsSRGB, backBufferImageViewsSRGB);
            std::replace(imageViewVector.begin(), imageViewVector.end(), inputImageViewsUNORM, backBufferImageViewsUNORM);

            backBufferDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(
                pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout, samplers, imageViewVector);
        }
        if (outputWrites > 2)
        {
            std::replace(imageViewVector.begin(), imageViewVector.end(), backBufferImageViewsSRGB, outputImageViewsSRGB);
            std::replace(imageViewVector.begin(), imageViewVector.end(), backBufferImageViewsUNORM, outputImageViewsUNORM);
            outputDescriptorSets = allocateAndWriteImageSamplerDescriptorSets(
                pLogicalDevice, descriptorPool, imageSamplerDescriptorSetLayout, samplers, imageViewVector);
        }

        Logger::debug("after writing ImageSamplerDescriptorSets");

        bool firstTimeStencilAccess = true; // Used to clear the sttencil attachment on the first time

        std::vector<VkSpecializationMapEntry> specMapEntrys;
        std::vector<char>                     specData;

        std::string prevSpecName;
        int vectorComponentIndex = 0;

        for (uint32_t specId = 0, offset = 0; auto &opt : module.spec_constants)
        {
            if (!opt.name.empty())
            {
                if (opt.name == prevSpecName)
                {
                    vectorComponentIndex++;
                }
                else
                {
                    vectorComponentIndex = 0;
                    prevSpecName = opt.name;
                }

                EffectParam* param = pEffectRegistry->getParameter(effectName, opt.name);
                if (!param)
                {
                    specId++;
                    continue;
                }

                std::variant<int32_t, uint32_t, float> convertedValue;
                offset = static_cast<uint32_t>(specData.size());

                switch (opt.type.base)
                {
                    case reshadefx::type::t_bool:
                        if (auto* bp = dynamic_cast<BoolParam*>(param))
                        {
                            convertedValue = (int32_t)(bp->value ? 1 : 0);
                            specData.resize(offset + sizeof(VkBool32));
                            std::memcpy(specData.data() + offset, &convertedValue, sizeof(VkBool32));
                            specMapEntrys.push_back({specId, offset, sizeof(VkBool32)});
                        }
                        break;
                    case reshadefx::type::t_int:
                        if (auto* ivp = dynamic_cast<IntVecParam*>(param))
                        {
                            if (vectorComponentIndex < static_cast<int>(ivp->componentCount))
                            {
                                convertedValue = ivp->value[vectorComponentIndex];
                                specData.resize(offset + sizeof(int32_t));
                                std::memcpy(specData.data() + offset, &convertedValue, sizeof(int32_t));
                                specMapEntrys.push_back({specId, offset, sizeof(int32_t)});
                            }
                        }
                        else if (auto* ip = dynamic_cast<IntParam*>(param))
                        {
                            convertedValue = ip->value;
                            specData.resize(offset + sizeof(int32_t));
                            std::memcpy(specData.data() + offset, &convertedValue, sizeof(int32_t));
                            specMapEntrys.push_back({specId, offset, sizeof(int32_t)});
                        }
                        break;
                    case reshadefx::type::t_uint:
                        if (auto* uvp = dynamic_cast<UintVecParam*>(param))
                        {
                            if (vectorComponentIndex < static_cast<int>(uvp->componentCount))
                            {
                                convertedValue = uvp->value[vectorComponentIndex];
                                specData.resize(offset + sizeof(uint32_t));
                                std::memcpy(specData.data() + offset, &convertedValue, sizeof(uint32_t));
                                specMapEntrys.push_back({specId, offset, sizeof(uint32_t)});
                            }
                        }
                        else if (auto* up = dynamic_cast<UintParam*>(param))
                        {
                            convertedValue = up->value;
                            specData.resize(offset + sizeof(uint32_t));
                            std::memcpy(specData.data() + offset, &convertedValue, sizeof(uint32_t));
                            specMapEntrys.push_back({specId, offset, sizeof(uint32_t)});
                        }
                        else if (auto* ip = dynamic_cast<IntParam*>(param))
                        {
                            // Fallback: some shaders use int for uint
                            convertedValue = static_cast<uint32_t>(ip->value);
                            specData.resize(offset + sizeof(uint32_t));
                            std::memcpy(specData.data() + offset, &convertedValue, sizeof(uint32_t));
                            specMapEntrys.push_back({specId, offset, sizeof(uint32_t)});
                        }
                        break;
                    case reshadefx::type::t_float:
                        if (auto* fvp = dynamic_cast<FloatVecParam*>(param))
                        {
                            if (vectorComponentIndex < static_cast<int>(fvp->componentCount))
                            {
                                convertedValue = fvp->value[vectorComponentIndex];
                                specData.resize(offset + sizeof(float));
                                std::memcpy(specData.data() + offset, &convertedValue, sizeof(float));
                                specMapEntrys.push_back({specId, offset, sizeof(float)});
                            }
                        }
                        else if (auto* fp = dynamic_cast<FloatParam*>(param))
                        {
                            convertedValue = fp->value;
                            specData.resize(offset + sizeof(float));
                            std::memcpy(specData.data() + offset, &convertedValue, sizeof(float));
                            specMapEntrys.push_back({specId, offset, sizeof(float)});
                        }
                        break;
                    default:
                        break;
                }
            }
            specId++;
        }

        VkSpecializationInfo specializationInfo;
        if (specMapEntrys.size() > 0)
        {
            specializationInfo = {.mapEntryCount = static_cast<uint32_t>(specMapEntrys.size()),
                                  .pMapEntries   = specMapEntrys.data(),
                                  .dataSize      = specData.size(),
                                  .pData         = specData.data()};
        }

        for (bool outputToBackBuffer = outputWrites % 2 == 0; auto& pass : module.techniques[0].passes)
        {
            if (!pass.cs_entry_point.empty())
            {
                VkPipelineShaderStageCreateInfo shaderStageCreateInfoComp;
                shaderStageCreateInfoComp.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageCreateInfoComp.pNext               = nullptr;
                shaderStageCreateInfoComp.flags               = 0;
                shaderStageCreateInfoComp.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
                shaderStageCreateInfoComp.module              = shaderModuleFor(pass.cs_entry_point);
                shaderStageCreateInfoComp.pName               = pass.cs_entry_point.c_str();
                shaderStageCreateInfoComp.pSpecializationInfo = (specMapEntrys.size() > 0) ? &specializationInfo : nullptr;

                VkComputePipelineCreateInfo computePipelineCreateInfo;
                computePipelineCreateInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                computePipelineCreateInfo.pNext              = nullptr;
                computePipelineCreateInfo.flags              = 0;
                computePipelineCreateInfo.stage              = shaderStageCreateInfoComp;
                computePipelineCreateInfo.layout             = pipelineLayout;
                computePipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
                computePipelineCreateInfo.basePipelineIndex  = -1;

                VkPipeline computePipeline;
                VkResult   computeResult = pLogicalDevice->vkd.CreateComputePipelines(
                    pLogicalDevice->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computePipeline);
                ASSERT_VULKAN(computeResult);

                graphicsPipelines.push_back(computePipeline);
                renderPasses.push_back(VK_NULL_HANDLE);
                renderPassBeginInfos.push_back({});
                framebuffers.push_back(std::vector<VkFramebuffer>(inputImages.size(), VK_NULL_HANDLE));
                renderTargets.push_back({});
                switchSamplers.push_back(false);

                Logger::debug("compute entry: " + pass.cs_entry_point);
                continue;
            }

            std::vector<VkAttachmentReference>               attachmentReferences;
            std::vector<VkAttachmentDescription>             attachmentDescriptions;
            std::vector<VkPipelineColorBlendAttachmentState> attachmentBlendStates;
            std::vector<std::vector<VkImageView>>            attachmentImageViews;
            std::vector<std::string>                         currentRenderTargets;

            for (int i = 0; i < 8; i++)
            {
                std::string target = pass.render_target_names[i];
                Logger::debug("render target:" + target);

                VkAttachmentDescription attachmentDescription;
                attachmentDescription.flags   = 0;
                attachmentDescription.format  = pass.srgb_write_enable ? textureFormatsSRGB[target] : textureFormatsUNORM[target];
                attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
                attachmentDescription.loadOp  = pass.clear_render_targets ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : pass.blend_enable[0]       ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                                          : VK_ATTACHMENT_LOAD_OP_DONT_CARE;

                attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
                attachmentDescription.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                if (target == "" && i == 0)
                {
                    attachmentDescription.format        = pass.srgb_write_enable ? inputOutputFormatSRGB : inputOutputFormatUNORM;
                    attachmentDescription.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
                    attachmentDescription.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
                    attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    attachmentDescription.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                else if (target == "")
                {
                    break;
                }

                attachmentDescriptions.push_back(attachmentDescription);

                VkAttachmentReference attachmentReference;
                attachmentReference.attachment = i;
                attachmentReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                attachmentReferences.push_back(attachmentReference);

                VkPipelineColorBlendAttachmentState colorBlendAttachment;
                colorBlendAttachment.blendEnable         = pass.blend_enable[0];
                colorBlendAttachment.srcColorBlendFactor = convertReshadeBlendFactor(pass.source_color_blend_factor[0]);
                colorBlendAttachment.dstColorBlendFactor = convertReshadeBlendFactor(pass.dest_color_blend_factor[0]);
                colorBlendAttachment.colorBlendOp        = convertReshadeBlendOp(pass.color_blend_op[0]);
                colorBlendAttachment.srcAlphaBlendFactor = convertReshadeBlendFactor(pass.source_alpha_blend_factor[0]);
                colorBlendAttachment.dstAlphaBlendFactor = convertReshadeBlendFactor(pass.dest_alpha_blend_factor[0]);
                colorBlendAttachment.alphaBlendOp        = convertReshadeBlendOp(pass.alpha_blend_op[0]);
                colorBlendAttachment.colorWriteMask      = pass.render_target_write_mask[0];

                attachmentBlendStates.push_back(colorBlendAttachment);

                attachmentImageViews.push_back(pass.srgb_write_enable ? renderImageViewsSRGB[target] : renderImageViewsUNORM[target]);
                if (target != "")
                {
                    currentRenderTargets.push_back(target);
                }
            }

            renderTargets.push_back(currentRenderTargets);

            VkRect2D scissor;
            scissor.offset        = {0, 0};
            scissor.extent.width  = pass.viewport_width ? pass.viewport_width : imageExtent.width;
            scissor.extent.height = pass.viewport_height ? pass.viewport_height : imageExtent.height;

            Logger::debug(std::to_string(scissor.extent.width) + " x " + std::to_string(scissor.extent.height));

            VkViewport viewport;
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = static_cast<float>(scissor.extent.width);
            viewport.height   = static_cast<float>(scissor.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            uint32_t depthAttachmentCount = 0;

            if (scissor.extent.width == imageExtent.width && scissor.extent.height == imageExtent.height)
            {
                depthAttachmentCount = 1;

                attachmentImageViews.push_back(std::vector<VkImageView>(inputImages.size(), stencilImageView));

                VkAttachmentReference attachmentReference;
                attachmentReference.attachment = attachmentReferences.size();
                attachmentReference.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                attachmentReferences.push_back(attachmentReference);

                VkAttachmentDescription attachmentDescription;
                attachmentDescription.flags          = 0;
                attachmentDescription.format         = stencilFormat;
                attachmentDescription.samples        = VK_SAMPLE_COUNT_1_BIT;
                attachmentDescription.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachmentDescription.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachmentDescription.stencilLoadOp  = firstTimeStencilAccess ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachmentDescription.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attachmentDescription.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                firstTimeStencilAccess = false;

                attachmentDescriptions.push_back(attachmentDescription);
            }


            VkSubpassDescription subpassDescription;
            subpassDescription.flags                   = 0;
            subpassDescription.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDescription.inputAttachmentCount    = 0;
            subpassDescription.pInputAttachments       = nullptr;
            subpassDescription.colorAttachmentCount    = attachmentReferences.size() - depthAttachmentCount;
            subpassDescription.pColorAttachments       = attachmentReferences.data();
            subpassDescription.pResolveAttachments     = nullptr;
            subpassDescription.pDepthStencilAttachment = depthAttachmentCount ? &attachmentReferences.back() : nullptr;
            subpassDescription.preserveAttachmentCount = 0;
            subpassDescription.pPreserveAttachments    = nullptr;

            VkSubpassDependency subpassDependencies[2];
            subpassDependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
            subpassDependencies[0].dstSubpass      = 0;
            subpassDependencies[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpassDependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpassDependencies[0].srcAccessMask   = 0;
            subpassDependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            subpassDependencies[0].dependencyFlags = 0;

            // Without this the implicit trailing dependency ends at BOTTOM_OF_PIPE with an empty
            // access mask, so what a pass renders into a texture is available but never made
            // visible to the sampler read of the pass, or the effect, that consumes it.
            subpassDependencies[1].srcSubpass      = 0;
            subpassDependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            subpassDependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpassDependencies[1].dstStageMask =
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            subpassDependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            subpassDependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
            subpassDependencies[1].dependencyFlags = 0;

            VkRenderPassCreateInfo renderPassCreateInfo;
            renderPassCreateInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.pNext           = nullptr;
            renderPassCreateInfo.flags           = 0;
            renderPassCreateInfo.attachmentCount = attachmentDescriptions.size();
            renderPassCreateInfo.pAttachments    = attachmentDescriptions.data();
            renderPassCreateInfo.subpassCount    = 1;
            renderPassCreateInfo.pSubpasses      = &subpassDescription;
            renderPassCreateInfo.dependencyCount = 2;
            renderPassCreateInfo.pDependencies   = subpassDependencies;

            VkRenderPass renderPass;
            VkResult     result = pLogicalDevice->vkd.CreateRenderPass(pLogicalDevice->device, &renderPassCreateInfo, nullptr, &renderPass);
            ASSERT_VULKAN(result);
            renderPasses.push_back(renderPass);

            VkRenderPassBeginInfo renderPassBeginInfo;
            renderPassBeginInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.pNext           = nullptr;
            renderPassBeginInfo.renderPass      = renderPass;
            renderPassBeginInfo.framebuffer     = VK_NULL_HANDLE; // changed at apply time
            renderPassBeginInfo.renderArea      = scissor;
            renderPassBeginInfo.clearValueCount = attachmentDescriptions.size();
            renderPassClearValues.emplace_back(attachmentDescriptions.size());
            renderPassBeginInfo.pClearValues =
                renderPassClearValues.back().empty()
                    ? nullptr : renderPassClearValues.back().data();

            renderPassBeginInfos.push_back(renderPassBeginInfo);


            if (writesReshadeBackBuffer(pass))
            {
                std::vector<VkImageView> backBufferImageViews = pass.srgb_write_enable ? backBufferImageViewsSRGB : backBufferImageViewsUNORM;
                std::vector<VkImageView> outputImageViews     = pass.srgb_write_enable ? outputImageViewsSRGB : outputImageViewsUNORM;
                framebuffers.push_back(createFramebuffers(
                    pLogicalDevice,
                    renderPass,
                    imageExtent,
                    {outputToBackBuffer ? backBufferImageViews : outputImageViews, std::vector<VkImageView>(inputImages.size(), stencilImageView)}));
                outputToBackBuffer = !outputToBackBuffer;
                switchSamplers.push_back(true);
            }
            else
            {
                framebuffers.push_back(createFramebuffers(pLogicalDevice, renderPass, scissor.extent, attachmentImageViews));
                switchSamplers.push_back(false);
            }



            VkPipelineShaderStageCreateInfo shaderStageCreateInfoVert;
            shaderStageCreateInfoVert.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageCreateInfoVert.pNext               = nullptr;
            shaderStageCreateInfoVert.flags               = 0;
            shaderStageCreateInfoVert.stage               = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStageCreateInfoVert.module              = shaderModuleFor(pass.vs_entry_point);
            shaderStageCreateInfoVert.pName               = pass.vs_entry_point.c_str();
            shaderStageCreateInfoVert.pSpecializationInfo = (specMapEntrys.size() > 0) ? &specializationInfo : nullptr;

            VkPipelineShaderStageCreateInfo shaderStageCreateInfoFrag;
            shaderStageCreateInfoFrag.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageCreateInfoFrag.pNext               = nullptr;
            shaderStageCreateInfoFrag.flags               = 0;
            shaderStageCreateInfoFrag.stage               = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStageCreateInfoFrag.module              = shaderModuleFor(pass.ps_entry_point);
            shaderStageCreateInfoFrag.pName               = pass.ps_entry_point.c_str();
            shaderStageCreateInfoFrag.pSpecializationInfo = (specMapEntrys.size() > 0) ? &specializationInfo : nullptr;

            VkPipelineShaderStageCreateInfo shaderStages[] = {shaderStageCreateInfoVert, shaderStageCreateInfoFrag};

            VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo;
            vertexInputCreateInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputCreateInfo.pNext                           = nullptr;
            vertexInputCreateInfo.flags                           = 0;
            vertexInputCreateInfo.vertexBindingDescriptionCount   = 0;
            vertexInputCreateInfo.pVertexBindingDescriptions      = nullptr;
            vertexInputCreateInfo.vertexAttributeDescriptionCount = 0;
            vertexInputCreateInfo.pVertexAttributeDescriptions    = nullptr;

            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            switch (pass.topology)
            {
                case reshadefx::primitive_topology::point_list: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
                case reshadefx::primitive_topology::line_list: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
                case reshadefx::primitive_topology::line_strip: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
                case reshadefx::primitive_topology::triangle_list: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
                case reshadefx::primitive_topology::triangle_strip: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
                default: Logger::err("unsupported primitiv type" + convertToString((uint8_t) pass.topology)); break;
            }

            VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo;
            inputAssemblyCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyCreateInfo.pNext                  = nullptr;
            inputAssemblyCreateInfo.flags                  = 0;
            inputAssemblyCreateInfo.topology               = topology;
            inputAssemblyCreateInfo.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewportStateCreateInfo;
            viewportStateCreateInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportStateCreateInfo.pNext         = nullptr;
            viewportStateCreateInfo.flags         = 0;
            viewportStateCreateInfo.viewportCount = 1;
            viewportStateCreateInfo.pViewports    = &viewport;
            viewportStateCreateInfo.scissorCount  = 1;
            viewportStateCreateInfo.pScissors     = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizationCreateInfo;
            rasterizationCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizationCreateInfo.pNext                   = nullptr;
            rasterizationCreateInfo.flags                   = 0;
            rasterizationCreateInfo.depthClampEnable        = VK_FALSE;
            rasterizationCreateInfo.rasterizerDiscardEnable = VK_FALSE;
            rasterizationCreateInfo.polygonMode             = VK_POLYGON_MODE_FILL;
            rasterizationCreateInfo.cullMode                = VK_CULL_MODE_NONE;
            rasterizationCreateInfo.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizationCreateInfo.depthBiasEnable         = VK_FALSE;
            rasterizationCreateInfo.depthBiasConstantFactor = 0.0f;
            rasterizationCreateInfo.depthBiasClamp          = 0.0f;
            rasterizationCreateInfo.depthBiasSlopeFactor    = 0.0f;
            rasterizationCreateInfo.lineWidth               = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampleCreateInfo;
            multisampleCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampleCreateInfo.pNext                 = nullptr;
            multisampleCreateInfo.flags                 = 0;
            multisampleCreateInfo.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
            multisampleCreateInfo.sampleShadingEnable   = VK_FALSE;
            multisampleCreateInfo.minSampleShading      = 1.0f;
            multisampleCreateInfo.pSampleMask           = nullptr;
            multisampleCreateInfo.alphaToCoverageEnable = VK_FALSE;
            multisampleCreateInfo.alphaToOneEnable      = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlendCreateInfo;
            colorBlendCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendCreateInfo.pNext             = nullptr;
            colorBlendCreateInfo.flags             = 0;
            colorBlendCreateInfo.logicOpEnable     = VK_FALSE;
            colorBlendCreateInfo.logicOp           = VK_LOGIC_OP_NO_OP;
            colorBlendCreateInfo.attachmentCount   = attachmentBlendStates.size();
            colorBlendCreateInfo.pAttachments      = attachmentBlendStates.data();
            colorBlendCreateInfo.blendConstants[0] = 0.0f;
            colorBlendCreateInfo.blendConstants[1] = 0.0f;
            colorBlendCreateInfo.blendConstants[2] = 0.0f;
            colorBlendCreateInfo.blendConstants[3] = 0.0f;

            VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo;
            dynamicStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStateCreateInfo.pNext             = nullptr;
            dynamicStateCreateInfo.flags             = 0;
            dynamicStateCreateInfo.dynamicStateCount = 0;
            dynamicStateCreateInfo.pDynamicStates    = nullptr;

            VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = {};

            depthStencilStateCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilStateCreateInfo.pNext                 = nullptr;
            depthStencilStateCreateInfo.depthTestEnable       = VK_FALSE;
            depthStencilStateCreateInfo.depthWriteEnable      = VK_FALSE;
            depthStencilStateCreateInfo.depthCompareOp        = VK_COMPARE_OP_ALWAYS;
            depthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
            depthStencilStateCreateInfo.stencilTestEnable     = pass.stencil_enable;
            depthStencilStateCreateInfo.front.failOp          = convertReshadeStencilOp(pass.stencil_fail_op);
            depthStencilStateCreateInfo.front.passOp          = convertReshadeStencilOp(pass.stencil_pass_op);
            depthStencilStateCreateInfo.front.depthFailOp     = convertReshadeStencilOp(pass.stencil_depth_fail_op);
            depthStencilStateCreateInfo.front.compareOp       = convertReshadeCompareOp(pass.stencil_comparison_func);
            depthStencilStateCreateInfo.front.compareMask     = pass.stencil_read_mask;
            depthStencilStateCreateInfo.front.writeMask       = pass.stencil_write_mask;
            depthStencilStateCreateInfo.front.reference       = pass.stencil_reference_value;
            depthStencilStateCreateInfo.back                  = depthStencilStateCreateInfo.front;
            depthStencilStateCreateInfo.minDepthBounds        = 0.0f;
            depthStencilStateCreateInfo.maxDepthBounds        = 1.0f;

            VkGraphicsPipelineCreateInfo pipelineCreateInfo;
            pipelineCreateInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineCreateInfo.pNext               = nullptr;
            pipelineCreateInfo.flags               = 0;
            pipelineCreateInfo.stageCount          = 2;
            pipelineCreateInfo.pStages             = shaderStages;
            pipelineCreateInfo.pVertexInputState   = &vertexInputCreateInfo;
            pipelineCreateInfo.pInputAssemblyState = &inputAssemblyCreateInfo;
            pipelineCreateInfo.pTessellationState  = nullptr;
            pipelineCreateInfo.pViewportState      = &viewportStateCreateInfo;
            pipelineCreateInfo.pRasterizationState = &rasterizationCreateInfo;
            pipelineCreateInfo.pMultisampleState   = &multisampleCreateInfo;
            pipelineCreateInfo.pDepthStencilState  = &depthStencilStateCreateInfo;
            pipelineCreateInfo.pColorBlendState    = &colorBlendCreateInfo;
            pipelineCreateInfo.pDynamicState       = &dynamicStateCreateInfo;
            pipelineCreateInfo.layout              = pipelineLayout;
            pipelineCreateInfo.renderPass          = renderPass;
            pipelineCreateInfo.subpass             = 0;
            pipelineCreateInfo.basePipelineHandle  = VK_NULL_HANDLE;
            pipelineCreateInfo.basePipelineIndex   = -1;

            VkPipeline pipeline;
            result = pLogicalDevice->vkd.CreateGraphicsPipelines(pLogicalDevice->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
            ASSERT_VULKAN(result);

            graphicsPipelines.push_back(pipeline);

            Logger::debug("vertex   entry: " + pass.vs_entry_point);
            Logger::debug("fragment entry: " + pass.ps_entry_point);
        }
        Logger::debug("finished creating Reshade effect");
    }

    void ReshadeEffect::updateEffect(uint32_t imageIndex)
    {
        if (imageIndex < uniformBuffersMapped.size()
            && uniformBuffersMapped[imageIndex] != nullptr)
        {
            for (auto& uniform : uniforms)
                uniform->update(uniformBuffersMapped[imageIndex]);
        }
    }

    void ReshadeEffect::useDepthImage(VkImageView depthImageView)
    {
        bool hasDepth = (depthImageView != VK_NULL_HANDLE);
        for (auto& uniform : uniforms)
        {
            auto* depthUniform = dynamic_cast<DepthUniform*>(uniform.get());
            if (depthUniform)
                depthUniform->setDepthAvailable(hasDepth);
        }

        std::vector<std::string> depthTextureNames;

        for (auto& texture : module.textures)
        {
            if (texture.semantic == "DEPTH")
            {
                depthTextureNames.push_back(texture.unique_name);
            }
        }

        for (size_t i = 0; i < module.samplers.size(); i++)
        {
            reshadefx::sampler info = module.samplers[i];
            for (auto& name : depthTextureNames)
            {
                if (info.texture_name == name)
                {
                    for (uint32_t j = 0; j < inputImages.size(); j++)
                    {
                        VkDescriptorImageInfo imageInfo;
                        imageInfo.sampler = samplers[i];
                        imageInfo.imageView   = depthImageView ? depthImageView : inputImageViewsUNORM[j];
                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                        VkWriteDescriptorSet writeDescriptorSet = {};

                        writeDescriptorSet.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writeDescriptorSet.pNext            = nullptr;
                        writeDescriptorSet.dstSet           = inputDescriptorSets[j];
                        writeDescriptorSet.dstBinding       = i;
                        writeDescriptorSet.dstArrayElement  = 0;
                        writeDescriptorSet.descriptorCount  = 1;
                        writeDescriptorSet.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        writeDescriptorSet.pImageInfo       = &imageInfo;
                        writeDescriptorSet.pBufferInfo      = nullptr;
                        writeDescriptorSet.pTexelBufferView = nullptr;

                        pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &writeDescriptorSet, 0, nullptr);
                        if (outputWrites > 1)
                        {
                            writeDescriptorSet.dstSet = backBufferDescriptorSets[j];
                            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &writeDescriptorSet, 0, nullptr);
                        }
                        if (outputWrites > 2)
                        {
                            writeDescriptorSet.dstSet = outputDescriptorSets[j];
                            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, 1, &writeDescriptorSet, 0, nullptr);
                        }
                    }
                    break;
                }
            }
        }
    }
    void ReshadeEffect::applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer)
    {
        VkImageMemoryBarrier memoryBarrier;
        memoryBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memoryBarrier.pNext               = nullptr;
        memoryBarrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        memoryBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        memoryBarrier.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        memoryBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memoryBarrier.image               = inputImages[imageIndex];

        memoryBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memoryBarrier.subresourceRange.baseMipLevel   = 0;
        memoryBarrier.subresourceRange.levelCount     = 1;
        memoryBarrier.subresourceRange.baseArrayLayer = 0;
        memoryBarrier.subresourceRange.layerCount     = 1;

        VkImageMemoryBarrier secondBarrier;
        secondBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        secondBarrier.pNext               = nullptr;
        secondBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        secondBarrier.dstAccessMask       = 0;
        secondBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        secondBarrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        secondBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        secondBarrier.image               = inputImages[imageIndex];

        secondBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        secondBarrier.subresourceRange.baseMipLevel   = 0;
        secondBarrier.subresourceRange.levelCount     = 1;
        secondBarrier.subresourceRange.baseArrayLayer = 0;
        secondBarrier.subresourceRange.layerCount     = 1;

        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        memoryBarrier.image     = outputImages[imageIndex];
        memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        memoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pLogicalDevice->vkd.CmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
        if (outputWrites > 1)
        {
            memoryBarrier.image = backBufferImages[imageIndex];
            pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                   0,
                                                   0,
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   1,
                                                   &memoryBarrier);
        }

        memoryBarrier.image                       = stencilImage;
        memoryBarrier.srcAccessMask               = 0;
        memoryBarrier.dstAccessMask               = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        memoryBarrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        memoryBarrier.newLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        memoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               1,
                                               &memoryBarrier);


        // An effect with no samplers gets no descriptor sets allocated at all, so every one of
        // these has to be checked before it is indexed. Reading past the end handed the driver a
        // wild pointer and took the host process down with it.
        const auto bindSet = [&](VkPipelineBindPoint bindPoint, uint32_t firstSet, const std::vector<VkDescriptorSet>& sets) {
            if (imageIndex < sets.size())
                pLogicalDevice->vkd.CmdBindDescriptorSets(
                    commandBuffer, bindPoint, pipelineLayout, firstSet, 1, &sets[imageIndex], 0, nullptr);
        };

        bindSet(VK_PIPELINE_BIND_POINT_GRAPHICS, 1, inputDescriptorSets);

        if (bufferSize)
        {
            bindSet(VK_PIPELINE_BIND_POINT_GRAPHICS, 0,
                    bufferDescriptorSets);
        }

        bool backBufferNext = outputWrites % 2 == 0;
        for (size_t i = 0; i < graphicsPipelines.size(); i++)
        {
            const auto& passInfo = module.techniques[0].passes[i];

            if (!passInfo.cs_entry_point.empty())
            {
                std::vector<VkImageMemoryBarrier> storageBarriers;
                for (const auto& storageTextureName : storageTextureNames)
                {
                    VkImageMemoryBarrier storageBarrier   = {};
                    storageBarrier.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    storageBarrier.pNext                  = nullptr;
                    storageBarrier.srcAccessMask          = VK_ACCESS_SHADER_READ_BIT;
                    storageBarrier.dstAccessMask          = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    storageBarrier.oldLayout              = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    storageBarrier.newLayout              = VK_IMAGE_LAYOUT_GENERAL;
                    storageBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
                    storageBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
                    storageBarrier.image                  = textureImages[storageTextureName][0];
                    storageBarrier.subresourceRange       = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
                    storageBarriers.push_back(storageBarrier);
                }

                pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                       0,
                                                       0,
                                                       nullptr,
                                                       0,
                                                       nullptr,
                                                       storageBarriers.size(),
                                                       storageBarriers.data());

                pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, graphicsPipelines[i]);

                bindSet(VK_PIPELINE_BIND_POINT_COMPUTE, 1, inputDescriptorSets);

                if (bufferSize)
                {
                    bindSet(VK_PIPELINE_BIND_POINT_COMPUTE, 0,
                            bufferDescriptorSets);
                }

                bindSet(VK_PIPELINE_BIND_POINT_COMPUTE, 2, storageDescriptorSets);

                pLogicalDevice->vkd.CmdDispatch(commandBuffer, passInfo.viewport_width, passInfo.viewport_height, passInfo.viewport_dispatch_z);

                for (auto& storageBarrier : storageBarriers)
                {
                    storageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    storageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    storageBarrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
                    storageBarrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                       0,
                                                       0,
                                                       nullptr,
                                                       0,
                                                       nullptr,
                                                       storageBarriers.size(),
                                                       storageBarriers.data());
                continue;
            }

            renderPassBeginInfos[i].framebuffer = framebuffers[i][imageIndex];

            pLogicalDevice->vkd.CmdBeginRenderPass(commandBuffer, &renderPassBeginInfos[i], VK_SUBPASS_CONTENTS_INLINE);

            pLogicalDevice->vkd.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelines[i]);

            pLogicalDevice->vkd.CmdDraw(commandBuffer, module.techniques[0].passes[i].num_vertices, 1, 0, 0);

            pLogicalDevice->vkd.CmdEndRenderPass(commandBuffer);

            if (switchSamplers[i] && outputWrites > 1)
            {
                if (backBufferNext)
                    bindSet(VK_PIPELINE_BIND_POINT_GRAPHICS, 1, backBufferDescriptorSets);
                else if (outputWrites > 2)
                    bindSet(VK_PIPELINE_BIND_POINT_GRAPHICS, 1, outputDescriptorSets);
                backBufferNext = !backBufferNext;
            }

            for (auto& renderTarget : renderTargets[i])
            {
                generateMipMaps(
                    pLogicalDevice, commandBuffer, textureImages[renderTarget][0], textureExtents[renderTarget], textureMipLevels[renderTarget]);
            }
        }
        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               1,
                                               &secondBarrier);
        secondBarrier.image = outputImages[imageIndex];
        pLogicalDevice->vkd.CmdPipelineBarrier(commandBuffer,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               1,
                                               &secondBarrier);
    }

    ReshadeEffect::~ReshadeEffect()
    {
        destroyResources();
    }

    void ReshadeEffect::destroyResources() noexcept
    {
        if (resourcesDestroyed || pLogicalDevice == nullptr)
            return;
        resourcesDestroyed = true;
        Logger::debug("destroying ReshadeEffect" + convertToString(this));
        for (auto& pipeline : graphicsPipelines)
        {
            pLogicalDevice->vkd.DestroyPipeline(pLogicalDevice->device, pipeline, nullptr);
        }

        if (bufferSize)
        {
            for (size_t i = 0; i < uniformBuffers.size(); ++i)
            {
                if (i < uniformBuffersMapped.size()
                    && uniformBuffersMapped[i] != nullptr
                    && i < uniformBufferMemory.size()
                    && uniformBufferMemory[i] != VK_NULL_HANDLE)
                {
                    pLogicalDevice->vkd.UnmapMemory(
                        pLogicalDevice->device, uniformBufferMemory[i]);
                }
                if (uniformBuffers[i] != VK_NULL_HANDLE)
                {
                    pLogicalDevice->vkd.DestroyBuffer(
                        pLogicalDevice->device, uniformBuffers[i], nullptr);
                }
                if (i < uniformBufferMemory.size()
                    && uniformBufferMemory[i] != VK_NULL_HANDLE)
                {
                    freeTrackedMemory(
                        pLogicalDevice, uniformBufferMemory[i], nullptr);
                }
            }
            uniformBuffers.clear();
            uniformBufferMemory.clear();
            uniformBuffersMapped.clear();
            bufferDescriptorSets.clear();
        }

        if (pipelineLayout != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyPipelineLayout(pLogicalDevice->device, pipelineLayout, nullptr);
        for (auto& renderPass : renderPasses)
        {
            pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        }

        for (auto& storageViews : storageImageViewVector)
        {
            if (!storageViews.empty())
            {
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, storageViews[0], nullptr);
            }
        }
        if (storageImageDescriptorSetLayout != VK_NULL_HANDLE)
        {
            pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, storageImageDescriptorSetLayout, nullptr);
        }
        if (imageSamplerDescriptorSetLayout != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, imageSamplerDescriptorSetLayout, nullptr);
        if (uniformDescriptorSetLayout != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorSetLayout(pLogicalDevice->device, uniformDescriptorSetLayout, nullptr);

        for (const auto& [entryPointName, created] : shaderModules)
            pLogicalDevice->vkd.DestroyShaderModule(pLogicalDevice->device, created, nullptr);

        if (descriptorPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);

        for (auto& fbs : framebuffers)
        {
            for (auto& fb : fbs)
            {
                pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, fb, nullptr);
            }
        }

        std::set<VkImageView> imageViewSet;

        imageViewSet.insert(inputImageViewsSRGB.begin(), inputImageViewsSRGB.end());
        imageViewSet.insert(inputImageViewsUNORM.begin(), inputImageViewsUNORM.end());
        imageViewSet.insert(outputImageViewsSRGB.begin(), outputImageViewsSRGB.end());
        imageViewSet.insert(outputImageViewsUNORM.begin(), outputImageViewsUNORM.end());
        imageViewSet.insert(backBufferImageViewsSRGB.begin(), backBufferImageViewsSRGB.end());
        imageViewSet.insert(backBufferImageViewsUNORM.begin(), backBufferImageViewsUNORM.end());

        for (auto& it : textureImageViewsSRGB)
        {
            for (auto imageView : it.second)
            {
                imageViewSet.insert(imageView);
            }
        }
        for (auto& it : textureImageViewsUNORM)
        {
            for (auto imageView : it.second)
            {
                imageViewSet.insert(imageView);
            }
        }

        for (auto& it : renderImageViewsSRGB)
        {
            for (auto imageView : it.second)
            {
                imageViewSet.insert(imageView);
            }
        }
        for (auto& it : renderImageViewsUNORM)
        {
            for (auto imageView : it.second)
            {
                imageViewSet.insert(imageView);
            }
        }

        for (auto imageView : imageViewSet)
        {
            if (imageView != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, imageView, nullptr);
        }
        if (stencilImageView != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyImageView(pLogicalDevice->device, stencilImageView, nullptr);

        for (auto& it : textureImages)
        {
            if (sharedTextureNames.find(it.first) != sharedTextureNames.end())
                continue;
            for (auto image : it.second)
            {
                if (image != VK_NULL_HANDLE)
                    pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            }
        }

        for (auto& image : backBufferImages)
        {
            if (image != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
        }

        if (stencilImage != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, stencilImage, nullptr);

        for (auto& sampler : samplers)
        {
            if (sampler != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroySampler(pLogicalDevice->device, sampler, nullptr);
        }

        for (auto& memory : textureMemory)
        {
            if (memory != VK_NULL_HANDLE)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);
        }
        pLogicalDevice = nullptr;
    }

    void ReshadeEffect::createReshadeModule()
    {
        std::vector<std::pair<std::string, std::string>> defines =
            reshadeCompileDefines(imageExtent, inputOutputFormatUNORM, colorSpace, customPreprocessorDefs);
        for (const auto& def : customPreprocessorDefs)
            Logger::debug("  custom macro: " + def.name + " = " + def.value);

        ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();

        std::string shaderPath = this->effectPath;
        if (shaderPath.empty())
        {
            shaderPath = pEffectRegistry->getEffectFilePath(effectName);
            if (shaderPath.empty())
            {
                for (const auto& searchPath : shaderMgrConfig.discoveredShaderPaths)
                {
                    std::string candidate = searchPath + "/" + effectName + ".fx";
                    if (std::filesystem::exists(candidate))
                    {
                        shaderPath = candidate;
                        break;
                    }
                    candidate = searchPath + "/" + effectName;
                    if (std::filesystem::exists(candidate))
                    {
                        shaderPath = candidate;
                        break;
                    }
                }
            }
        }

        if (shaderPath.empty())
        {
            Logger::err("no shader file found for: " + effectName);
            throw std::runtime_error("failed to load shader: " + effectName);
        }

        auto compiled = getOrCompileReshadeEffect(shaderPath, defines, shaderMgrConfig.discoveredShaderPaths,
                                                  pEffectRegistry && pEffectRegistry->getAllowHalfPrecision(effectName));
        if (!compiled->ok())
        {
            Logger::err(shaderPath + ": " + compiled->error);
            throw std::runtime_error("failed to compile shader: " + effectName);
        }
        if (!compiled->warnings.empty())
            Logger::warn(shaderPath + ": " + compiled->warnings);

        module = compiled->module;


        for (const auto& [entryPointName, words] : compiled->entryPointSpirv)
        {
            VkShaderModuleCreateInfo shaderCreateInfo;
            shaderCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderCreateInfo.pNext    = nullptr;
            shaderCreateInfo.flags    = 0;
            shaderCreateInfo.codeSize = words.size() * sizeof(uint32_t);
            shaderCreateInfo.pCode    = words.data();

            VkShaderModule created = VK_NULL_HANDLE;
            VkResult result = pLogicalDevice->vkd.CreateShaderModule(pLogicalDevice->device, &shaderCreateInfo, nullptr, &created);
            if (result != VK_SUCCESS)
            {
                Logger::err("failed to create shader module for: " + effectName + " entry point " + entryPointName);
                throw std::runtime_error("VkCreateShaderModule failed for: " + effectName);
            }

            shaderModules[entryPointName] = created;
        }

        Logger::debug("created reshade shaderModule");
    }

    VkFormat ReshadeEffect::convertReshadeFormat(reshadefx::texture_format texFormat)
    {
        switch (texFormat)
        {
            case reshadefx::texture_format::r8: return VK_FORMAT_R8_UNORM;
            case reshadefx::texture_format::r16: return VK_FORMAT_R16_UNORM;
            case reshadefx::texture_format::r16f: return VK_FORMAT_R16_SFLOAT;
            case reshadefx::texture_format::r32f: return VK_FORMAT_R32_SFLOAT;
            case reshadefx::texture_format::r32i: return VK_FORMAT_R32_SINT;
            case reshadefx::texture_format::r32u: return VK_FORMAT_R32_UINT;
            case reshadefx::texture_format::rg8: return VK_FORMAT_R8G8_UNORM;
            case reshadefx::texture_format::rg16: return VK_FORMAT_R16G16_UNORM;
            case reshadefx::texture_format::rg16f: return VK_FORMAT_R16G16_SFLOAT;
            case reshadefx::texture_format::rg32f: return VK_FORMAT_R32G32_SFLOAT;
            case reshadefx::texture_format::rgba8: return VK_FORMAT_R8G8B8A8_UNORM;
            case reshadefx::texture_format::rgba16: return VK_FORMAT_R16G16B16A16_UNORM;
            case reshadefx::texture_format::rgba16f: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case reshadefx::texture_format::rgba32f: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case reshadefx::texture_format::rgb10a2: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    VkCompareOp ReshadeEffect::convertReshadeCompareOp(reshadefx::stencil_func compareOp)
    {
        switch (compareOp)
        {
            case reshadefx::stencil_func::never: return VK_COMPARE_OP_NEVER;
            case reshadefx::stencil_func::less: return VK_COMPARE_OP_LESS;
            case reshadefx::stencil_func::equal: return VK_COMPARE_OP_EQUAL;
            case reshadefx::stencil_func::less_equal: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case reshadefx::stencil_func::greater: return VK_COMPARE_OP_GREATER;
            case reshadefx::stencil_func::not_equal: return VK_COMPARE_OP_NOT_EQUAL;
            case reshadefx::stencil_func::greater_equal: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case reshadefx::stencil_func::always: return VK_COMPARE_OP_ALWAYS;
            default: return VK_COMPARE_OP_ALWAYS;
        }
    }

    VkStencilOp ReshadeEffect::convertReshadeStencilOp(reshadefx::stencil_op stencilOp)
    {
        switch (stencilOp)
        {
            case reshadefx::stencil_op::zero: return VK_STENCIL_OP_ZERO;
            case reshadefx::stencil_op::keep: return VK_STENCIL_OP_KEEP;
            case reshadefx::stencil_op::replace: return VK_STENCIL_OP_REPLACE;
            case reshadefx::stencil_op::increment_saturate: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case reshadefx::stencil_op::decrement_saturate: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case reshadefx::stencil_op::invert: return VK_STENCIL_OP_INVERT;
            case reshadefx::stencil_op::increment: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case reshadefx::stencil_op::decrement: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            default: return VK_STENCIL_OP_KEEP;
        }
    }

    VkBlendOp ReshadeEffect::convertReshadeBlendOp(reshadefx::blend_op blendOp)
    {
        switch (blendOp)
        {
            case reshadefx::blend_op::add: return VK_BLEND_OP_ADD;
            case reshadefx::blend_op::subtract: return VK_BLEND_OP_SUBTRACT;
            case reshadefx::blend_op::reverse_subtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case reshadefx::blend_op::min: return VK_BLEND_OP_MIN;
            case reshadefx::blend_op::max: return VK_BLEND_OP_MAX;
            default: return VK_BLEND_OP_ADD;
        }
    }

    VkBlendFactor ReshadeEffect::convertReshadeBlendFactor(reshadefx::blend_factor blendFactor)
    {
        switch (blendFactor)
        {
            case reshadefx::blend_factor::zero: return VK_BLEND_FACTOR_ZERO;
            case reshadefx::blend_factor::one: return VK_BLEND_FACTOR_ONE;
            case reshadefx::blend_factor::source_color: return VK_BLEND_FACTOR_SRC_COLOR;
            case reshadefx::blend_factor::source_alpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case reshadefx::blend_factor::one_minus_source_color: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case reshadefx::blend_factor::one_minus_source_alpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case reshadefx::blend_factor::dest_alpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case reshadefx::blend_factor::one_minus_dest_alpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case reshadefx::blend_factor::dest_color: return VK_BLEND_FACTOR_DST_COLOR;
            case reshadefx::blend_factor::one_minus_dest_color: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            default: return VK_BLEND_FACTOR_ZERO;
        }
    }
} // namespace vkBasalt
