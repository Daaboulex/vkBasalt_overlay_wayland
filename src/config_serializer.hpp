#ifndef CONFIG_SERIALIZER_HPP_INCLUDED
#define CONFIG_SERIALIZER_HPP_INCLUDED

#include <string>
#include <vector>
#include <map>

#include "effects/effect_config.hpp"

namespace vkBasalt
{
    struct ConfigParam
    {
        std::string effectName;
        std::string paramName;
        std::string value;
    };

    struct VkBasaltSettings
    {
        int maxEffects = 10;
        bool overlayBlockInput = false;
        std::string toggleKey = "Home";
        std::string reloadKey = "F10";
        std::string overlayKey = "End";
        bool enableOnLaunch = true;
        bool depthCapture = false;
        bool autoApply = true;
        int autoApplyDelay = 200; // ms
        bool showDebugWindow = false;
        bool effectGpuTiming = false;
    };

    struct ProfileSettings
    {
        bool safeAntiCheat = false;
    };

    struct ShaderManagerConfig
    {
        std::vector<std::string> parentDirectories;
        std::vector<std::string> discoveredShaderPaths;
        std::vector<std::string> discoveredTexturePaths;
    };

    class ConfigSerializer
    {
    public:
        static bool saveConfig(
            const std::string& configName,
            const std::vector<std::string>& effects,
            const std::vector<std::string>& disabledEffects,
            const std::vector<ConfigParam>& params,
            const std::map<std::string, std::string>& effectPaths = {},
            const std::vector<PreprocessorDefinition>& preprocessorDefs = {});

        static std::string getBaseConfigDir();

        static std::string getConfigsDir();

        static std::vector<std::string> listConfigs();

        static bool deleteConfig(const std::string& configName);

        static bool setDefaultConfig(const std::string& configName);
        static std::string getDefaultConfig();
        static std::string getDefaultConfigPath();

        static VkBasaltSettings loadSettings();
        static bool saveSettings(const VkBasaltSettings& settings);

        static ShaderManagerConfig loadShaderManagerConfig();
        static bool saveShaderManagerConfig(const ShaderManagerConfig& config);

        static void ensureConfigExists();

        static std::string detectGameName();

        static std::string autoDetectConfig();

        // profileName "default" or "" -> configs/<gameName>.conf
        // profileName "foo" -> configs/<gameName>@foo.conf
        static std::string getProfilePath(const std::string& gameName,
                                          const std::string& profileName = "");

        static std::string ensureGameProfile(const std::string& gameName);

        static std::vector<std::string> listProfilesForGame(const std::string& gameName);

        static std::string getActiveProfile(const std::string& gameName);
        static void setActiveProfile(const std::string& gameName,
                                     const std::string& profileName);

        static bool createProfile(const std::string& gameName,
                                  const std::string& profileName,
                                  const std::string& copyFromProfile = "");

        static bool deleteProfile(const std::string& gameName,
                                  const std::string& profileName);

        static ProfileSettings loadProfileSettings(const std::string& filePath);

        static bool saveToPath(
            const std::string& filePath,
            const std::vector<std::string>& effects,
            const std::vector<std::string>& disabledEffects,
            const std::vector<ConfigParam>& params,
            const std::map<std::string, std::string>& effectPaths = {},
            const std::vector<PreprocessorDefinition>& preprocessorDefs = {},
            const ProfileSettings& profileSettings = {});
    };

} // namespace vkBasalt

#endif // CONFIG_SERIALIZER_HPP_INCLUDED
