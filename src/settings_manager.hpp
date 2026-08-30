#ifndef SETTINGS_MANAGER_HPP_INCLUDED
#define SETTINGS_MANAGER_HPP_INCLUDED

#include <string>

#include "config_serializer.hpp"

namespace vkBasalt
{
    class SettingsManager
    {
    public:
        void initialize();

        bool isInitialized() const { return initialized; }

        bool save();

        int getMaxEffects() const { return settings.maxEffects; }
        bool getOverlayBlockInput() const { return settings.overlayBlockInput; }
        const std::string& getToggleKey() const { return settings.toggleKey; }
        const std::string& getReloadKey() const { return settings.reloadKey; }
        const std::string& getOverlayKey() const { return settings.overlayKey; }
        bool getEnableOnLaunch() const { return settings.enableOnLaunch; }
        bool getDepthCapture() const { return settings.depthCapture; }
        bool getAutoApply() const { return settings.autoApply; }
        int getAutoApplyDelay() const { return settings.autoApplyDelay; }
        bool getShowDebugWindow() const { return settings.showDebugWindow; }
        bool getEffectGpuTiming() const { return settings.effectGpuTiming; }
        bool getLiveReshadeUniforms() const { return settings.liveReshadeUniforms; }
        bool getSafeAntiCheat() const { return safeAntiCheat; }

        void setMaxEffects(int value) { settings.maxEffects = value; }
        void setOverlayBlockInput(bool value) { settings.overlayBlockInput = value; }
        void setToggleKey(const std::string& value) { settings.toggleKey = value; }
        void setReloadKey(const std::string& value) { settings.reloadKey = value; }
        void setOverlayKey(const std::string& value) { settings.overlayKey = value; }
        void setEnableOnLaunch(bool value) { settings.enableOnLaunch = value; }
        void setDepthCapture(bool value) { settings.depthCapture = value; }
        void setAutoApply(bool value) { settings.autoApply = value; }
        void setAutoApplyDelay(int value) { settings.autoApplyDelay = value; }
        void setShowDebugWindow(bool value) { settings.showDebugWindow = value; }
        void setEffectGpuTiming(bool value) { settings.effectGpuTiming = value; }
        void setLiveReshadeUniforms(bool value) { settings.liveReshadeUniforms = value; }
        void setSafeAntiCheat(bool value) { safeAntiCheat = value; }

        const VkBasaltSettings& getSettings() const { return settings; }

    private:
        VkBasaltSettings settings;
        bool initialized = false;
        bool safeAntiCheat = false;
    };

    extern SettingsManager settingsManager;

} // namespace vkBasalt

#endif // SETTINGS_MANAGER_HPP_INCLUDED
