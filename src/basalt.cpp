#include "vulkan_include.hpp"

#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <sys/stat.h>
#include <signal.h>
#include <setjmp.h>

#include "util.hpp"
#include "crash_guard.hpp"
#include "keyboard_input.hpp"
#include "mouse_input.hpp"
#include "keyboard_input_wayland.hpp"
#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "input_blocker.hpp"
#include "wayland_display.hpp"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#include "vulkan/vulkan_wayland.h"

#include "config_paths.hpp"
#include "logical_device.hpp"
#include "logical_swapchain.hpp"

#include "image_view.hpp"
#include "sampler.hpp"
#include "framebuffer.hpp"
#include "descriptor_set.hpp"
#include "shader.hpp"
#include "graphics_pipeline.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "memory.hpp"
#include "config_serializer.hpp"
#include "shader_cache.hpp"
#include "settings_manager.hpp"
#include "effect_selection.hpp"
#include "fake_swapchain.hpp"
#include "renderpass.hpp"
#include "format.hpp"
#include "logger.hpp"

#include "effects/effect.hpp"
#include "effects/effect_reshade.hpp"
#include "effects/effect_transfer.hpp"
#include "effects/builtin/builtin_effects.hpp"
#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"

#define VKBASALT_NAME "VK_LAYER_VKBASALT_OVERLAY_post_processing"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    std::shared_ptr<Config> pBaseConfig = nullptr;
    std::shared_ptr<Config> pConfig = nullptr;
    EffectRegistry effectRegistry;

    Logger Logger::s_instance;

    std::vector<std::string> activeEffectNames()
    {
        return enabledEffectNames(
            effectRegistry.getSelectedEffects(),
            effectRegistry.getEffectEnabledStates());
    }

    std::unordered_map<void*, InstanceDispatch>                           instanceDispatchMap;
    std::unordered_map<void*, VkInstance>                                 instanceMap;
    std::unordered_map<void*, uint32_t>                                   instanceVersionMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>>             deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    std::mutex globalLock;
#ifdef _GCC_
    using scoped_lock __attribute__((unused)) = std::lock_guard<std::mutex>;
#else
    using scoped_lock = std::lock_guard<std::mutex>;
#endif

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    struct CachedEffectsData
    {
        std::vector<std::string> currentConfigEffects;
        std::vector<std::string> defaultConfigEffects;
        std::map<std::string, std::string> effectPaths;
        std::string configPath;
        bool initialized = false;
    };
    CachedEffectsData cachedEffects;

    struct CachedParametersData
    {
        std::vector<std::unique_ptr<EffectParam>> parameters;
        std::vector<std::string> effectNames;
        std::string configPath;
        bool dirty = true;
    };
    CachedParametersData cachedParams;

    struct ResizeDebounceState
    {
        std::chrono::steady_clock::time_point lastResizeTime;
        bool pending = false;
    };
    ResizeDebounceState resizeDebounce;
    constexpr int64_t RESIZE_DEBOUNCE_MS = 200;

    bool handleKeyPress(uint32_t keySymbol, bool& wasPressed)
    {
        if (isKeyPressed(keySymbol))
        {
            if (!wasPressed)
            {
                wasPressed = true;
                return true;
            }
        }
        else
        {
            wasPressed = false;
        }
        return false;
    }

    struct DepthState
    {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    DepthState getDepthState(LogicalDevice* pLogicalDevice)
    {
        DepthState state;
        for (const auto& depthImage : pLogicalDevice->depthImages)
        {
            if (depthImage.view == VK_NULL_HANDLE)
                continue;

            state.imageView = depthImage.view;
            state.image = depthImage.image;
            state.format = depthImage.format;
            break;
        }
        return state;
    }

    EffectCollectionBuildSpec captureEffectCollectionBuildSpec(
        LogicalSwapchain* pLogicalSwapchain, Config* pBuildSource,
        const std::vector<std::string>& orderedEffects, const DepthState& depth)
    {
        EffectCollectionBuildSpec spec;
        spec.orderedActiveEffects = orderedEffects;
        spec.configRevision = pBuildSource->getBuildStateRevision();
        spec.configState = pBuildSource->getBuildStateSignature();
        spec.registryRevision = effectRegistry.getBuildStateRevision();
        spec.registryState = effectRegistry.getBuildStateSignature(orderedEffects);
        spec.format = pLogicalSwapchain->format;
        spec.colorSpace = pLogicalSwapchain->swapchainCreateInfo.imageColorSpace;
        spec.extent = pLogicalSwapchain->imageExtent;
        spec.imageCount = pLogicalSwapchain->imageCount;
        spec.maxEffectSlots = pLogicalSwapchain->maxEffectSlots;
        spec.realImages = pLogicalSwapchain->images;
        spec.intermediateImages = pLogicalSwapchain->fakeImages;
        spec.depthImage = depth.image;
        spec.depthView = depth.imageView;
        spec.depthFormat = depth.format;
        return spec;
    }

    static std::string detectedGameName;
    static std::string activeProfileName;
    static std::string activeProfilePath;

    void initConfigs()
    {
        if (pBaseConfig != nullptr)
            return;  // Already initialized

        if (conflictingLayerLoaded())
        {
            Logger::warn("the unforked vkBasalt is loaded in this process as well, from " + conflictingLayerPath()
                         + " -- both layers share ENABLE_VKBASALT, so both are active: effects apply twice and "
                           "DISABLE_VKBASALT cannot turn off one without the other. Remove the install that path points at.");
        }

        {
            std::string baseDir = ConfigSerializer::getBaseConfigDir();
            if (!baseDir.empty())
                mkdir(baseDir.c_str(), 0755);
        }

        settingsManager.initialize();

        pBaseConfig = std::make_shared<Config>();

        detectedGameName = ConfigSerializer::detectGameName();

        std::string currentConfigPath;

        const char* envConfig = std::getenv("VKBASALT_CONFIG_FILE");
        if (envConfig && *envConfig)
        {
            currentConfigPath = envConfig;
            Logger::info("config from env: " + currentConfigPath);
        }
        else if (!detectedGameName.empty())
        {
            activeProfileName = ConfigSerializer::getActiveProfile(detectedGameName);
            activeProfilePath = ConfigSerializer::getProfilePath(detectedGameName, activeProfileName);

            struct stat st;
            if (stat(activeProfilePath.c_str(), &st) != 0)
            {
                activeProfilePath = ConfigSerializer::ensureGameProfile(detectedGameName);
            }

            if (!activeProfilePath.empty())
            {
                currentConfigPath = activeProfilePath;
                Logger::info("game: " + detectedGameName + " | profile: " + activeProfileName);
            }
        }

        if (currentConfigPath.empty())
        {
            std::string defaultName = ConfigSerializer::getDefaultConfig();
            if (!defaultName.empty())
                currentConfigPath = ConfigSerializer::getConfigsDir() + "/" + defaultName + ".conf";
        }

        if (!currentConfigPath.empty())
        {
            std::ifstream file(currentConfigPath);
            if (file.good())
            {
                pConfig = std::make_shared<Config>(currentConfigPath);
                pConfig->setFallback(pBaseConfig.get());

                setMemorySoftLimitBytes(static_cast<VkDeviceSize>(pConfig->getOption<int32_t>("vramSoftLimitMB", 0))
                                        * 1024 * 1024);
                Logger::info("current config: " + currentConfigPath);
            }
            else
            {
                pConfig = pBaseConfig;
            }
        }
        else
        {
            pConfig = pBaseConfig;
        }

        if (!activeProfilePath.empty())
        {
            ProfileSettings ps = ConfigSerializer::loadProfileSettings(activeProfilePath);
            if (ps.safeAntiCheat)
            {
                settingsManager.setSafeAntiCheat(true);
                settingsManager.setDepthCapture(false);
                Logger::info("safeAntiCheat enabled - depth capture forced off, depth-using effects blocked");
            }
        }

        effectRegistry.initialize(pConfig.get());
    }

    void switchConfig(const std::string& configPath)
    {
        Logger::info("switching to config: " + configPath);

        pConfig = std::make_shared<Config>(configPath);
        pConfig->setFallback(pBaseConfig.get());

        if (pBaseConfig)
            pBaseConfig->clearOverrides();

        effectRegistry.initialize(pConfig.get());
        cachedParams.dirty = true;

        Logger::info("switched to config: " + configPath);
    }

    void getAvailableEffects(Config* pConfig,
                             std::vector<std::string>& currentConfigEffects,
                             std::vector<std::string>& defaultConfigEffects,
                             std::map<std::string, std::string>& effectPaths)
    {
        if (cachedEffects.initialized && cachedEffects.configPath == pConfig->getConfigFilePath())
        {
            currentConfigEffects = cachedEffects.currentConfigEffects;
            defaultConfigEffects = cachedEffects.defaultConfigEffects;
            effectPaths = cachedEffects.effectPaths;
            return;
        }

        currentConfigEffects.clear();
        defaultConfigEffects.clear();
        effectPaths.clear();

        std::set<std::string> knownEffects;

        auto configEffects = pConfig->getEffectDefinitions();
        for (const auto& [name, path] : configEffects)
        {
            currentConfigEffects.push_back(name);
            effectPaths[name] = path;
            knownEffects.insert(name);
        }

        if (pBaseConfig && pBaseConfig->getConfigFilePath() != pConfig->getConfigFilePath())
        {
            auto defaultEffects = pBaseConfig->getEffectDefinitions();
            for (const auto& [name, path] : defaultEffects)
            {
                if (knownEffects.find(name) == knownEffects.end())
                {
                    defaultConfigEffects.push_back(name);
                    effectPaths[name] = path;
                    knownEffects.insert(name);
                }
            }
        }

        ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();
        for (const auto& shaderPath : shaderMgrConfig.discoveredShaderPaths)
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(shaderPath))
                {
                    if (!entry.is_regular_file())
                        continue;

                    std::string filename = entry.path().filename().string();
                    if (filename.size() < 4 || filename.substr(filename.size() - 3) != ".fx")
                        continue;

                    std::string effectName = filename.substr(0, filename.size() - 3);

                    if (knownEffects.find(effectName) != knownEffects.end())
                        continue;

                    defaultConfigEffects.push_back(effectName);
                    effectPaths[effectName] = entry.path().string();
                    knownEffects.insert(effectName);
                }
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                Logger::warn("failed to scan shader path " + shaderPath + ": " + std::string(e.what()));
            }
        }

        std::sort(defaultConfigEffects.begin(), defaultConfigEffects.end());

        cachedEffects.currentConfigEffects = currentConfigEffects;
        cachedEffects.defaultConfigEffects = defaultConfigEffects;
        cachedEffects.effectPaths = effectPaths;
        cachedEffects.configPath = pConfig->getConfigFilePath();
        cachedEffects.initialized = true;
    }

    // Appends slots for a longer effect chain. Only ever appends, so the images already returned
    // to the application from vkGetSwapchainImagesKHR keep their handles for the swapchain's life.
    bool growFakeSwapchainImages(LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain, size_t neededSlots)
    {
        if (neededSlots <= pLogicalSwapchain->maxEffectSlots)
            return true;

        const size_t hardLimit = static_cast<size_t>(std::max(1, settingsManager.getMaxEffects()));
        if (neededSlots > hardLimit)
        {
            Logger::warn("refusing to allocate room for " + std::to_string(neededSlots) + " effects, maxEffects is "
                         + std::to_string(hardLimit));
            return false;
        }

        const size_t   extraSlots  = neededSlots - pLogicalSwapchain->maxEffectSlots;
        const uint32_t extraImages = pLogicalSwapchain->imageCount * extraSlots;

        VkDeviceMemory       block = VK_NULL_HANDLE;
        std::vector<VkImage> grown =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, extraImages, block);

        if (grown.empty())
        {
            Logger::warn("could not allocate room for " + std::to_string(neededSlots) + " effects, keeping "
                         + std::to_string(pLogicalSwapchain->maxEffectSlots));
            return false;
        }

        const std::vector<VkImage> existingImages = pLogicalSwapchain->fakeImages;
        pLogicalSwapchain->fakeImageMemory.push_back(block);
        pLogicalSwapchain->fakeImages.insert(pLogicalSwapchain->fakeImages.end(), grown.begin(), grown.end());
        if (!appendOnlyHandleGrowthPreservesPrefix(existingImages, pLogicalSwapchain->fakeImages))
        {
            Logger::err("effect image-pool growth changed an existing image handle prefix");
            for (VkImage image : grown)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            freeTrackedMemory(pLogicalDevice, block, nullptr);
            pLogicalSwapchain->fakeImages = existingImages;
            pLogicalSwapchain->fakeImageMemory.pop_back();
            return false;
        }
        pLogicalSwapchain->maxEffectSlots = neededSlots;

        Logger::debug("grew the effect image pool to " + std::to_string(neededSlots) + " slots");
        return true;
    }

    struct EffectImagePoolCheckpoint
    {
        size_t imageCount = 0;
        size_t memoryBlockCount = 0;
        size_t maxEffectSlots = 0;
        std::vector<VkImage> imagePrefix;
    };

    EffectImagePoolCheckpoint checkpointEffectImagePool(const LogicalSwapchain* pLogicalSwapchain)
    {
        return {
            pLogicalSwapchain->fakeImages.size(),
            pLogicalSwapchain->fakeImageMemory.size(),
            pLogicalSwapchain->maxEffectSlots,
            pLogicalSwapchain->fakeImages,
        };
    }

    void rollbackEffectImagePool(
        LogicalDevice* pLogicalDevice, LogicalSwapchain* pLogicalSwapchain,
        const EffectImagePoolCheckpoint& checkpoint)
    {
        for (size_t i = checkpoint.imageCount; i < pLogicalSwapchain->fakeImages.size(); ++i)
            pLogicalDevice->vkd.DestroyImage(
                pLogicalDevice->device, pLogicalSwapchain->fakeImages[i], nullptr);
        for (size_t i = checkpoint.memoryBlockCount; i < pLogicalSwapchain->fakeImageMemory.size(); ++i)
            freeTrackedMemory(pLogicalDevice, pLogicalSwapchain->fakeImageMemory[i], nullptr);

        pLogicalSwapchain->fakeImages.resize(checkpoint.imageCount);
        pLogicalSwapchain->fakeImageMemory.resize(checkpoint.memoryBlockCount);
        pLogicalSwapchain->maxEffectSlots = checkpoint.maxEffectSlots;
        if (pLogicalSwapchain->fakeImages != checkpoint.imagePrefix)
            Logger::err("transaction rollback did not restore the original effect image handles");
        Logger::info("transaction rollback restored effect image pool to "
                     + std::to_string(checkpoint.maxEffectSlots) + " slots");
    }

    bool consumeTransactionFault(const std::string& stage)
    {
        const char* configured = std::getenv("VKBASALT_TEST_TRANSACTION_FAIL_STAGE");
        if (configured == nullptr || stage != configured)
            return false;

        static std::set<std::string> consumedStages;
        if (consumedStages.contains(stage))
            return false;
        consumedStages.insert(stage);
        Logger::warn("test fault injection at transaction stage: " + stage);
        return true;
    }

    EffectCreationSummary createEffectsForSwapchain(
        LogicalSwapchain* pLogicalSwapchain,
        LogicalDevice* pLogicalDevice,
        Config* pConfig,
        EffectCollection* pCollection,
        const std::vector<std::string>& effectStrings,
        bool checkEnabledState,
        bool disableFailedEffects)
    {
        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        VkFormat srgbFormat = convertToSRGB(pLogicalSwapchain->format);
        EffectCreationSummary summary;
        summary.requestedEffects = effectStrings.size();

        if (effectStrings.empty())
        {
            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin(),
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
            pCollection->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                firstImages, pLogicalSwapchain->images, pConfig)));
            return summary;
        }

        for (uint32_t i = 0; i < effectStrings.size(); i++)
        {
            Logger::debug("creating effect " + std::to_string(i) + ": " + effectStrings[i]);

            std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * i,
                                             pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1));

            std::vector<VkImage> secondImages;
            if (i == effectStrings.size() - 1)
            {
                secondImages = pLogicalDevice->supportsMutableFormat
                    ? pLogicalSwapchain->images
                    : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount,
                                           pLogicalSwapchain->fakeImages.end());
            }
            else
            {
                secondImages = std::vector<VkImage>(pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 1),
                                                    pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount * (i + 2));
            }

            bool effectFailed = effectRegistry.hasEffectFailed(effectStrings[i]);
            bool effectDisabled = checkEnabledState && !effectRegistry.isEffectEnabled(effectStrings[i]);

            if (effectFailed || effectDisabled)
            {
                Logger::debug("effect " + std::string(effectFailed ? "failed" : "disabled") + ", using pass-through: " + effectStrings[i]);
                ++summary.fallbackEffects;
                pCollection->effects.push_back(std::shared_ptr<Effect>(
                    new TransferEffect(pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                continue;
            }

            std::string effectType = effectRegistry.getEffectType(effectStrings[i]);
            if (effectType.empty())
                effectType = effectStrings[i];

            const auto* def = BuiltInEffects::instance().getDef(effectType);
            if (def)
            {
                for (auto* param : effectRegistry.getParametersForEffect(effectStrings[i]))
                {
                    auto serialized = param->serialize();
                    for (const auto& [suffix, value] : serialized)
                    {
                        std::string key = suffix.empty() ? param->name : (param->name + suffix);
                        pConfig->setOverride(key, value);
                    }
                }

                try
                {
                    ScopedAssertVulkanFailure failFast;
                    VkFormat format = def->usesSrgbFormat ? srgbFormat : unormFormat;
                    pCollection->effects.push_back(
                        def->factory(pLogicalDevice, format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig));
                    ++summary.constructedRequestedEffects;
                }
                catch (const std::exception& e)
                {
                    Logger::err("Failed to create built-in effect " + effectStrings[i] + ": " + e.what());
                    if (disableFailedEffects)
                        effectRegistry.setEffectError(effectStrings[i], e.what());
                    ++summary.fallbackEffects;
                    pCollection->effects.push_back(std::shared_ptr<Effect>(
                        new TransferEffect(pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                }
            }
            else
            {
                std::string effectPath = effectRegistry.getEffectFilePath(effectStrings[i]);
                auto customDefs = effectRegistry.getCompilePreprocessorDefs(effectStrings[i]);

                try
                {
                    ScopedAssertVulkanFailure failFast;
                    pCollection->effects.push_back(std::shared_ptr<Effect>(new ReshadeEffect(
                        pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                        pLogicalSwapchain->swapchainCreateInfo.imageColorSpace,
                        firstImages, secondImages, &effectRegistry, pCollection->sharedTexturePool,
                        effectStrings[i], effectPath, customDefs)));
                    ++summary.constructedRequestedEffects;
                }
                catch (const std::exception& e)
                {
                    Logger::err("Failed to create ReshadeEffect " + effectStrings[i] + ": " + e.what());
                    if (disableFailedEffects)
                        effectRegistry.setEffectError(effectStrings[i], e.what());
                    ++summary.fallbackEffects;
                    pCollection->effects.push_back(std::shared_ptr<Effect>(
                        new TransferEffect(pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig)));
                }
            }
        }

        if (!pLogicalDevice->supportsMutableFormat)
        {
            pCollection->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images, pConfig)));
        }

        return summary;
    }

    std::vector<VkFence> createSignaledEffectFences(
        LogicalDevice* pLogicalDevice, uint32_t count)
    {
        std::vector<VkFence> fences(count, VK_NULL_HANDLE);
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < count; ++i)
        {
            const VkResult result = pLogicalDevice->vkd.CreateFence(
                pLogicalDevice->device, &fenceInfo, nullptr, &fences[i]);
            if (result != VK_SUCCESS)
            {
                Logger::err("failed to create effect-collection fence " + std::to_string(i)
                            + ": " + std::to_string(result));
                for (VkFence created : fences)
                {
                    if (created != VK_NULL_HANDLE)
                        pLogicalDevice->vkd.DestroyFence(
                            pLogicalDevice->device, created, nullptr);
                }
                return {};
            }
        }
        return fences;
    }

    std::unique_ptr<EffectCollection> buildEffectCollection(
        LogicalSwapchain* pLogicalSwapchain,
        Config* pConfig,
        const std::vector<std::string>& effectStrings,
        bool requireComplete)
    {
        LogicalDevice* pLogicalDevice = pLogicalSwapchain->pLogicalDevice;
        auto collection = std::make_unique<EffectCollection>(pLogicalDevice);
        collection->generation = pLogicalSwapchain->nextEffectCollectionGeneration++;
        const DepthState depth = getDepthState(pLogicalDevice);
        collection->buildSpec = captureEffectCollectionBuildSpec(
            pLogicalSwapchain, pConfig, effectStrings, depth);
        collection->configSnapshot = std::make_shared<Config>(*pConfig);
        collection->sharedTexturePool =
            std::make_shared<SharedReshadeTexturePool>(pLogicalDevice);
        Config* pBuildConfig = collection->configSnapshot.get();

        const EffectCreationSummary creation = createEffectsForSwapchain(
            pLogicalSwapchain, pLogicalDevice, pBuildConfig, collection.get(),
            effectStrings, false, !requireComplete);
        if (requireComplete && !creation.allRequestedEffectsConstructed())
        {
            Logger::err("effect collection generation " + std::to_string(collection->generation)
                        + " was incomplete; retaining the live collection");
            return nullptr;
        }
        if (requireComplete && consumeTransactionFault("after-effects"))
            return nullptr;

        collection->defaultTransfer = std::shared_ptr<Effect>(new TransferEffect(
            pLogicalDevice,
            pLogicalSwapchain->format,
            pLogicalSwapchain->imageExtent,
            std::vector<VkImage>(
                pLogicalSwapchain->fakeImages.begin(),
                pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount),
            pLogicalSwapchain->images,
            pBuildConfig));

        collection->commandBuffersEffect = allocateCommandBuffer(
            pLogicalDevice, pLogicalSwapchain->imageCount);
        if (collection->commandBuffersEffect.size() != pLogicalSwapchain->imageCount
            || writeCommandBuffers(
                pLogicalDevice, collection->effects,
                depth.image, depth.imageView, depth.format,
                collection->commandBuffersEffect) != VK_SUCCESS)
        {
            Logger::err("effect collection generation " + std::to_string(collection->generation)
                        + " could not record its effect command buffers");
            return nullptr;
        }

        collection->commandBuffersNoEffect = allocateCommandBuffer(
            pLogicalDevice, pLogicalSwapchain->imageCount);
        if (collection->commandBuffersNoEffect.size() != pLogicalSwapchain->imageCount
            || writeCommandBuffers(
                pLogicalDevice, {collection->defaultTransfer},
                VK_NULL_HANDLE, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED,
                collection->commandBuffersNoEffect) != VK_SUCCESS)
        {
            Logger::err("effect collection generation " + std::to_string(collection->generation)
                        + " could not record its bypass command buffers");
            return nullptr;
        }
        if (requireComplete && consumeTransactionFault("after-command-recording"))
            return nullptr;

        collection->fences = createSignaledEffectFences(
            pLogicalDevice, pLogicalSwapchain->imageCount);
        collection->submissionsInFlight.assign(collection->fences.size(), false);
        if (requireComplete && consumeTransactionFault("after-fence-creation"))
            return nullptr;

        const size_t nullFences = static_cast<size_t>(std::count(
            collection->fences.begin(), collection->fences.end(), VK_NULL_HANDLE));
        const bool allEffectsPresent = std::all_of(
            collection->effects.begin(), collection->effects.end(),
            [](const std::shared_ptr<Effect>& effect) { return effect != nullptr; });
        const EffectCollectionReadiness readiness{
            creation.requestedEffects,
            creation.constructedRequestedEffects,
            creation.fallbackEffects,
            collection->effects.size(),
            collection->commandBuffersEffect.size(),
            collection->commandBuffersNoEffect.size(),
            collection->fences.size(),
            nullFences,
            pLogicalSwapchain->imageCount,
            collection->defaultTransfer != nullptr,
        };
        if (!allEffectsPresent
            || !effectCollectionUsable(readiness, requireComplete)
            || !collection->hasCompleteSubmissionTracking(pLogicalSwapchain->imageCount))
        {
            Logger::err("effect collection generation " + std::to_string(collection->generation)
                        + " failed its usability/submission-tracking contract");
            return nullptr;
        }
        return collection;
    }

    void collectRetiredEffectCollections(LogicalSwapchain* pLogicalSwapchain)
    {
        auto& retired = pLogicalSwapchain->retiredEffectCollections;
        for (auto it = retired.begin(); it != retired.end();)
        {
            const EffectCollectionRetirementStatus status = (*it)->retirementStatus();
            if (status == EffectCollectionRetirementStatus::Ready)
            {
                Logger::debug("retiring completed effect collection generation "
                              + std::to_string((*it)->generation));
                it = retired.erase(it);
            }
            else
            {
                if (status == EffectCollectionRetirementStatus::Error)
                    Logger::err("could not query retired effect-collection fences");
                ++it;
            }
        }
    }

    bool replacementOwnershipIsDisjoint(
        const LogicalSwapchain* target, const EffectCollection* replacement)
    {
        if (replacement == nullptr || replacement->sharedTexturePool == nullptr
            || !replacement->hasCompleteSubmissionTracking(target->imageCount))
            return false;

        const auto disjointFrom = [&](const std::unique_ptr<EffectCollection>& other) {
            return !other
                || (replacement->sharedTexturePool.get() != other->sharedTexturePool.get()
                    && handleSetsDisjoint(replacement->fences, other->fences));
        };
        if (!disjointFrom(target->activeEffectCollection))
            return false;
        for (const auto& retired : target->retiredEffectCollections)
        {
            if (!disjointFrom(retired))
                return false;
        }
        for (const auto& [_, swapchain] : swapchainMap)
        {
            if (!swapchain || swapchain.get() == target
                || swapchain->pLogicalDevice != target->pLogicalDevice)
                continue;
            if (!disjointFrom(swapchain->activeEffectCollection))
                return false;
            for (const auto& retired : swapchain->retiredEffectCollections)
            {
                if (!disjointFrom(retired))
                    return false;
            }
        }
        return true;
    }

    enum class EffectCollectionPublishResult
    {
        Published,
        Stale,
        Invalid,
    };

    EffectCollectionPublishResult publishEffectCollection(
        LogicalSwapchain* pLogicalSwapchain, Config* pBuildSource,
        std::unique_ptr<EffectCollection> replacement)
    {
        if (!replacementOwnershipIsDisjoint(pLogicalSwapchain, replacement.get()))
        {
            Logger::err("refusing to publish an effect collection with incomplete or shared ownership");
            return EffectCollectionPublishResult::Invalid;
        }

        const std::vector<std::string> desiredEffects = activeEffectNames();
        const EffectCollectionBuildSpec desiredSpec = captureEffectCollectionBuildSpec(
            pLogicalSwapchain, pBuildSource, desiredEffects,
            getDepthState(pLogicalSwapchain->pLogicalDevice));
        if (replacement->buildSpec != desiredSpec)
        {
            Logger::warn("discarding stale effect collection generation "
                         + std::to_string(replacement->generation)
                         + "; desired build inputs changed before publication");
            return EffectCollectionPublishResult::Stale;
        }

        const uint64_t generation = replacement->generation;
        commitReplacementPreservingLastGood(
            pLogicalSwapchain->activeEffectCollection,
            pLogicalSwapchain->retiredEffectCollections,
            std::move(replacement));
        Logger::info("published effect collection generation " + std::to_string(generation));
        collectRetiredEffectCollections(pLogicalSwapchain);
        return EffectCollectionPublishResult::Published;
    }

    bool reloadEffectsForSwapchain(
        LogicalSwapchain* pLogicalSwapchain, Config* pConfig,
        const std::vector<std::string>& activeEffects = {}, uint32_t staleRetry = 0)
    {
        LogicalDevice* pLogicalDevice = pLogicalSwapchain->pLogicalDevice;
        collectRetiredEffectCollections(pLogicalSwapchain);
        EffectCollection* const lastGood =
            pLogicalSwapchain->activeEffectCollection.get();
        const uint64_t lastGoodGeneration = lastGood ? lastGood->generation : 0;
        const VkDeviceSize stagingMemoryBaseline = trackedMemoryBytes();
        const auto proveLastGoodSurvived = [&] {
            const bool survived =
                pLogicalSwapchain->activeEffectCollection.get() == lastGood;
            if (survived)
            {
                Logger::info("last-good effect collection generation "
                             + std::to_string(lastGoodGeneration)
                             + " remains active after failed rebuild");
            }
            else
            {
                Logger::err("failed rebuild changed the active effect collection");
            }
            return survived;
        };
        const auto proveStagingMemoryRolledBack = [&] {
            const VkDeviceSize current = trackedMemoryBytes();
            if (current == stagingMemoryBaseline)
            {
                Logger::info("staged effect memory returned to the pre-transaction baseline");
                return true;
            }
            if (current > stagingMemoryBaseline)
            {
                Logger::err("failed transaction leaked "
                            + std::to_string(current - stagingMemoryBaseline)
                            + " bytes of tracked effect memory");
            }
            else
            {
                Logger::err("failed transaction released memory owned by the active generation");
            }
            return false;
        };

        const EffectImagePoolCheckpoint imagePoolCheckpoint =
            checkpointEffectImagePool(pLogicalSwapchain);
        const bool needsGrowth = activeEffects.size() > pLogicalSwapchain->maxEffectSlots;

        if (needsGrowth && consumeTransactionFault("image-growth-oom"))
            failNextTrackedAllocationForTest();
        if (needsGrowth
            && !growFakeSwapchainImages(
                pLogicalDevice, pLogicalSwapchain, activeEffects.size()))
        {
            clearTrackedAllocationFailureForTest();
            Logger::err("effect collection transaction could not reserve image slots; previous generation remains live");
            proveLastGoodSurvived();
            proveStagingMemoryRolledBack();
            Logger::err("effect collection transaction aborted; previous generation remains live");
            return false;
        }
        if (needsGrowth && consumeTransactionFault("after-image-growth"))
        {
            rollbackEffectImagePool(pLogicalDevice, pLogicalSwapchain, imagePoolCheckpoint);
            proveLastGoodSurvived();
            proveStagingMemoryRolledBack();
            Logger::err("effect collection transaction aborted; previous generation remains live");
            return false;
        }
        if (consumeTransactionFault("effect-allocation-oom"))
        {
            uint32_t allocationOrdinal = 1;
            const char* configuredOrdinal =
                std::getenv("VKBASALT_TEST_TRANSACTION_ALLOCATION_FAIL_INDEX");
            if (configuredOrdinal != nullptr && *configuredOrdinal != '\0')
            {
                char* end = nullptr;
                const unsigned long parsed = std::strtoul(
                    configuredOrdinal, &end, 10);
                if (end != configuredOrdinal && *end == '\0'
                    && parsed > 0 && parsed <= UINT32_MAX)
                    allocationOrdinal = static_cast<uint32_t>(parsed);
            }
            failTrackedAllocationForTest(allocationOrdinal);
            Logger::warn("test fault injection will fail tracked staging allocation #"
                         + std::to_string(allocationOrdinal));
        }

        Logger::info("transactionally rebuilding "
                     + std::to_string(activeEffects.size()) + " effects");
        const uint64_t startingConfigRevision = pConfig->getBuildStateRevision();
        const std::string startingConfigState = pConfig->getBuildStateSignature();
        const uint64_t startingRegistryRevision = effectRegistry.getBuildStateRevision();
        std::unique_ptr<EffectCollection> replacement;
        try
        {
            replacement = buildEffectCollection(
                pLogicalSwapchain, pConfig, activeEffects, true);
        }
        catch (const std::exception& e)
        {
            Logger::err("effect collection staging threw: " + std::string(e.what()));
        }
        catch (...)
        {
            Logger::err("effect collection staging threw an unknown error");
        }
        clearTrackedAllocationFailureForTest();

        if (!replacement)
        {
            if (needsGrowth)
                rollbackEffectImagePool(pLogicalDevice, pLogicalSwapchain, imagePoolCheckpoint);
            const bool desiredStateChanged =
                pConfig->getBuildStateRevision() != startingConfigRevision
                || pConfig->getBuildStateSignature() != startingConfigState
                || effectRegistry.getBuildStateRevision() != startingRegistryRevision;
            if (desiredStateChanged && staleRetry < 2)
            {
                proveLastGoodSurvived();
                proveStagingMemoryRolledBack();
                return reloadEffectsForSwapchain(
                    pLogicalSwapchain, pConfig, activeEffectNames(), staleRetry + 1);
            }
            proveLastGoodSurvived();
            proveStagingMemoryRolledBack();
            Logger::err("effect collection transaction aborted; previous generation remains live");
            return false;
        }

        const char* staleBuildTest = std::getenv("VKBASALT_TEST_TRANSACTION_STALE_ONCE");
        static bool staleBuildTestConsumed = false;
        if (!staleBuildTestConsumed && staleBuildTest != nullptr
            && std::string(staleBuildTest) == "1")
        {
            staleBuildTestConsumed = true;
            pConfig->setOverride("__vkbasalt_test_build_spec_revision", "1");
            Logger::warn("test fault injection changed desired config state before publication");
        }

        const EffectCollectionPublishResult result = publishEffectCollection(
            pLogicalSwapchain, pConfig, std::move(replacement));
        if (result != EffectCollectionPublishResult::Published)
        {
            if (needsGrowth)
                rollbackEffectImagePool(pLogicalDevice, pLogicalSwapchain, imagePoolCheckpoint);
            if (result == EffectCollectionPublishResult::Stale && staleRetry < 2)
            {
                proveLastGoodSurvived();
                proveStagingMemoryRolledBack();
                Logger::info("restarting transaction from the newest desired state");
                return reloadEffectsForSwapchain(
                    pLogicalSwapchain, pConfig, activeEffectNames(), staleRetry + 1);
            }
            proveLastGoodSurvived();
            proveStagingMemoryRolledBack();
            Logger::err("effect collection transaction aborted at publication; previous generation remains live");
            return false;
        }
        Logger::info("effect collection transaction committed successfully");
        return true;
    }

    bool reloadAllSwapchains(
        LogicalDevice* pLogicalDevice, const std::vector<std::string>& activeEffects)
    {
        bool allSucceeded = true;
        for (auto& [_, pLogicalSwapchain] : swapchainMap)
        {
            if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice
                && !pLogicalSwapchain->fakeImages.empty())
            {
                allSucceeded = reloadEffectsForSwapchain(
                    pLogicalSwapchain.get(), pConfig.get(), activeEffects)
                    && allSucceeded;
            }
        }
        return allSucceeded;
    }

    void updateOverlayState(LogicalDevice* pLogicalDevice, bool effectsEnabled)
    {
        if (!pLogicalDevice->imguiOverlay || !pLogicalDevice->imguiOverlay->isVisible())
            return;

        OverlayState overlayState;
        overlayState.effectNames = pLogicalDevice->imguiOverlay->getActiveEffects();

        // This walks every configured shader directory and re-reads the shader manager config.
        // The overlay asks for it once a frame, but the directories do not change that fast.
        static std::vector<std::string> cachedCurrentConfigEffects;
        static std::vector<std::string> cachedDefaultConfigEffects;
        static std::map<std::string, std::string> cachedEffectPaths;
        static std::chrono::steady_clock::time_point lastEffectScan;

        const auto now = std::chrono::steady_clock::now();
        if (lastEffectScan == std::chrono::steady_clock::time_point{}
            || now - lastEffectScan >= std::chrono::milliseconds(500))
        {
            cachedCurrentConfigEffects.clear();
            cachedDefaultConfigEffects.clear();
            cachedEffectPaths.clear();
            getAvailableEffects(pConfig.get(), cachedCurrentConfigEffects,
                                cachedDefaultConfigEffects, cachedEffectPaths);
            lastEffectScan = now;
        }

        overlayState.currentConfigEffects = cachedCurrentConfigEffects;
        overlayState.defaultConfigEffects = cachedDefaultConfigEffects;
        overlayState.effectPaths = cachedEffectPaths;
        overlayState.configPath = pConfig->getConfigFilePath();

        static std::string cachedConfigPath;
        static std::string cachedConfigName;
        if (overlayState.configPath != cachedConfigPath)
        {
            cachedConfigPath = overlayState.configPath;
            cachedConfigName = std::filesystem::path(cachedConfigPath).filename().string();
        }
        overlayState.configName = cachedConfigName;
        overlayState.effectsEnabled = effectsEnabled;

        for (const auto& effectName : pLogicalDevice->imguiOverlay->getSelectedEffects())
        {
            if (effectRegistry.hasEffect(effectName))
                continue;
            auto pathIt = overlayState.effectPaths.find(effectName);
            std::string effectPath = (pathIt != overlayState.effectPaths.end()) ? pathIt->second : "";
            effectRegistry.ensureEffect(effectName, effectPath);
        }

        pLogicalDevice->imguiOverlay->updateState(std::move(overlayState));
    }

    VkResult submitOverlayFrame(LogicalDevice* pLogicalDevice, LogicalSwapchain* pSwapchain,
                                uint32_t index, VkSemaphore& outSemaphore)
    {
        outSemaphore = pSwapchain->semaphores[index];

        if (!pLogicalDevice->imguiOverlay)
            return VK_SUCCESS;

        VkCommandBuffer overlayCmd = pLogicalDevice->imguiOverlay->recordFrame(
            index, pSwapchain->imageViews[index],
            pSwapchain->imageExtent.width, pSwapchain->imageExtent.height);

        if (overlayCmd == VK_NULL_HANDLE)
            return VK_SUCCESS;

        VkPipelineStageFlags overlayWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo overlaySubmit = {};
        overlaySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        overlaySubmit.waitSemaphoreCount = 1;
        overlaySubmit.pWaitSemaphores = &pSwapchain->semaphores[index];
        overlaySubmit.pWaitDstStageMask = &overlayWaitStage;
        overlaySubmit.commandBufferCount = 1;
        overlaySubmit.pCommandBuffers = &overlayCmd;
        overlaySubmit.signalSemaphoreCount = 1;
        overlaySubmit.pSignalSemaphores = &pSwapchain->overlaySemaphores[index];

        VkFence overlayFence = pLogicalDevice->imguiOverlay->getCommandBufferFence(index);
        VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &overlaySubmit, overlayFence);
        if (vr == VK_SUCCESS)
            outSemaphore = pSwapchain->overlaySemaphores[index];

        return vr;
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance*                  pInstance)
    {
        VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;

        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }

        Logger::trace("vkCreateInstance");

        // The enabled surface extension is the reliable backend signal: XWayland
        // clients inherit WAYLAND_DISPLAY, so the env var alone misroutes them.
        // A client enabling both extensions is left undetermined here and
        // resolved by whichever surface it actually creates; both the Wayland
        // and the Xlib/Xcb entry points record that choice.
        {
            bool wantsWayland = false;
            bool wantsX11     = false;
            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
            {
                const char* ext = pCreateInfo->ppEnabledExtensionNames[i];
                if (!ext)
                    continue;
                if (!std::strcmp(ext, "VK_KHR_wayland_surface"))
                    wantsWayland = true;
                else if (!std::strcmp(ext, "VK_KHR_xlib_surface") || !std::strcmp(ext, "VK_KHR_xcb_surface"))
                    wantsX11 = true;
            }
            if (wantsX11 && !wantsWayland)
                setX11Surface();
        }

        if (layerCreateInfo == nullptr)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance) gpa(VK_NULL_HANDLE, "vkCreateInstance");

        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
        VkApplicationInfo    appInfo;
        if (modifiedCreateInfo.pApplicationInfo)
        {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_1)
            {
                appInfo.apiVersion = VK_API_VERSION_1_1;
            }
        }
        else
        {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_1;
        }

        modifiedCreateInfo.pApplicationInfo = &appInfo;
        VkResult ret                        = createFunc(&modifiedCreateInfo, pAllocator, pInstance);

        if (ret != VK_SUCCESS)
            return ret;

        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        {
            scoped_lock l(globalLock);
            instanceDispatchMap[GetKey(*pInstance)] = dispatchTable;
            instanceMap[GetKey(*pInstance)]         = *pInstance;
            instanceVersionMap[GetKey(*pInstance)]  = modifiedCreateInfo.pApplicationInfo->apiVersion;
        }

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyInstance");

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];

        dispatchTable.DestroyInstance(instance, pAllocator);

        instanceDispatchMap.erase(GetKey(instance));
        instanceMap.erase(GetKey(instance));
        instanceVersionMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(VkPhysicalDevice             physicalDevice,
                                              const VkDeviceCreateInfo*    pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice*                    pDevice)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr)
        {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice) gipa(VK_NULL_HANDLE, "vkCreateDevice");

        uint32_t extensionCount = 0;

        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat = false;
        bool supportsSwapchain     = false;
        bool supportsMaintenance2  = false;
        for (VkExtensionProperties properties : extensionProperties)
        {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format"))
            {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
            }
            else if (properties.extensionName == std::string("VK_KHR_swapchain"))
            {
                supportsSwapchain = true;
            }
            else if (properties.extensionName == std::string("VK_KHR_maintenance2"))
            {
                supportsMaintenance2 = true;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo       modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount)
        {
            enabledExtensionNames = std::vector<const char*>(modifiedCreateInfo.ppEnabledExtensionNames,
                                                             modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        if (supportsSwapchain)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain");
        }
        if (supportsMutableFormat && supportsSwapchain)
        {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
            if (deviceProps.apiVersion < VK_API_VERSION_1_1 && supportsMaintenance2)
            {
                addUniqueCString(enabledExtensionNames, "VK_KHR_maintenance2");
            }
        }
        else
        {
            supportsMutableFormat = false;
        }
        if (deviceProps.apiVersion < VK_API_VERSION_1_2 || instanceVersionMap[GetKey(physicalDevice)] < VK_API_VERSION_1_2)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        VkPhysicalDeviceFeatures supportedFeatures = {};
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

        VkPhysicalDeviceFeatures2* appFeatures2 = nullptr;
        for (VkBaseOutStructure* s = (VkBaseOutStructure*) modifiedCreateInfo.pNext; s; s = s->pNext)
        {
            if (s->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            {
                appFeatures2 = (VkPhysicalDeviceFeatures2*) s;
                break;
            }
        }

        VkPhysicalDeviceFeatures deviceFeatures = {};
        bool                     ownEnabledFeatures = false;

        using FeatureField = VkBool32 VkPhysicalDeviceFeatures::*;
        auto requestFeature = [&](FeatureField field, const char* name, const char* consequence) {
            const bool alreadyRequested = appFeatures2
                                              ? appFeatures2->features.*field == VK_TRUE
                                              : (modifiedCreateInfo.pEnabledFeatures && modifiedCreateInfo.pEnabledFeatures->*field == VK_TRUE);
            if (alreadyRequested)
                return true;

            if (!(supportedFeatures.*field))
            {
                Logger::warn(std::string("device does not support ") + name + " -- " + consequence);
                return false;
            }

            if (appFeatures2)
            {
                appFeatures2->features.*field = VK_TRUE;
                return true;
            }

            if (!ownEnabledFeatures)
            {
                if (modifiedCreateInfo.pEnabledFeatures)
                    deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
                modifiedCreateInfo.pEnabledFeatures = &deviceFeatures;
                ownEnabledFeatures                  = true;
            }
            deviceFeatures.*field = VK_TRUE;
            return true;
        };

        requestFeature(&VkPhysicalDeviceFeatures::shaderImageGatherExtended,
                       "shaderImageGatherExtended",
                       "ReShade effects that gather with offsets will fail to compile");

        const bool storageWrite = requestFeature(&VkPhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat,
                                                 "shaderStorageImageWriteWithoutFormat",
                                                 "ReShade compute effects will be refused");
        const bool storageRead  = requestFeature(&VkPhysicalDeviceFeatures::shaderStorageImageReadWithoutFormat,
                                                "shaderStorageImageReadWithoutFormat",
                                                "ReShade compute effects will be refused");

        VkResult ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

        if (ret != VK_SUCCESS)
            return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice(new LogicalDevice());
        pLogicalDevice->vki                   = instanceDispatchMap[GetKey(physicalDevice)];
        pLogicalDevice->device                = *pDevice;
        pLogicalDevice->physicalDevice        = physicalDevice;
        pLogicalDevice->supportsStorageImageWithoutFormat = storageWrite && storageRead;
        pLogicalDevice->instance              = instanceMap[GetKey(physicalDevice)];
        pLogicalDevice->queue                 = VK_NULL_HANDLE;
        pLogicalDevice->queueFamilyIndex      = 0;
        pLogicalDevice->commandPool           = VK_NULL_HANDLE;
        pLogicalDevice->supportsMutableFormat = supportsMutableFormat;

        fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);

        uint32_t count;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, queueProperties.data());
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = 0;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue)
        {
            Logger::err("Did not find a graphics queue! vkBasalt requires a graphics-capable queue.");
        }

        deviceMap[GetKey(*pDevice)] = pLogicalDevice;

        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        // Establish completion once before releasing any swapchain generation,
        // overlay resource, depth view, or the shared command pool. Vulkan apps
        // normally destroy swapchains first, but device teardown must not rely
        // on that convention for generation-owned command buffers.
        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
        for (auto it = swapchainMap.begin(); it != swapchainMap.end();)
        {
            if (it->second && it->second->pLogicalDevice == pLogicalDevice)
            {
                it->second->destroy(true);
                it = swapchainMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        pLogicalDevice->imguiOverlay.reset();

        for (const auto& depthImage : pLogicalDevice->depthImages)
            if (depthImage.view != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(device, depthImage.view, nullptr);
        pLogicalDevice->depthImages.clear();

        // The keyboard and pointer proxies live on the shared event queue, so the
        // queue is released here, after both have let go of it -- not by whichever
        // of them happened to be cleaned up first.
        cleanupWaylandKeyboard();
        cleanupWaylandMouse();
        cleanupWaylandInputCommon();

        if (pLogicalDevice->commandPool != VK_NULL_HANDLE)
        {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, pAllocator);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        deviceMap.erase(GetKey(device));
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice                        device,
                                                               const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                               const VkAllocationCallbacks*    pAllocator,
                                                               VkSwapchainKHR*                 pSwapchain)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        if (pLogicalDevice->supportsMutableFormat)
        {
            // OR our usage into the app's instead of replacing it: the app and
            // any layer below us (lsfg-vk adds TRANSFER_SRC/DST to blit frames
            // for generation) may rely on bits a replacement would silently drop.
            modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                             | VK_IMAGE_USAGE_SAMPLED_BIT; // we want to use the swapchain images as output of the graphics pipeline
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            // TODO what if the application already uses multiple formats for the swapchain?

            imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
            imageFormatListCreateInfo.pNext           = modifiedCreateInfo.pNext;
            imageFormatListCreateInfo.viewFormatCount = (srgbFormat == unormFormat) ? 1 : 2;
            imageFormatListCreateInfo.pViewFormats    = formats;

            modifiedCreateInfo.pNext = &imageFormatListCreateInfo;
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        Logger::debug("swapchain create: app usage=" + convertToString(pCreateInfo->imageUsage)
                      + " our usage=" + convertToString(modifiedCreateInfo.imageUsage)
                      + " flags=" + convertToString(modifiedCreateInfo.flags)
                      + " (decimal bit values)"
                      + " minImageCount=" + std::to_string(modifiedCreateInfo.minImageCount)
                      + " presentMode=" + std::to_string(modifiedCreateInfo.presentMode)
                      + " extent=" + std::to_string(modifiedCreateInfo.imageExtent.width) + "x"
                      + std::to_string(modifiedCreateInfo.imageExtent.height));

        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain(new LogicalSwapchain());
        pLogicalSwapchain->pLogicalDevice      = pLogicalDevice;
        pLogicalSwapchain->swapchainCreateInfo = *pCreateInfo;
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->format              = modifiedCreateInfo.imageFormat;
        pLogicalSwapchain->imageCount          = 0;

        VkResult result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);

        swapchainMap[*pSwapchain] = pLogicalSwapchain;

        return result;
    }

    // Compiling an effect costs a measured 4.4ms at the median and 214ms at worst. Doing it under
    // globalLock blocks every other intercepted call, including the vkCreateImage a texture thread
    // makes once depth capture is on. The compile cache carries its own mutex, so filling it with
    // no lock held leaves the compile under the lock a cache hit. A miss is harmless: it compiles
    // under the lock as before, so correctness never depends on any of this having run.
    struct EffectCompileRequest
    {
        std::string                                      shaderPath;
        std::vector<std::pair<std::string, std::string>> defines;
        bool                                             relaxMinPrecision = false;
    };

    // Caller HOLDS globalLock.
    std::vector<EffectCompileRequest> gatherEffectCompileRequests(const std::vector<std::string>& effectNames,
                                                                 VkExtent2D                      extent,
                                                                 VkFormat                        unormFormat,
                                                                 VkColorSpaceKHR                 colorSpace)
    {
        std::vector<EffectCompileRequest> requests;

        for (const std::string& name : effectNames)
        {
            std::string path = effectRegistry.getEffectFilePath(name);
            if (path.empty())
                continue;

            requests.push_back({std::move(path),
                                reshadeCompileDefines(extent, unormFormat, colorSpace, effectRegistry.getPreprocessorDefs(name)),
                                effectRegistry.getAllowHalfPrecision(name)});
        }

        return requests;
    }

    // Caller HOLDS NO lock.
    void runEffectCompileWarmUp(const std::vector<EffectCompileRequest>& requests)
    {
        if (requests.empty())
            return;

        const std::vector<std::string> includePaths = ConfigSerializer::loadShaderManagerConfig().discoveredShaderPaths;

        for (const EffectCompileRequest& request : requests)
        {
            try
            {
                getOrCompileReshadeEffect(request.shaderPath, request.defines, includePaths, request.relaxMinPrecision);
            }
            catch (const std::exception& e)
            {
                Logger::debug(std::string("compile cache warm-up skipped an effect: ") + e.what());
            }
        }
    }

    void warmEffectCompileCache(VkSwapchainKHR swapchain)
    {
        std::vector<EffectCompileRequest> requests;

        {
            scoped_lock l(globalLock);

            const auto it = swapchainMap.find(swapchain);
            if (it == swapchainMap.end() || !it->second)
                return;

            LogicalSwapchain* pLogicalSwapchain = it->second.get();
            if (!pLogicalSwapchain->fakeImages.empty())
                return;

            requests = gatherEffectCompileRequests(activeEffectNames(),
                                                   pLogicalSwapchain->imageExtent,
                                                   convertToUNORM(pLogicalSwapchain->format),
                                                   pLogicalSwapchain->swapchainCreateInfo.imageColorSpace);
        }

        runEffectCompileWarmUp(requests);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice       device,
                                                                  VkSwapchainKHR swapchain,
                                                                  uint32_t*      pCount,
                                                                  VkImage*       pSwapchainImages)
    {
        if (pSwapchainImages != nullptr)
            warmEffectCompileCache(swapchain);

        scoped_lock l(globalLock);
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        if (pSwapchainImages == nullptr)
        {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

        if (pLogicalSwapchain->fakeImages.size())
        {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, nullptr);
        pLogicalSwapchain->images.resize(pLogicalSwapchain->imageCount);
        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, pLogicalSwapchain->images.data());

        // A frame-generation layer below us bumps minImageCount for its extra
        // acquires; this delta in the log proves which side of lsfg-vk we are on.
        Logger::debug("down-chain swapchain imageCount=" + std::to_string(pLogicalSwapchain->imageCount)
                      + " (app requested minImageCount=" + std::to_string(pLogicalSwapchain->swapchainCreateInfo.minImageCount) + ")");

        pLogicalSwapchain->imageViews.resize(pLogicalSwapchain->imageCount);
        for (uint32_t i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = pLogicalSwapchain->images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = pLogicalSwapchain->format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            VkResult viewResult = pLogicalDevice->vkd.CreateImageView(pLogicalDevice->device, &viewInfo, nullptr, &pLogicalSwapchain->imageViews[i]);
            if (viewResult != VK_SUCCESS)
                Logger::err("Failed to create swapchain image view " + std::to_string(i) + ": " + std::to_string(viewResult));
        }

        bool isFirstRun = !effectRegistry.isInitializedFromConfig();
        if (isFirstRun)
            effectRegistry.initializeSelectedEffectsFromConfig();

        const std::vector<std::string> activeEffects = activeEffectNames();

        // Only what the current chain needs. Adding effects later grows this instead of reserving
        // the maximum up front, which at 4K reserved most of a gigabyte to hold effects nobody had
        // selected. growFakeSwapchainImages only ever appends, so the images already handed to the
        // application keep their handles.
        size_t effectSlots = std::max<size_t>(activeEffects.size(), 1);
        pLogicalSwapchain->maxEffectSlots = effectSlots;

        VkDeviceMemory fakeImageBlock = VK_NULL_HANDLE;
        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * (effectSlots + !pLogicalDevice->supportsMutableFormat);

        pLogicalSwapchain->fakeImages =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, fakeImageBlock);
        if (fakeImageBlock != VK_NULL_HANDLE)
            pLogicalSwapchain->fakeImageMemory.push_back(fakeImageBlock);
        Logger::debug("created fake swapchain images");

        if (pLogicalSwapchain->fakeImages.empty())
        {
            // The allocation was abandoned, so there is nothing to render into.
            // Leave the application on the real swapchain images: no effects and
            // no overlay, but a correct picture instead of a black window.
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->images.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        std::vector<std::string> initialEffects = activeEffects;
        if (!isFirstRun && !activeEffects.empty())
        {
            Logger::debug("using pass-through during resize, will restore effects after debounce");
            initialEffects.clear();
            resizeDebounce.pending = true;
            resizeDebounce.lastResizeTime = std::chrono::steady_clock::now();
        }

        pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        pLogicalSwapchain->overlaySemaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        Logger::debug("created semaphores");

        try
        {
            pLogicalSwapchain->activeEffectCollection = buildEffectCollection(
                pLogicalSwapchain, pConfig.get(), initialEffects, false);
        }
        catch (const std::exception& e)
        {
            Logger::err("initial effect collection construction failed: " + std::string(e.what()));
        }
        if (!pLogicalSwapchain->activeEffectCollection)
        {
            Logger::err("could not construct a usable initial effect collection; passing real swapchain images through");
            for (VkImage image : pLogicalSwapchain->fakeImages)
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, nullptr);
            for (VkDeviceMemory memory : pLogicalSwapchain->fakeImageMemory)
                freeTrackedMemory(pLogicalDevice, memory, nullptr);
            pLogicalSwapchain->fakeImages.clear();
            pLogicalSwapchain->fakeImageMemory.clear();
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->images.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        Logger::debug("active effect count: " + std::to_string(initialEffects.size()));
        Logger::debug("effect object count: "
                      + std::to_string(pLogicalSwapchain->activeEffectCollection->effects.size()));
        Logger::trace("vkGetSwapchainImagesKHR");

        if (!pLogicalDevice->imguiOverlay)
        {
            if (!pLogicalDevice->overlayPersistentState)
                pLogicalDevice->overlayPersistentState = std::make_unique<OverlayPersistentState>();
            pLogicalDevice->imguiOverlay = std::make_unique<ImGuiOverlay>(
                pLogicalDevice, pLogicalSwapchain->format, pLogicalSwapchain->imageCount,
                pLogicalDevice->overlayPersistentState.get());
            pLogicalDevice->imguiOverlay->setEffectRegistry(&effectRegistry);

            pLogicalDevice->imguiOverlay->setGameProfile(detectedGameName, activeProfileName, activeProfilePath);

            static bool inputBlockerInited = false;
            if (!inputBlockerInited)
            {
                initInputBlocker(settingsManager.getOverlayBlockInput());
                inputBlockerInited = true;
            }
        }

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        std::unique_lock<std::mutex> l(globalLock);

        // Mark new input frame so dispatch deduplication resets
        beginWaylandInputFrame();
        beginMouseInputFrame();
        beginReshadeInputFrame();

        // Guard: if no device for this queue, pass through
        auto devIt = deviceMap.find(GetKey(queue));
        if (devIt == deviceMap.end() || !devIt->second)
            return VK_ERROR_DEVICE_LOST;
        if (!devIt->second->queue)
            return devIt->second->vkd.QueuePresentKHR(queue, pPresentInfo);

        static uint32_t keySymbol = convertToKeySym(settingsManager.getToggleKey());
        static uint32_t reloadKeySymbol = convertToKeySym(settingsManager.getReloadKey());
        static uint32_t overlayKeySymbol = convertToKeySym(settingsManager.getOverlayKey());
        static bool initLogged = false;

        static bool pressed       = false;
        static bool presentEffect = settingsManager.getEnableOnLaunch();
        static bool reloadPressed = false;
        static bool overlayPressed = false;

        LogicalDevice* pDeviceForSettings = devIt->second.get();
        if (pDeviceForSettings && pDeviceForSettings->imguiOverlay && pDeviceForSettings->imguiOverlay->hasSettingsSaved())
        {
            keySymbol = convertToKeySym(settingsManager.getToggleKey());
            reloadKeySymbol = convertToKeySym(settingsManager.getReloadKey());
            overlayKeySymbol = convertToKeySym(settingsManager.getOverlayKey());
            initInputBlocker(settingsManager.getOverlayBlockInput());
            pDeviceForSettings->imguiOverlay->clearSettingsSaved();
            Logger::info("Settings reloaded from SettingsManager");
        }

        if (pDeviceForSettings && pDeviceForSettings->imguiOverlay && pDeviceForSettings->imguiOverlay->hasShaderPathsChanged())
        {
            cachedEffects.initialized = false;  // Force re-scan of available effects
            pDeviceForSettings->imguiOverlay->clearShaderPathsChanged();
            Logger::info("Shader paths changed, effect list refreshed");
        }

        if (!initLogged)
        {
            Logger::info("hot-reload initialized, config: " + pConfig->getConfigFilePath());
            initLogged = true;
        }

        if (handleKeyPress(keySymbol, pressed))
            presentEffect = !presentEffect;

        bool shouldReload = false;
        std::vector<std::string> testActiveEffectOverride;
        if (handleKeyPress(reloadKeySymbol, reloadPressed))
        {
            Logger::debug("reload key pressed");
            shouldReload = true;
        }
        if (pConfig->hasConfigChanged())
        {
            Logger::debug("config file changed detected");
            shouldReload = true;
        }

        static const uint64_t testTransactionReloadEvery = [] {
            const char* value = std::getenv("VKBASALT_TEST_TRANSACTION_RELOAD_EVERY");
            if (value == nullptr || *value == '\0')
                return uint64_t{0};
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(value, &end, 10);
            return end != value && *end == '\0'
                ? static_cast<uint64_t>(parsed) : uint64_t{0};
        }();
        static const std::vector<std::vector<std::string>> testEffectSequence = [] {
            std::vector<std::vector<std::string>> result;
            const char* value = std::getenv("VKBASALT_TEST_TRANSACTION_EFFECT_SEQUENCE");
            if (value == nullptr || *value == '\0')
                return result;

            std::stringstream collections(value);
            std::string collectionValue;
            while (std::getline(collections, collectionValue, ';'))
            {
                std::vector<std::string> collection;
                std::stringstream names(collectionValue);
                std::string name;
                while (std::getline(names, name, ':'))
                {
                    if (!name.empty())
                        collection.push_back(name);
                }
                if (!collection.empty())
                    result.push_back(std::move(collection));
            }
            return result;
        }();
        static uint64_t testPresentCycle = 0;
        static size_t testEffectSequenceIndex = 1;
        ++testPresentCycle;
        if (testTransactionReloadEvery != 0
            && testPresentCycle % testTransactionReloadEvery == 0)
        {
            if (!testEffectSequence.empty())
            {
                testActiveEffectOverride = testEffectSequence[
                    testEffectSequenceIndex % testEffectSequence.size()];
                ++testEffectSequenceIndex;
            }
            Logger::info("test-triggered effect collection transaction at present cycle "
                         + std::to_string(testPresentCycle));
            shouldReload = true;
        }

        if (handleKeyPress(overlayKeySymbol, overlayPressed))
        {
            LogicalDevice* pDevice = deviceMap[GetKey(queue)].get();
            if (pDevice->imguiOverlay)
                pDevice->imguiOverlay->toggle();
        }
        else if (inputFocusLost())
        {
            // The overlay holds the keyboard and the cursor, and an X11 grab
            // outlives the focus change that took the user elsewhere.
            LogicalDevice* pDevice = deviceMap[GetKey(queue)].get();
            if (pDevice->imguiOverlay && pDevice->imguiOverlay->isVisible())
            {
                Logger::debug("focus left the application, closing the overlay and releasing input");
                pDevice->imguiOverlay->toggle();
            }
        }

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(queue)].get();

        if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->hasToggleEffectsRequest())
        {
            presentEffect = !presentEffect;
            pLogicalDevice->imguiOverlay->clearToggleEffectsRequest();
        }

        if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->hasModifiedParams())
        {
            pLogicalDevice->imguiOverlay->clearApplyRequest();
            shouldReload = true;
        }

        if (shouldReload)
        {
            Logger::info("hot-reloading config and effects...");

            if (pLogicalDevice->imguiOverlay && pLogicalDevice->imguiOverlay->hasPendingConfig())
            {
                std::string newConfigPath = pLogicalDevice->imguiOverlay->getPendingConfigPath();
                switchConfig(newConfigPath);
                std::vector<std::string> newEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
                std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});
                pLogicalDevice->imguiOverlay->setSelectedEffects(newEffects, disabledEffects);
                pLogicalDevice->imguiOverlay->clearPendingConfig();
                pLogicalDevice->imguiOverlay->markDirty();  // Defer reload via debounce
            }
            else
            {
                pConfig->reload();
                cachedEffects.initialized = false;
                cachedParams.dirty = true;

                std::vector<std::string> activeEffects = pLogicalDevice->imguiOverlay
                    ? pLogicalDevice->imguiOverlay->getActiveEffects()
                    : activeEffectNames();

                if (!testActiveEffectOverride.empty())
                {
                    const std::set<std::string> activeSet(
                        testActiveEffectOverride.begin(), testActiveEffectOverride.end());
                    for (const std::string& effectName : effectRegistry.getSelectedEffects())
                        effectRegistry.setEffectEnabled(
                            effectName, activeSet.contains(effectName));
                    activeEffects = testActiveEffectOverride;
                }

                // A reload is where a newly added effect is compiled for the first time, so this is
                // the cold path. Compile it before the reload rather than during, with the lock let
                // go in between.
                std::vector<EffectCompileRequest> requests;
                for (const auto& [_, pSwapchain] : swapchainMap)
                {
                    if (!pSwapchain || pSwapchain->fakeImages.empty())
                        continue;

                    std::vector<EffectCompileRequest> forSwapchain = gatherEffectCompileRequests(
                        activeEffects, pSwapchain->imageExtent, convertToUNORM(pSwapchain->format),
                        pSwapchain->swapchainCreateInfo.imageColorSpace);
                    requests.insert(requests.end(), forSwapchain.begin(), forSwapchain.end());
                }

                if (!requests.empty())
                {
                    l.unlock();
                    runEffectCompileWarmUp(requests);
                    l.lock();

                    // The device could have gone away while the lock was released. Presenting to a
                    // destroyed device is not possible and reporting success for it would be a lie.
                    const auto deviceIt = deviceMap.find(GetKey(queue));
                    if (deviceIt == deviceMap.end() || deviceIt->second.get() != pLogicalDevice)
                    {
                        Logger::err("device was destroyed while effects were being compiled");
                        return VK_ERROR_DEVICE_LOST;
                    }
                }

                reloadAllSwapchains(pLogicalDevice, activeEffects);
            }
        }

        if (resizeDebounce.pending)
        {
            auto resizeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - resizeDebounce.lastResizeTime).count();

            if (resizeElapsed >= RESIZE_DEBOUNCE_MS)
            {
                Logger::info("debounced resize reload after " + std::to_string(resizeElapsed) + "ms");
                resizeDebounce.pending = false;

                const std::vector<std::string> activeEffects = activeEffectNames();
                for (auto& [_, pSwapchain] : swapchainMap)
                {
                    if (pSwapchain->fakeImages.empty())
                        continue;
                    reloadEffectsForSwapchain(pSwapchain.get(), pConfig.get(), activeEffects);
                }
            }
        }

        static thread_local std::vector<VkSemaphore> presentSemaphores;
        static thread_local std::vector<VkPipelineStageFlags> waitStages;
        presentSemaphores.clear();
        presentSemaphores.reserve(pPresentInfo->swapchainCount);
        waitStages.assign(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        updateOverlayState(pLogicalDevice, presentEffect);

        bool appWaitsConsumed = false;

        for (unsigned int i = 0; i < pPresentInfo->swapchainCount; i++)
        {
            uint32_t          index             = pPresentInfo->pImageIndices[i];
            VkSwapchainKHR    swapchain         = pPresentInfo->pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();
            collectRetiredEffectCollections(pLogicalSwapchain);
            EffectCollection* pCollection = pLogicalSwapchain->activeEffectCollection.get();

            // Nothing was set up for this swapchain -- the image allocation was
            // abandoned, so the application is presenting the real images itself
            // and there is nothing of ours to submit.
            if (pLogicalSwapchain->fakeImages.empty() || pCollection == nullptr
                || index >= pCollection->commandBuffersEffect.size()
                || index >= pCollection->commandBuffersNoEffect.size()
                || index >= pCollection->fences.size())
            {
                continue;
            }

            if (presentEffect)
            {
                for (auto& effect : pCollection->effects)
                    effect->updateEffect();
            }

            // This command buffer is about to be submitted again, so the previous submission of it
            // must have finished. Re-acquiring the image usually means it already has, making this
            // wait free, but "usually" is not a synchronisation guarantee.
            VkFence effectFence = pCollection->fences[index];
            if (effectFence == VK_NULL_HANDLE)
            {
                Logger::err("active effect collection has a null submission fence");
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (pCollection->submissionsInFlight[index])
            {
                const VkResult waitResult = pLogicalDevice->vkd.WaitForFences(
                    pLogicalDevice->device, 1, &effectFence, VK_TRUE, UINT64_MAX);
                if (waitResult != VK_SUCCESS)
                {
                    Logger::err("effect fence wait failed for image " + std::to_string(index));
                    return waitResult;
                }
                pCollection->markSubmissionComplete(index);
            }
            const VkResult resetResult = pLogicalDevice->vkd.ResetFences(
                pLogicalDevice->device, 1, &effectFence);
            if (resetResult != VK_SUCCESS)
            {
                Logger::err("effect fence reset failed for image " + std::to_string(index));
                return resetResult;
            }

            VkSubmitInfo submitInfo = {};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            // The application's wait semaphores must be honoured exactly once,
            // by the first submit we actually make -- which is not necessarily
            // the first swapchain, since a swapchain we set nothing up for is
            // skipped above.
            submitInfo.waitSemaphoreCount = appWaitsConsumed ? 0 : pPresentInfo->waitSemaphoreCount;
            submitInfo.pWaitSemaphores    = appWaitsConsumed ? nullptr : pPresentInfo->pWaitSemaphores;
            submitInfo.pWaitDstStageMask  = appWaitsConsumed ? nullptr : waitStages.data();
            appWaitsConsumed              = true;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = presentEffect
                ? &pCollection->commandBuffersEffect[index]
                : &pCollection->commandBuffersNoEffect[index];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &pLogicalSwapchain->semaphores[index];

            VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, effectFence);
            if (vr != VK_SUCCESS)
            {
                // The fence was reset for a submission that never happened, so nothing will ever
                // signal it. Put it back to signalled, or the next wait on it never returns.
                pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, effectFence, nullptr);

                VkFenceCreateInfo fenceInfo = {};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                if (pLogicalDevice->vkd.CreateFence(
                        pLogicalDevice->device, &fenceInfo, nullptr,
                        &pCollection->fences[index]) != VK_SUCCESS)
                    pCollection->fences[index] = VK_NULL_HANDLE;
                pCollection->markSubmissionComplete(index);
                return vr;
            }
            pCollection->markSubmissionStarted(index);

            VkSemaphore finalSemaphore;
            vr = submitOverlayFrame(pLogicalDevice, pLogicalSwapchain, index, finalSemaphore);
            if (vr != VK_SUCCESS)
                return vr;

            presentSemaphores.push_back(finalSemaphore);
        }

        VkPresentInfoKHR presentInfo = *pPresentInfo;
        if (appWaitsConsumed)
        {
            presentInfo.waitSemaphoreCount = presentSemaphores.size();
            presentInfo.pWaitSemaphores    = presentSemaphores.data();
        }
        // Nothing of ours was submitted, so the application's own semaphores are
        // still the only thing the presentation engine must wait on; replacing
        // them with an empty list would present before its rendering completed.

        // Release the global lock across the down-chain present: a frame
        // generation layer below us (lsfg-vk) acquires additional swapchain
        // images and presents several frames INSIDE this call, blocking until
        // the presentation engine frees images. Holding our lock across that
        // serializes every other intercepted entry point (input thread, image
        // create/destroy) into multi-frame stalls -- the frozen-black-screen
        // shape reported when vkBasalt sits above lsfg-vk.
        PFN_vkQueuePresentKHR downchainPresent = pLogicalDevice->vkd.QueuePresentKHR;
        l.unlock();

        VkResult presentResult = downchainPresent(queue, &presentInfo);

        // Down-chain visibility: this log ticking at the game's real FPS while
        // the display runs at the generated rate proves effects process real
        // frames only; anomalous results are surfaced because games die on
        // them silently.
        static std::atomic<uint64_t> presentCycles{0};
        uint64_t cycle = ++presentCycles;
        // trace: every cycle (so a counted rate is a REAL rate, not a sampled
        // one); debug: a bounded sample, because a per-frame log at debug would
        // be its own overhead.
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("present cycle " + std::to_string(cycle) + " -> result " + std::to_string(presentResult));
        if (cycle <= 5 || cycle % 600 == 0)
            Logger::debug("present cycle " + std::to_string(cycle) + " -> result " + std::to_string(presentResult));
        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
        {
            static std::atomic<uint64_t> badResults{0};
            uint64_t bad = ++badResults;
            if (bad <= 20 || bad % 300 == 0)
                Logger::warn("down-chain present returned " + std::to_string(presentResult)
                             + " (occurrence " + std::to_string(bad) + ")");
        }

        return presentResult;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));
        swapchainMap[swapchain]->destroy();
        swapchainMap.erase(swapchain);
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImage(VkDevice                     device,
                                                        const VkImageCreateInfo*     pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkImage*                     pImage)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (isDepthFormat(pCreateInfo->format) && pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT
            && ((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            Logger::debug("detected depth image with format: " + convertToString(pCreateInfo->format));
            Logger::debug(std::to_string(pCreateInfo->extent.width) + "x" + std::to_string(pCreateInfo->extent.height));
            Logger::debug(
                std::to_string((pCreateInfo->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));

            VkImageCreateInfo modifiedCreateInfo = *pCreateInfo;
            modifiedCreateInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            VkResult result = pLogicalDevice->vkd.CreateImage(device, &modifiedCreateInfo, pAllocator, pImage);
            pLogicalDevice->depthImages.push_back({*pImage, pCreateInfo->format, VK_NULL_HANDLE});

            return result;
        }
        else
        {
            return pLogicalDevice->vkd.CreateImage(device, pCreateInfo, pAllocator, pImage);
        }
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
    {
        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        VkResult result = pLogicalDevice->vkd.BindImageMemory(device, image, memory, memoryOffset);

        const auto tracked = std::find_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                                          [image](const auto& depthImage) { return depthImage.image == image; });
        if (tracked == pLogicalDevice->depthImages.end() || tracked->view != VK_NULL_HANDLE)
            return result;
        if (result != VK_SUCCESS)
            return result;

        Logger::debug("before creating depth image view");
        try
        {
            const std::vector<VkImageView> views = createImageViews(
                pLogicalDevice, tracked->format, {image},
                VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);
            if (views.empty() || views.front() == VK_NULL_HANDLE)
                return result;
            tracked->view = views.front();
        }
        catch (const std::exception& e)
        {
            Logger::warn("failed to create a depth image view: "
                         + std::string(e.what()));
            return result;
        }
        Logger::debug("created depth image view");

        const size_t viewCount = std::count_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                                               [](const auto& depthImage) { return depthImage.view != VK_NULL_HANDLE; });
        if (viewCount > 1)
            return result;

        for (auto& [swapchainHandle, pLogicalSwapchain] : swapchainMap)
        {
            if (pLogicalSwapchain->pLogicalDevice != pLogicalDevice)
                continue;
            if (!pLogicalSwapchain->activeEffectCollection)
                continue;

            reloadEffectsForSwapchain(
                pLogicalSwapchain.get(), pConfig.get(), activeEffectNames());
            Logger::debug("transactionally rebound depth for swapchain "
                          + convertToString(swapchainHandle));
        }

        return result;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
    {
        if (!image)
            return;

        scoped_lock l(globalLock);

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        auto it = std::find_if(pLogicalDevice->depthImages.begin(), pLogicalDevice->depthImages.end(),
                               [image](const auto& depthImage) { return depthImage.image == image; });
        if (it != pLogicalDevice->depthImages.end())
        {
            const VkImageView removedView = it->view;
            pLogicalDevice->depthImages.erase(it);

            for (auto& [swapchainHandle, pLogicalSwapchain] : swapchainMap)
            {
                if (pLogicalSwapchain->pLogicalDevice != pLogicalDevice)
                    continue;
                if (!pLogicalSwapchain->activeEffectCollection)
                    continue;

                if (!reloadEffectsForSwapchain(
                        pLogicalSwapchain.get(), pConfig.get(), activeEffectNames()))
                {
                    Logger::warn("depth disappeared while a requested replacement was unusable; installing a safe bypass generation");
                    std::unique_ptr<EffectCollection> bypass;
                    try
                    {
                        bypass = buildEffectCollection(
                            pLogicalSwapchain.get(), pConfig.get(), {}, false);
                    }
                    catch (...)
                    {
                    }
                    if (bypass)
                    {
                        commitReplacementPreservingLastGood(
                            pLogicalSwapchain->activeEffectCollection,
                            pLogicalSwapchain->retiredEffectCollections,
                            std::move(bypass));
                    }
                    else
                    {
                        pLogicalSwapchain->retiredEffectCollections.push_back(
                            std::move(pLogicalSwapchain->activeEffectCollection));
                    }
                }
                Logger::debug("transactionally rebound destroyed depth for swapchain "
                              + convertToString(swapchainHandle));
            }

            // The application is allowed to destroy its image now. One queue
            // completion is required to finish any already-submitted old
            // generation that referenced it; ordinary effect replacement does
            // not drain the queue.
            pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);
            for (auto& [_, pLogicalSwapchain] : swapchainMap)
            {
                if (pLogicalSwapchain->pLogicalDevice == pLogicalDevice)
                    pLogicalSwapchain->retiredEffectCollections.clear();
            }
            if (removedView != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyImageView(
                    pLogicalDevice->device, removedView, nullptr);
        }

        pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, image, pAllocator);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateWaylandSurfaceKHR(
        VkInstance                              instance,
        const VkWaylandSurfaceCreateInfoKHR*    pCreateInfo,
        const VkAllocationCallbacks*            pAllocator,
        VkSurfaceKHR*                           pSurface)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateWaylandSurfaceKHR");

        if (pCreateInfo && pCreateInfo->display)
            setWaylandDisplay(pCreateInfo->display);
        if (pCreateInfo && pCreateInfo->surface)
            setWaylandSurface(pCreateInfo->surface);

        auto nextFunc = (PFN_vkCreateWaylandSurfaceKHR)
            instanceDispatchMap[GetKey(instance)].GetInstanceProcAddr(
                instanceMap[GetKey(instance)], "vkCreateWaylandSurfaceKHR");
        if (!nextFunc)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        return nextFunc(instance, pCreateInfo, pAllocator, pSurface);
    }

    // Xlib and Xcb surface creation is intercepted only to record which backend
    // the client actually chose; the create-info is never read, so the opaque
    // pointer keeps the X11 platform headers out of this translation unit.
    typedef VkResult(VKAPI_PTR* PFN_vkCreateOpaqueSurfaceKHR)(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*);

    static VkResult createX11Surface(VkInstance                   instance,
                                     const void*                  pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkSurfaceKHR*                pSurface,
                                     const char*                  name)
    {
        scoped_lock l(globalLock);

        Logger::trace(name);

        setX11Surface();

        auto nextFunc = (PFN_vkCreateOpaqueSurfaceKHR) instanceDispatchMap[GetKey(instance)].GetInstanceProcAddr(instanceMap[GetKey(instance)], name);
        if (!nextFunc)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        return nextFunc(instance, pCreateInfo, pAllocator, pSurface);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXlibSurfaceKHR(VkInstance                   instance,
                                                                 const void*                  pCreateInfo,
                                                                 const VkAllocationCallbacks* pAllocator,
                                                                 VkSurfaceKHR*                pSurface)
    {
        return createX11Surface(instance, pCreateInfo, pAllocator, pSurface, "vkCreateXlibSurfaceKHR");
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateXcbSurfaceKHR(VkInstance                   instance,
                                                                const void*                  pCreateInfo,
                                                                const VkAllocationCallbacks* pAllocator,
                                                                VkSurfaceKHR*                pSurface)
    {
        return createX11Surface(instance, pCreateInfo, pAllocator, pSurface, "vkCreateXcbSurfaceKHR");
    }


    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (settingsManager.getSafeAntiCheat())
        {
            if (pPropertyCount)
                *pPropertyCount = 0;
            return VK_SUCCESS;
        }

        if (pPropertyCount)
            *pPropertyCount = 1;

        if (pProperties)
        {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer");
            pProperties->implementationVersion = 1;
            pProperties->specVersion =
                VK_MAKE_VERSION(VKBASALT_LAYER_API_MAJOR, VKBASALT_LAYER_API_MINOR, VKBASALT_LAYER_API_PATCH);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
                                                                uint32_t*          pPropertyCount,
                                                                VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                      uint32_t*              pPropertyCount,
                                                                      VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                    const char*            pLayerName,
                                                                    uint32_t*              pPropertyCount,
                                                                    VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strcmp(pLayerName, VKBASALT_NAME))
        {
            if (physicalDevice == VK_NULL_HANDLE)
            {
                return VK_SUCCESS;
            }

            scoped_lock l(globalLock);
            return instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }
} // namespace vkBasalt

extern "C"
{

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName);
    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define GETPROCADDR(func) \
    if (!std::strcmp(pName, "vk" #func)) \
        return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_##func;

    // vkGetDeviceProcAddr needs to behave like vkGetInstanceProcAddr thanks to some games
#define INTERCEPT_CALLS \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
    GETPROCADDR(CreateWaylandSurfaceKHR); \
    GETPROCADDR(CreateXlibSurfaceKHR); \
    GETPROCADDR(CreateXcbSurfaceKHR); \
\
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
    GETPROCADDR(CreateSwapchainKHR); \
    GETPROCADDR(GetSwapchainImagesKHR); \
    GETPROCADDR(QueuePresentKHR); \
    GETPROCADDR(DestroySwapchainKHR); \
\
    if (vkBasalt::settingsManager.getDepthCapture()) \
    { \
        GETPROCADDR(CreateImage); \
        GETPROCADDR(DestroyImage); \
        GETPROCADDR(BindImageMemory); \
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        if (!device)
            return nullptr;

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            const auto it = vkBasalt::deviceMap.find(vkBasalt::GetKey(device));
            if (it == vkBasalt::deviceMap.end() || !it->second || !it->second->vkd.GetDeviceProcAddr)
                return nullptr;
            return it->second->vkd.GetDeviceProcAddr(device, pName);
        }
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        vkBasalt::initConfigs();

        INTERCEPT_CALLS

        if (!instance)
            return nullptr;

        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            const auto it = vkBasalt::instanceDispatchMap.find(vkBasalt::GetKey(instance));
            if (it == vkBasalt::instanceDispatchMap.end() || !it->second.GetInstanceProcAddr)
                return nullptr;
            return it->second.GetInstanceProcAddr(instance, pName);
        }
    }

} // extern "C"
