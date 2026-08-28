#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "reshade_parser.hpp"
#include "logger.hpp"
#include "mouse_input.hpp"
#include "keyboard_input.hpp"
#include "input_blocker.hpp"
#include "config_serializer.hpp"
#include "wayland_display.hpp"
#include "wayland_pointer_constraints.hpp"
#include "wayland_input_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_vulkan.h"

namespace vkBasalt
{
    static void VKAPI_CALL dummyVulkanFunc() {}

    static PFN_vkVoidFunction imguiVulkanLoaderDummy(const char* function_name, void* user_data)
    {
        LogicalDevice* device = static_cast<LogicalDevice*>(user_data);

        #define CHECK_FUNC(name) if (strcmp(function_name, "vk" #name) == 0) return (PFN_vkVoidFunction)device->vkd.name

        CHECK_FUNC(AllocateCommandBuffers);
        CHECK_FUNC(AllocateDescriptorSets);
        CHECK_FUNC(AllocateMemory);
        CHECK_FUNC(BeginCommandBuffer);
        CHECK_FUNC(BindBufferMemory);
        CHECK_FUNC(BindImageMemory);
        CHECK_FUNC(CmdBeginRenderPass);
        CHECK_FUNC(CmdBindDescriptorSets);
        CHECK_FUNC(CmdBindIndexBuffer);
        CHECK_FUNC(CmdBindPipeline);
        CHECK_FUNC(CmdBindVertexBuffers);
        CHECK_FUNC(CmdCopyBufferToImage);
        CHECK_FUNC(CmdDrawIndexed);
        CHECK_FUNC(CmdEndRenderPass);
        CHECK_FUNC(CmdPipelineBarrier);
        CHECK_FUNC(CmdPushConstants);
        CHECK_FUNC(CmdSetScissor);
        CHECK_FUNC(CmdSetViewport);
        CHECK_FUNC(CreateBuffer);
        CHECK_FUNC(CreateCommandPool);
        CHECK_FUNC(CreateDescriptorPool);
        CHECK_FUNC(CreateDescriptorSetLayout);
        CHECK_FUNC(CreateFence);
        CHECK_FUNC(CreateFramebuffer);
        CHECK_FUNC(CreateGraphicsPipelines);
        CHECK_FUNC(CreateImage);
        CHECK_FUNC(CreateImageView);
        CHECK_FUNC(CreatePipelineLayout);
        CHECK_FUNC(CreateRenderPass);
        CHECK_FUNC(CreateSampler);
        CHECK_FUNC(CreateSemaphore);
        CHECK_FUNC(CreateShaderModule);
        CHECK_FUNC(CreateSwapchainKHR);
        CHECK_FUNC(DestroyBuffer);
        CHECK_FUNC(DestroyCommandPool);
        CHECK_FUNC(DestroyDescriptorPool);
        CHECK_FUNC(DestroyDescriptorSetLayout);
        CHECK_FUNC(DestroyFence);
        CHECK_FUNC(DestroyFramebuffer);
        CHECK_FUNC(DestroyImage);
        CHECK_FUNC(DestroyImageView);
        CHECK_FUNC(DestroyPipeline);
        CHECK_FUNC(DestroyPipelineLayout);
        CHECK_FUNC(DestroyRenderPass);
        CHECK_FUNC(DestroySampler);
        CHECK_FUNC(DestroySemaphore);
        CHECK_FUNC(DestroyShaderModule);
        CHECK_FUNC(DestroySwapchainKHR);
        CHECK_FUNC(EndCommandBuffer);
        CHECK_FUNC(FlushMappedMemoryRanges);
        CHECK_FUNC(FreeCommandBuffers);
        CHECK_FUNC(FreeDescriptorSets);
        CHECK_FUNC(FreeMemory);
        CHECK_FUNC(GetBufferMemoryRequirements);
        CHECK_FUNC(GetDeviceQueue);
        CHECK_FUNC(GetImageMemoryRequirements);
        CHECK_FUNC(GetSwapchainImagesKHR);
        CHECK_FUNC(MapMemory);
        CHECK_FUNC(QueueSubmit);
        CHECK_FUNC(QueueWaitIdle);
        CHECK_FUNC(ResetCommandPool);
        CHECK_FUNC(ResetFences);
        CHECK_FUNC(UnmapMemory);
        CHECK_FUNC(UpdateDescriptorSets);
        CHECK_FUNC(WaitForFences);

        #undef CHECK_FUNC

        #define CHECK_IFUNC(name) if (strcmp(function_name, "vk" #name) == 0) return (PFN_vkVoidFunction)device->vki.name
        CHECK_IFUNC(GetPhysicalDeviceMemoryProperties);
        CHECK_IFUNC(GetPhysicalDeviceProperties);
        CHECK_IFUNC(GetPhysicalDeviceQueueFamilyProperties);
        #undef CHECK_IFUNC

        // ImGui's LoadFunctions treats a nullptr return as a load failure.
        return (PFN_vkVoidFunction)dummyVulkanFunc;
    }

    ImGuiOverlay::ImGuiOverlay(LogicalDevice* device, VkFormat swapchainFormat, uint32_t imageCount, OverlayPersistentState* persistentState)
        : pLogicalDevice(device), pPersistentState(persistentState)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        std::ifstream iniFile(iniPath);
        std::string iniContent((std::istreambuf_iterator<char>(iniFile)),
                                std::istreambuf_iterator<char>());

        if (!iniContent.empty())
            ImGui::LoadIniSettingsFromDisk(iniPath.c_str());

        ImGui::StyleColorsDark();

        ImGuiStyle& style = ImGui::GetStyle();
        style.Alpha            = 0.96f;
        style.WindowRounding   = 8.0f;
        style.ChildRounding    = 8.0f;
        style.FrameRounding    = 6.0f;
        style.PopupRounding    = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding     = 6.0f;
        style.TabRounding      = 6.0f;

        style.WindowPadding    = ImVec2(14.0f, 12.0f);
        style.FramePadding     = ImVec2(10.0f, 6.0f);
        style.ItemSpacing      = ImVec2(10.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
        style.IndentSpacing    = 20.0f;
        style.ScrollbarSize    = 12.0f;
        style.GrabMinSize      = 10.0f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize  = 0.0f;
        style.PopupBorderSize  = 0.0f;
        style.FrameBorderSize  = 0.0f;
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg]        = ImVec4(0.09f, 0.09f, 0.11f, 0.96f);
        colors[ImGuiCol_ChildBg]         = ImVec4(0.11f, 0.11f, 0.13f, 0.60f);
        colors[ImGuiCol_PopupBg]         = ImVec4(0.09f, 0.09f, 0.11f, 0.98f);
        colors[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.21f, 0.21f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive]   = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        colors[ImGuiCol_TitleBg]         = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
        colors[ImGuiCol_TitleBgActive]   = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_Header]          = ImVec4(0.24f, 0.42f, 0.66f, 0.55f);
        colors[ImGuiCol_HeaderHovered]   = ImVec4(0.28f, 0.48f, 0.74f, 0.75f);
        colors[ImGuiCol_HeaderActive]    = ImVec4(0.30f, 0.52f, 0.80f, 0.90f);
        colors[ImGuiCol_Button]          = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
        colors[ImGuiCol_ButtonHovered]   = ImVec4(0.28f, 0.48f, 0.74f, 0.85f);
        colors[ImGuiCol_ButtonActive]    = ImVec4(0.30f, 0.52f, 0.80f, 1.00f);
        colors[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.52f, 0.80f, 0.90f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.36f, 0.60f, 0.90f, 1.00f);
        colors[ImGuiCol_CheckMark]       = ImVec4(0.40f, 0.66f, 0.95f, 1.00f);
        colors[ImGuiCol_Separator]       = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
        colors[ImGuiCol_Tab]             = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
        colors[ImGuiCol_TabHovered]      = ImVec4(0.28f, 0.48f, 0.74f, 0.80f);
        colors[ImGuiCol_TabSelected]     = ImVec4(0.20f, 0.34f, 0.54f, 1.00f);

        initVulkanBackend(swapchainFormat, imageCount);

        if (pPersistentState)
            visible = pPersistentState->visible;

        initialized = true;
        Logger::info("ImGui overlay initialized");
    }

    ImGuiOverlay::~ImGuiOverlay()
    {
        if (!initialized) return;

        if (profileDirty && !activeProfilePath.empty())
            autoSaveProfile();

        pLogicalDevice->vkd.QueueWaitIdle(pLogicalDevice->queue);

        if (isWayland())
            cleanupPointerConstraints();

        std::string iniPath = ConfigSerializer::getBaseConfigDir() + "/imgui.ini";
        ImGui::SaveIniSettingsToDisk(iniPath.c_str());

        if (backendInitialized)
            ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();

        for (auto fb : framebuffers)
        {
            if (fb != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, fb, nullptr);
        }
        for (auto fence : commandBufferFences)
        {
            if (fence != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyFence(pLogicalDevice->device, fence, nullptr);
        }
        if (commandPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyCommandPool(pLogicalDevice->device, commandPool, nullptr);
        if (renderPass != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyRenderPass(pLogicalDevice->device, renderPass, nullptr);
        if (descriptorPool != VK_NULL_HANDLE)
            pLogicalDevice->vkd.DestroyDescriptorPool(pLogicalDevice->device, descriptorPool, nullptr);

        // Tearing down while the overlay owns input would leave the grab and the
        // pointer confinement in place with nothing left to lift them.
        if (visible)
        {
            visible = false;
            setInputBlocked(false);
            if (isWayland())
                releasePointer();
        }

        Logger::info("ImGui overlay destroyed");
    }

    void ImGuiOverlay::toggle()
    {
        visible = !visible;
        setInputBlocked(visible);

        // Keeps the cursor inside the game window while the overlay owns it, so
        // it cannot be lost to another monitor mid-interaction.
        if (isWayland())
        {
            if (visible)
                confinePointer();
            else
                releasePointer();
        }

        saveToPersistentState();
    }

    void ImGuiOverlay::saveToPersistentState()
    {
        if (!pPersistentState)
            return;

        pPersistentState->visible = visible;
    }

    void ImGuiOverlay::updateState(OverlayState newState)
    {
        state = std::move(newState);

        if (!pEffectRegistry)
            return;

        const auto& selectedEffects = pEffectRegistry->getSelectedEffects();
        for (const auto& effectName : selectedEffects)
        {
            if (!pEffectRegistry->hasEffect(effectName))
                pEffectRegistry->ensureEffect(effectName);
        }

        if (profileSafeAntiCheat)
            disableDepthEffects();
    }

    void ImGuiOverlay::disableDepthEffects()
    {
        if (!pEffectRegistry)
            return;

        const auto& selectedEffects = pEffectRegistry->getSelectedEffects();

        for (const auto& effectName : selectedEffects)
        {
            auto it = state.effectPaths.find(effectName);
            if (it == state.effectPaths.end())
                continue;

            bool usesDepth = depthShaders.count(effectName) > 0;
            if (!usesDepth && !shaderTestComplete && !checkedShaders.count(effectName))
            {
                checkedShaders.insert(effectName);
                ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
                if (checkShaderUsesDepth(effectName, it->second, smConfig.discoveredShaderPaths))
                {
                    depthShaders.insert(effectName);
                    usesDepth = true;
                }
            }
            if (usesDepth && pEffectRegistry->isEffectEnabled(effectName))
            {
                pEffectRegistry->setEffectEnabled(effectName, false);
                Logger::info("Safe Anti-Cheat: disabled depth effect '" + effectName + "'");
            }
        }
    }

    std::vector<std::unique_ptr<EffectParam>> ImGuiOverlay::getModifiedParams()
    {
        if (!pEffectRegistry)
            return {};
        return pEffectRegistry->getAllParameters();
    }

    std::vector<std::string> ImGuiOverlay::getActiveEffects() const
    {
        return pEffectRegistry ? pEffectRegistry->getActiveEffects() : std::vector<std::string>{};
    }

    const std::vector<std::string>& ImGuiOverlay::getSelectedEffects() const
    {
        static std::vector<std::string> empty;
        return pEffectRegistry ? pEffectRegistry->getSelectedEffects() : empty;
    }

    void ImGuiOverlay::collectSaveData(
        std::vector<std::string>& effects,
        std::vector<std::string>& disabledEffects,
        std::vector<ConfigParam>& params,
        std::map<std::string, std::string>& effectPaths,
        std::vector<PreprocessorDefinition>& allDefs)
    {
        if (!pEffectRegistry)
            return;

        effects = pEffectRegistry->getSelectedEffects();

        for (const auto& effectName : effects)
        {
            for (auto* p : pEffectRegistry->getParametersForEffect(effectName))
            {
                if (!p->hasChanged())
                    continue;

                auto serialized = p->serialize();
                for (const auto& [suffix, value] : serialized)
                {
                    ConfigParam cp;
                    cp.effectName = p->effectName;
                    cp.paramName = suffix.empty() ? p->name : suffix;
                    cp.value = value;
                    params.push_back(cp);
                }
            }

            if (!pEffectRegistry->isEffectEnabled(effectName))
                disabledEffects.push_back(effectName);

            if (pEffectRegistry->isEffectBuiltIn(effectName))
            {
                std::string effectType = pEffectRegistry->getEffectType(effectName);
                if (!effectType.empty())
                    effectPaths[effectName] = effectType;
            }
            else
            {
                std::string path = pEffectRegistry->getEffectFilePath(effectName);
                if (!path.empty())
                    effectPaths[effectName] = path;

                const auto& defs = pEffectRegistry->getPreprocessorDefs(effectName);
                for (const auto& def : defs)
                    allDefs.push_back(def);

                if (pEffectRegistry->getAllowHalfPrecision(effectName))
                {
                    ConfigParam cp;
                    cp.effectName = effectName;
                    cp.paramName  = "halfPrecision";
                    cp.value      = "true";
                    params.push_back(cp);
                }
            }
        }
    }

    void ImGuiOverlay::saveCurrentConfig()
    {
        if (!pEffectRegistry)
            return;

        std::vector<std::string> effects, disabledEffects;
        std::vector<ConfigParam> params;
        std::map<std::string, std::string> effectPaths;
        std::vector<PreprocessorDefinition> allDefs;
        collectSaveData(effects, disabledEffects, params, effectPaths, allDefs);

        ConfigSerializer::saveConfig(saveConfigName, effects, disabledEffects, params, effectPaths, allDefs);
        profileDirty = false;
    }

    void ImGuiOverlay::autoSaveProfile()
    {
        if (!pEffectRegistry || activeProfilePath.empty())
            return;

        std::vector<std::string> effects, disabledEffects;
        std::vector<ConfigParam> params;
        std::map<std::string, std::string> effectPaths;
        std::vector<PreprocessorDefinition> allDefs;
        collectSaveData(effects, disabledEffects, params, effectPaths, allDefs);

        ProfileSettings profileSettings;
        profileSettings.safeAntiCheat = profileSafeAntiCheat;

        if (ConfigSerializer::saveToPath(activeProfilePath, effects, disabledEffects, params, effectPaths, allDefs, profileSettings))
        {
            profileDirty = false;
            Logger::debug("Auto-saved profile: " + activeProfilePath);
        }
    }

    void ImGuiOverlay::setSelectedEffects(const std::vector<std::string>& effects,
                                          const std::vector<std::string>& disabledEffects)
    {
        if (!pEffectRegistry)
            return;

        pEffectRegistry->setSelectedEffects(effects);

        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        for (const auto& effectName : effects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            pEffectRegistry->setEffectEnabled(effectName, enabled);
        }
    }

    void ImGuiOverlay::initVulkanBackend(VkFormat swapchainFormat, uint32_t imageCount)
    {
        bool loaded = ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imguiVulkanLoaderDummy, pLogicalDevice);
        if (!loaded)
        {
            Logger::err("Failed to load Vulkan functions for ImGui");
            return;
        }
        Logger::debug("ImGui Vulkan functions loaded");

        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 }
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 100;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;

        VkResult vr = pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &poolInfo, nullptr, &descriptorPool);
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to create ImGui descriptor pool: " + std::to_string(vr));
            return;
        }

        VkAttachmentDescription attachment = {};
        attachment.format = swapchainFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // VK_ACCESS_MEMORY_READ_BIT on the outgoing dependency: without it, GPU
        // caches may not be flushed before PipeWire/compositor DMA-BUF readers see the image.
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;

        vr = pLogicalDevice->vkd.CreateRenderPass(pLogicalDevice->device, &renderPassInfo, nullptr, &renderPass);
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to create ImGui render pass: " + std::to_string(vr));
            return;
        }

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = pLogicalDevice->instance;
        initInfo.PhysicalDevice = pLogicalDevice->physicalDevice;
        initInfo.Device = pLogicalDevice->device;
        initInfo.QueueFamily = pLogicalDevice->queueFamilyIndex;
        initInfo.Queue = pLogicalDevice->queue;
        initInfo.DescriptorPool = descriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = 2;
        initInfo.PipelineInfoMain.RenderPass = renderPass;

        ImGui_ImplVulkan_Init(&initInfo);

        this->swapchainFormat = swapchainFormat;
        this->imageCount = imageCount;

        VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCreateInfo.queueFamilyIndex = pLogicalDevice->queueFamilyIndex;
        vr = pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &poolCreateInfo, nullptr, &commandPool);
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to create ImGui command pool: " + std::to_string(vr));
            return;
        }

        commandBuffers.resize(imageCount);
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = imageCount;
        vr = pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, commandBuffers.data());
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to allocate ImGui command buffers: " + std::to_string(vr));
            return;
        }

        commandBufferFences.resize(imageCount);
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < imageCount; i++)
        {
            vr = pLogicalDevice->vkd.CreateFence(pLogicalDevice->device, &fenceInfo, nullptr, &commandBufferFences[i]);
            if (vr != VK_SUCCESS)
                Logger::err("Failed to create ImGui fence " + std::to_string(i) + ": " + std::to_string(vr));
        }

        backendInitialized = true;
        Logger::debug("ImGui Vulkan backend initialized");
    }

    VkCommandBuffer ImGuiOverlay::recordFrame(uint32_t imageIndex, VkImageView imageView, uint32_t width, uint32_t height)
    {
        if (!backendInitialized || !visible)
            return VK_NULL_HANDLE;

        if (imageIndex >= commandBuffers.size() || imageIndex >= commandBufferFences.size())
        {
            Logger::err("ImGui overlay: image index outside the allocated command buffers");
            return VK_NULL_HANDLE;
        }

        currentWidth = width;
        currentHeight = height;

        // The fence was submitted 2-3 frames ago and is almost always already
        // signaled; the timeout scales with the measured frame time.
        static float avgFrameTimeMs = 8.0f;
        VkFence fence = commandBufferFences[imageIndex];
        uint64_t timeoutNs = static_cast<uint64_t>(
            std::clamp(avgFrameTimeMs * 4.0f, 2.0f, 50.0f) * 1'000'000.0f);
        VkResult fenceResult = pLogicalDevice->vkd.WaitForFences(pLogicalDevice->device, 1, &fence, VK_TRUE, timeoutNs);
        if (fenceResult == VK_TIMEOUT)
        {
            Logger::warn("ImGui fence wait timed out for image " + std::to_string(imageIndex));
            return VK_NULL_HANDLE;
        }
        if (fenceResult != VK_SUCCESS)
        {
            Logger::err("ImGui fence wait failed: " + std::to_string(fenceResult));
            return VK_NULL_HANDLE;
        }
        VkResult resetResult = pLogicalDevice->vkd.ResetFences(pLogicalDevice->device, 1, &fence);
        if (resetResult != VK_SUCCESS)
        {
            Logger::err("Failed to reset ImGui fence: " + std::to_string(resetResult));
            return VK_NULL_HANDLE;
        }

        VkCommandBuffer cmd = commandBuffers[imageIndex];

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = pLogicalDevice->vkd.BeginCommandBuffer(cmd, &beginInfo);
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to begin ImGui command buffer: " + std::to_string(vr));
            return VK_NULL_HANDLE;
        }

        if (framebuffers.size() <= imageIndex)
        {
            framebuffers.resize(imageIndex + 1, VK_NULL_HANDLE);
            framebufferImageViews.resize(imageIndex + 1, VK_NULL_HANDLE);
        }

        bool needRecreate = (framebuffers[imageIndex] == VK_NULL_HANDLE) ||
                            (framebufferImageViews[imageIndex] != imageView) ||
                            (framebufferWidth != width) || (framebufferHeight != height);

        if (needRecreate)
        {
            if (framebuffers[imageIndex] != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyFramebuffer(pLogicalDevice->device, framebuffers[imageIndex], nullptr);

            VkFramebufferCreateInfo fbInfo = {};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &imageView;
            fbInfo.width = width;
            fbInfo.height = height;
            fbInfo.layers = 1;
            vr = pLogicalDevice->vkd.CreateFramebuffer(pLogicalDevice->device, &fbInfo, nullptr, &framebuffers[imageIndex]);
            if (vr != VK_SUCCESS)
            {
                Logger::err("Failed to create ImGui framebuffer: " + std::to_string(vr));
                pLogicalDevice->vkd.EndCommandBuffer(cmd);
                return VK_NULL_HANDLE;
            }
            framebufferImageViews[imageIndex] = imageView;
            framebufferWidth = width;
            framebufferHeight = height;
        }

        VkFramebuffer framebuffer = framebuffers[imageIndex];

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)width, (float)height);

        static auto lastFrameTime = std::chrono::steady_clock::now();
        static float lastGoodDt = 0.008f;
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        if (dt > 0.0f && dt < 1.0f)
        {
            io.DeltaTime = dt;
            lastGoodDt = dt;
            avgFrameTimeMs = avgFrameTimeMs * 0.9f + (dt * 1000.0f) * 0.1f;
        }
        else
        {
            io.DeltaTime = lastGoodDt;
        }

        beginWaylandInputFrame();

        MouseState mouse = getMouseState();
        io.MousePos = ImVec2((float)mouse.x, (float)mouse.y);

        io.MouseDown[0] = mouse.leftButton;
        io.MouseDown[1] = mouse.rightButton;
        io.MouseDown[2] = mouse.middleButton;
        io.MouseWheel = mouse.scrollDelta;
        io.MouseDrawCursor = true;  // Draw software cursor (games often hide the OS cursor)

        KeyboardState keyboard = getKeyboardState();
        for (char c : keyboard.typedChars)
            io.AddInputCharacter(c);
        if (keyboard.backspace) { io.AddKeyEvent(ImGuiKey_Backspace, true); io.AddKeyEvent(ImGuiKey_Backspace, false); }
        if (keyboard.del) { io.AddKeyEvent(ImGuiKey_Delete, true); io.AddKeyEvent(ImGuiKey_Delete, false); }
        if (keyboard.enter) { io.AddKeyEvent(ImGuiKey_Enter, true); io.AddKeyEvent(ImGuiKey_Enter, false); }
        if (keyboard.left) { io.AddKeyEvent(ImGuiKey_LeftArrow, true); io.AddKeyEvent(ImGuiKey_LeftArrow, false); }
        if (keyboard.right) { io.AddKeyEvent(ImGuiKey_RightArrow, true); io.AddKeyEvent(ImGuiKey_RightArrow, false); }
        if (keyboard.home) { io.AddKeyEvent(ImGuiKey_Home, true); io.AddKeyEvent(ImGuiKey_Home, false); }
        if (keyboard.end) { io.AddKeyEvent(ImGuiKey_End, true); io.AddKeyEvent(ImGuiKey_End, false); }

        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 screenMin = viewport->WorkPos;
        ImVec2 screenMax = ImVec2(screenMin.x + viewport->WorkSize.x, screenMin.y + viewport->WorkSize.y);

        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 200), ImVec2(screenMax.x - screenMin.x, screenMax.y - screenMin.y));
        if (resetLayoutRequested)
        {
            ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_Always);
            resetLayoutRequested = false;
        }
        else
        {
            ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        }

        ImGui::Begin("vkBasalt Overlay", nullptr, ImGuiWindowFlags_NoCollapse);

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        bool clamped = false;
        if (winPos.x + winSize.x < screenMin.x + 50) { winPos.x = screenMin.x; clamped = true; }
        if (winPos.y < screenMin.y)                   { winPos.y = screenMin.y; clamped = true; }
        if (winPos.x > screenMax.x - 50)              { winPos.x = screenMax.x - 50; clamped = true; }
        if (winPos.y > screenMax.y - 30)              { winPos.y = screenMax.y - 30; clamped = true; }
        if (clamped)
            ImGui::SetWindowPos(winPos);

        processShaderTest();

        if (ImGui::BeginTabBar("OverlayTabs"))
        {
            if (ImGui::BeginTabItem("Effects"))
            {
                if (inSelectionMode)
                    renderAddEffectsView();
                else if (inConfigManageMode)
                    renderConfigManagerView();
                else
                    renderMainView(keyboard);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Shaders"))
            {
                renderShaderManagerView();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                renderSettingsView(keyboard);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Diagnostics"))
            {
                renderDiagnosticsView();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();  // vkBasalt Overlay

        renderDebugWindow();

        if (settingsManager.getAutoApply() && paramsDirty)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeTime).count();
            if (elapsed >= settingsManager.getAutoApplyDelay())
            {
                applyRequested = true;
                paramsDirty = false;
                profileDirty = true;
            }
        }

        if (profileDirty && !paramsDirty && !activeProfilePath.empty())
            autoSaveProfile();

        static bool firstFrame = true;
        if (firstFrame)
        {
            ImGui::SetWindowFocus("Effects");
            firstFrame = false;
        }

        ImGui::Render();

        VkRenderPassBeginInfo rpBegin = {};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea.extent.width = width;
        rpBegin.renderArea.extent.height = height;

        pLogicalDevice->vkd.CmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        pLogicalDevice->vkd.CmdEndRenderPass(cmd);

        vr = pLogicalDevice->vkd.EndCommandBuffer(cmd);
        if (vr != VK_SUCCESS)
        {
            Logger::err("Failed to end ImGui command buffer: " + std::to_string(vr));
            return VK_NULL_HANDLE;
        }

        return cmd;
    }

} // namespace vkBasalt
