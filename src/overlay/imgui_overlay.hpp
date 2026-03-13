#ifndef IMGUI_OVERLAY_HPP_INCLUDED
#define IMGUI_OVERLAY_HPP_INCLUDED

#include <vector>
#include <memory>
#include <string>
#include <chrono>
#include <map>
#include <set>

#include "vulkan_include.hpp"
#include "logical_device.hpp"
#include "keyboard_input.hpp"
#include "effects/params/effect_param.hpp"
#include "config_serializer.hpp"

namespace vkBasalt
{
    class Effect;
    class EffectRegistry;

    struct OverlayState
    {
        std::vector<std::string> effectNames;           // Effects in current config
        std::vector<std::string> disabledEffects;       // Effects that are unchecked (in list but not rendered)
        std::vector<std::string> currentConfigEffects;  // ReShade effects from current config (e.g., tunic.conf)
        std::vector<std::string> defaultConfigEffects;  // ReShade effects from default vkBasalt.conf (no duplicates)
        std::map<std::string, std::string> effectPaths; // Effect name -> file path (for reshade effects)
        std::string configPath;
        std::string configName;  // Just the filename (e.g., "tunic.conf")
        bool effectsEnabled = true;
        // Parameters now read directly from EffectRegistry
    };

    // UI preferences that persist across swapchain recreation
    // Effect-related state is managed by EffectRegistry
    // Settings are managed by SettingsManager
    struct OverlayPersistentState
    {
        bool visible = false;
    };

    class ImGuiOverlay
    {
    public:
        ImGuiOverlay(LogicalDevice* device, VkFormat swapchainFormat, uint32_t imageCount, OverlayPersistentState* persistentState);
        ~ImGuiOverlay();

        void toggle();
        bool isVisible() const { return visible; }

        void updateState(OverlayState newState);

        // Returns modified parameters when Apply is clicked, empty otherwise
        std::vector<std::unique_ptr<EffectParam>> getModifiedParams();
        bool hasModifiedParams() const { return applyRequested; }
        void clearApplyRequest() { applyRequested = false; }

        // Config switching
        bool hasPendingConfig() const { return !pendingConfigPath.empty(); }
        std::string getPendingConfigPath() const { return pendingConfigPath; }
        void clearPendingConfig() { pendingConfigPath.clear(); }

        // Effects toggle (global on/off)
        bool hasToggleEffectsRequest() const { return toggleEffectsRequested; }
        void clearToggleEffectsRequest() { toggleEffectsRequested = false; }

        // Set the effect registry (single source of truth for enabled states)
        void setEffectRegistry(EffectRegistry* registry) { pEffectRegistry = registry; }

        // Set game/profile info for auto-save (called from basalt.cpp after detection)
        void setGameProfile(const std::string& gameName, const std::string& profileName, const std::string& profilePath)
        {
            activeGameName = gameName;
            activeProfileName = profileName;
            activeProfilePath = profilePath;

            // Load per-profile settings
            if (!profilePath.empty())
            {
                ProfileSettings ps = ConfigSerializer::loadProfileSettings(profilePath);
                profileSafeAntiCheat = ps.safeAntiCheat;
            }
        }

        // Trigger debounced reload (for config switch)
        void markDirty() { paramsDirty = true; lastChangeTime = std::chrono::steady_clock::now(); }

        // Settings were saved (keybindings need reload)
        bool hasSettingsSaved() const { return settingsSaved; }
        void clearSettingsSaved() { settingsSaved = false; }

        // Shader paths were changed (effect list needs refresh)
        bool hasShaderPathsChanged() const { return shaderPathsChanged; }
        void clearShaderPathsChanged() { shaderPathsChanged = false; }

        // Returns list of effects that should be active (enabled, for reloading)
        std::vector<std::string> getActiveEffects() const;

        // Returns all selected effects (enabled + disabled, for parameter collection)
        const std::vector<std::string>& getSelectedEffects() const;

        // Set effects list (when loading a different config)
        // disabledEffects: effects that should be unchecked (in list but not rendered)
        void setSelectedEffects(const std::vector<std::string>& effects,
                                const std::vector<std::string>& disabledEffects = {});

        VkCommandBuffer recordFrame(uint32_t imageIndex, VkImageView imageView, uint32_t width, uint32_t height);

        // Get fence for command buffer synchronization (used by basalt.cpp submit)
        VkFence getCommandBufferFence(uint32_t imageIndex) const
        {
            return (imageIndex < commandBufferFences.size()) ? commandBufferFences[imageIndex] : VK_NULL_HANDLE;
        }

    private:
        void initVulkanBackend(VkFormat swapchainFormat, uint32_t imageCount);
        void saveToPersistentState();
        void saveCurrentConfig();

        // View rendering methods (implemented in separate files)
        void renderAddEffectsView();
        void renderConfigManagerView();
        void renderSettingsView(const KeyboardState& keyboard);
        void renderShaderManagerView();
        void renderShaderTestSection();  // Shader test UI (part of shader manager)
        void renderMainView(const KeyboardState& keyboard);
        void renderDiagnosticsView();
        void renderDebugWindow();  // Debug window with effect registry and log data

        LogicalDevice* pLogicalDevice;
        OverlayPersistentState* pPersistentState;
        EffectRegistry* pEffectRegistry = nullptr;  // Single source of truth for enabled states
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkFence> commandBufferFences;  // Fences to track command buffer completion
        std::vector<VkFramebuffer> framebuffers;    // Pre-created per swapchain image
        std::vector<VkImageView> framebufferImageViews;  // Track which image views framebuffers were created for
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        uint32_t imageCount = 0;
        uint32_t framebufferWidth = 0;   // Dimensions used to create framebuffers
        uint32_t framebufferHeight = 0;
        OverlayState state;
        std::vector<std::pair<std::string, std::string>> pendingAddEffects;  // {instanceName, effectType} to add
        bool inSelectionMode = false;
        int insertPosition = -1;  // Position to insert effects (-1 = append to end)
        char addEffectsSearch[64] = "";  // Search filter for add effects view
        bool inConfigManageMode = false;
        int currentTab = 0;  // 0=Effects, 1=Shaders, 2=Settings, 3=Diagnostics
        std::vector<std::string> configList;

        // Shader Manager state
        std::vector<std::string> shaderMgrParentDirs;
        std::vector<std::string> shaderMgrShaderPaths;
        std::vector<std::string> shaderMgrTexturePaths;
        bool shaderMgrInitialized = false;

        // Shader test state
        bool shaderTestRunning = false;
        bool shaderTestComplete = false;
        size_t shaderTestCurrentIndex = 0;
        int shaderTestDuplicateCount = 0;  // Number of duplicate shaders skipped
        std::vector<std::pair<std::string, std::string>> shaderTestQueue;  // {effectName, filePath}
        std::vector<std::string> shaderTestIncludePaths;  // Cached include paths (avoid re-reading config per shader)
        std::vector<std::tuple<std::string, std::string, bool, std::string>> shaderTestResults;  // {name, path, success, error}

        // UI state for settings view
        int listeningForKey = 0;  // 0=none, 1=toggle, 2=reload, 3=overlay
        bool settingsSaved = false;  // True when settings saved, cleared by basalt.cpp
        bool shaderPathsChanged = false;  // True when shader manager saved, cleared by basalt.cpp
        size_t maxEffects = 10;  // Cached from settingsManager for VRAM estimates

        // UI state for debug window
        int debugWindowTab = 0;  // 0=Registry, 1=Log
        bool debugLogFilters[5] = {false, false, true, true, true};  // Trace, Debug, Info, Warn, Error
        char debugLogSearch[128] = "";  // Search filter for log tab
        int dragSourceIndex = -1;   // Index of effect being dragged, -1 if none
        int dragTargetIndex = -1;   // Index where effect will be dropped
        bool isDragging = false;    // True while actively dragging
        bool applyRequested = false;
        bool toggleEffectsRequested = false;
        bool paramsDirty = false;  // True when params changed, waiting for debounce
        std::chrono::steady_clock::time_point lastChangeTime;
        bool visible = false;
        bool initialized = false;
        bool backendInitialized = false;
        bool resetLayoutRequested = false;  // When true, reset window position/size next frame
        uint32_t currentWidth = 1920;   // Current swapchain resolution for VRAM estimates
        uint32_t currentHeight = 1080;
        char saveConfigName[64] = "";
        std::string pendingConfigPath;

        // Per-app profile system
        std::string activeGameName;       // Detected executable name
        std::string activeProfileName;    // Active profile ("default", "performance", etc.)
        std::string activeProfilePath;    // Full path to active profile file
        bool profileDirty = false;        // True when changes need saving
        bool profileSafeAntiCheat = false; // Per-profile: force-disable depth capture

        void autoSaveProfile();           // Save current state to active profile
        void collectSaveData(            // Shared helper for save operations
            std::vector<std::string>& effects,
            std::vector<std::string>& disabledEffects,
            std::vector<ConfigParam>& params,
            std::map<std::string, std::string>& effectPaths,
            std::vector<PreprocessorDefinition>& allDefs);
    };

} // namespace vkBasalt

#endif // IMGUI_OVERLAY_HPP_INCLUDED
