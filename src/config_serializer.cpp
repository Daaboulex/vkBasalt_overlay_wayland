#include "config_serializer.hpp"
#include "config_paths.hpp"
#include "logger.hpp"

#include <fstream>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <array>
#include <unistd.h>
#include <climits>

namespace vkBasalt
{
    namespace
    {
        // Config::readConfigLine strips unquoted whitespace and stops at '#', so
        // a value carrying either must be written quoted or the reload eats it.
        std::string quoteForConfig(const std::string& value)
        {
            if (value.find_first_of(" \t#\"") == std::string::npos)
                return value;
            std::string quoted = "\"";
            for (char c : value)
                if (c != '"')
                    quoted += c;
            quoted += '"';
            return quoted;
        }
    } // anonymous namespace

    std::string ConfigSerializer::getBaseConfigDir()
    {
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfig)
            return std::string(xdgConfig) + "/vkBasalt-overlay";

        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + "/.config/vkBasalt-overlay";

        return "";
    }

    std::string ConfigSerializer::getConfigsDir()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return "";
        return baseDir + "/configs";
    }

    std::vector<std::string> ConfigSerializer::listConfigs()
    {
        std::vector<std::string> configs;
        std::string dir = getConfigsDir();

        DIR* d = opendir(dir.c_str());
        if (!d)
            return configs;

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".conf")
                configs.push_back(name.substr(0, name.size() - 5));
        }
        closedir(d);

        std::sort(configs.begin(), configs.end());
        return configs;
    }

    static std::string joinEffects(const std::vector<std::string>& effects)
    {
        std::string result;
        for (size_t i = 0; i < effects.size(); i++)
        {
            if (i > 0)
                result += ":";
            result += effects[i];
        }
        return result;
    }

    bool ConfigSerializer::saveConfig(
        const std::string& configName,
        const std::vector<std::string>& effects,
        const std::vector<std::string>& disabledEffects,
        const std::vector<ConfigParam>& params,
        const std::map<std::string, std::string>& effectPaths,
        const std::vector<PreprocessorDefinition>& preprocessorDefs)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
        {
            Logger::err("Could not determine configs directory");
            return false;
        }

        mkdir(configsDir.c_str(), 0755);

        std::string filePath = configsDir + "/" + configName + ".conf";
        bool result = saveToPath(filePath, effects, disabledEffects, params, effectPaths, preprocessorDefs);
        if (result)
            Logger::info("Saved config to: " + filePath);
        return result;
    }

    bool ConfigSerializer::deleteConfig(const std::string& configName)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
            return false;

        std::string filePath = configsDir + "/" + configName + ".conf";
        if (std::remove(filePath.c_str()) == 0)
        {
            Logger::info("Deleted config: " + filePath);
            return true;
        }
        Logger::err("Failed to delete config: " + filePath);
        return false;
    }

    std::string ConfigSerializer::getDefaultConfigPath()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return "";
        return baseDir + "/default_config";
    }

    bool ConfigSerializer::setDefaultConfig(const std::string& configName)
    {
        std::string path = getDefaultConfigPath();
        if (path.empty())
            return false;

        std::ofstream file(path);
        if (!file.is_open())
        {
            Logger::err("Could not write default config file: " + path);
            return false;
        }

        file << configName;
        file.close();
        Logger::info("Set default config: " + configName);
        return true;
    }

    std::string ConfigSerializer::getDefaultConfig()
    {
        std::string path = getDefaultConfigPath();
        if (path.empty())
            return "";

        std::ifstream file(path);
        if (!file.is_open())
            return "";

        std::string configName;
        std::getline(file, configName);
        return configName;
    }

    VkBasaltSettings ConfigSerializer::loadSettings()
    {
        VkBasaltSettings settings;

        std::string baseDir     = getBaseConfigDir();
        std::string homePath    = std::getenv("HOME") ? std::getenv("HOME") : "";
        const char* dataHomeEnv = std::getenv("XDG_DATA_HOME");
        std::string dataHome    = dataHomeEnv ? std::string(dataHomeEnv) + "/vkBasalt-overlay" : homePath + "/.local/share/vkBasalt-overlay";

        const std::array<std::string, 6> configPaths = {
            baseDir + "/settings.conf",
            baseDir + "/vkBasalt.conf",
            dataHome + "/vkBasalt.conf",
            std::string(SYSCONFDIR) + "/vkBasalt-overlay/vkBasalt.conf",
            std::string(SYSCONFDIR) + "/vkBasalt-overlay.conf",
            std::string(DATADIR) + "/vkBasalt-overlay/vkBasalt.conf",
        };

        std::string configPath;
        for (const auto& path : configPaths)
        {
            std::ifstream test(path);
            if (test.is_open())
            {
                configPath = path;
                break;
            }
        }

        if (configPath.empty())
            return settings;

        Logger::info("SettingsManager loading from: " + configPath);

        std::ifstream file(configPath);
        if (!file.is_open())
            return settings;

        std::string line;
        while (std::getline(file, line))
        {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            auto trimWs = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t");
                size_t end = s.find_last_not_of(" \t");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trimWs(key);
            trimWs(value);

            if (key == "maxEffects")
            {
                try { settings.maxEffects = std::stoi(value); }
                catch (...) { Logger::err("invalid maxEffects value: " + value); }
            }
            else if (key == "overlayBlockInput")
                settings.overlayBlockInput = (value == "true" || value == "1");
            else if (key == "toggleKey")
                settings.toggleKey = value;
            else if (key == "reloadKey")
                settings.reloadKey = value;
            else if (key == "overlayKey")
                settings.overlayKey = value;
            else if (key == "enableOnLaunch")
                settings.enableOnLaunch = (value == "true" || value == "1");
            else if (key == "depthCapture")
                settings.depthCapture = (value == "on" || value == "true" || value == "1");
            else if (key == "autoApply")
                settings.autoApply = (value == "true" || value == "1");
            else if (key == "autoApplyDelay")
            {
                try { settings.autoApplyDelay = std::stoi(value); }
                catch (...) { Logger::err("invalid autoApplyDelay value: " + value); }
            }
            else if (key == "showDebugWindow")
                settings.showDebugWindow = (value == "true" || value == "1");
            else if (key == "liveReshadeUniforms")
                settings.liveReshadeUniforms = (value == "true" || value == "1");
        }

        return settings;
    }

    bool ConfigSerializer::saveSettings(const VkBasaltSettings& settings)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/settings.conf";
        std::ofstream file(configPath);
        if (!file.is_open())
        {
            Logger::err("Could not open settings.conf for writing: " + configPath);
            return false;
        }

        file << "# vkBasalt overlay settings -- written by the overlay.\n";
        file << "# Effects and shader paths live in vkBasalt.conf, which this never rewrites.\n\n";

        file << "# Overlay settings\n";
        file << "overlayBlockInput = " << (settings.overlayBlockInput ? "true" : "false") << "\n";
        file << "maxEffects = " << settings.maxEffects << "\n";
        file << "autoApply = " << (settings.autoApply ? "true" : "false") << "\n";
        file << "autoApplyDelay = " << settings.autoApplyDelay << "\n";

        file << "\n# Key bindings\n";
        file << "toggleKey = " << settings.toggleKey << "\n";
        file << "reloadKey = " << settings.reloadKey << "\n";
        file << "overlayKey = " << settings.overlayKey << "\n";

        file << "\n# Startup behavior\n";
        file << "enableOnLaunch = " << (settings.enableOnLaunch ? "true" : "false") << "\n";
        file << "depthCapture = " << (settings.depthCapture ? "on" : "off") << "\n";

        file << "\n# Debug\n";
        file << "showDebugWindow = " << (settings.showDebugWindow ? "true" : "false") << "\n";
        file << "liveReshadeUniforms = " << (settings.liveReshadeUniforms ? "true" : "false") << "\n";

        file.close();
        Logger::info("Saved settings to: " + configPath);
        return true;
    }

    void ConfigSerializer::ensureConfigExists()
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
            return;

        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/settings.conf";

        struct stat st;
        if (stat(configPath.c_str(), &st) == 0)
            return;

        VkBasaltSettings defaults;
        saveSettings(defaults);
        Logger::info("Created default vkBasalt.conf");
    }

    std::string ConfigSerializer::detectGameName()
    {
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0)
            return "";

        buf[len] = '\0';
        std::string exePath(buf);

        size_t lastSlash = exePath.rfind('/');
        if (lastSlash == std::string::npos)
            return exePath;

        return exePath.substr(lastSlash + 1);
    }

    std::string ConfigSerializer::autoDetectConfig()
    {
        std::string gameName = detectGameName();
        if (gameName.empty())
            return "";

        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
            return "";

        std::string configPath = configsDir + "/" + gameName + ".conf";
        struct stat st;
        if (stat(configPath.c_str(), &st) != 0)
            return "";

        Logger::info("Auto-loaded config for: " + gameName);
        return gameName;
    }

    static bool equalsIgnoreCaseLocal(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    static void scanDirectoryForShaders(
        const std::string& dir,
        std::vector<std::string>& shaderPaths,
        std::vector<std::string>& texturePaths)
    {
        try
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                dir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_directory())
                    continue;

                std::error_code relEc;
                const auto relativePath = std::filesystem::relative(entry.path(), dir, relEc);
                if (relEc)
                    continue;

                bool underShaders  = false;
                bool underTextures = false;
                for (const auto& part : relativePath)
                {
                    if (equalsIgnoreCaseLocal(part.string(), "Shaders"))
                        underShaders = true;
                    else if (equalsIgnoreCaseLocal(part.string(), "Textures"))
                        underTextures = true;
                }

                if (underShaders)
                    shaderPaths.push_back(entry.path().string());
                else if (underTextures)
                    texturePaths.push_back(entry.path().string());
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            Logger::err("Error scanning directory " + dir + ": " + e.what());
        }
    }

    ShaderManagerConfig ConfigSerializer::loadShaderManagerConfig()
    {
        static std::mutex                      cacheMutex;
        static ShaderManagerConfig             cachedConfig;
        static std::filesystem::file_time_type cachedMTime;
        static bool                            cacheValid = false;

        const std::string configPath = getBaseConfigDir() + "/shader_manager.conf";
        std::error_code   mtimeError;
        const auto        mtime = std::filesystem::last_write_time(configPath, mtimeError);

        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cacheValid && !mtimeError && mtime == cachedMTime)
            return cachedConfig;

        ShaderManagerConfig config;

        std::ifstream file(configPath);
        if (!file.is_open())
        {
            std::string defaultReshadeDir = getBaseConfigDir() + "/reshade";

            mkdir(defaultReshadeDir.c_str(), 0755);
            mkdir((defaultReshadeDir + "/Shaders").c_str(), 0755);
            mkdir((defaultReshadeDir + "/Textures").c_str(), 0755);

            config.parentDirectories.push_back(defaultReshadeDir);

            scanDirectoryForShaders(defaultReshadeDir,
                config.discoveredShaderPaths, config.discoveredTexturePaths);

            saveShaderManagerConfig(config);
            Logger::info("Created default shader manager config with reshade directory");
            cachedConfig = config;
            cachedMTime  = std::filesystem::last_write_time(configPath, mtimeError);
            cacheValid   = !mtimeError;
            return config;
        }

        std::string line;
        while (std::getline(file, line))
        {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            auto trimWs = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t");
                size_t end = s.find_last_not_of(" \t");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trimWs(key);
            trimWs(value);

            if (key == "parentDir" && !value.empty())
                config.parentDirectories.push_back(value);
            else if (key == "shaderPath" && !value.empty())
                config.discoveredShaderPaths.push_back(value);
            else if (key == "texturePath" && !value.empty())
                config.discoveredTexturePaths.push_back(value);
        }

        cachedConfig = config;
        cachedMTime  = std::filesystem::last_write_time(configPath, mtimeError);
        cacheValid   = !mtimeError;
        return config;
    }

    bool ConfigSerializer::saveShaderManagerConfig(const ShaderManagerConfig& config)
    {
        std::string baseDir = getBaseConfigDir();
        if (baseDir.empty())
        {
            Logger::err("Could not determine config directory");
            return false;
        }

        mkdir(baseDir.c_str(), 0755);

        std::string configPath = baseDir + "/shader_manager.conf";
        std::ofstream file(configPath);
        if (!file.is_open())
        {
            Logger::err("Could not open shader_manager.conf for writing: " + configPath);
            return false;
        }

        file << "# Shader Manager configuration\n";
        file << "# Parent directories are scanned recursively for Shaders/ and Textures/ subdirs\n\n";

        file << "# Parent directories (user-added)\n";
        for (const auto& dir : config.parentDirectories)
            file << "parentDir = " << dir << "\n";

        file << "\n# Discovered shader paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredShaderPaths)
            file << "shaderPath = " << path << "\n";

        file << "\n# Discovered texture paths (auto-generated on scan)\n";
        for (const auto& path : config.discoveredTexturePaths)
            file << "texturePath = " << path << "\n";

        file.close();
        Logger::info("Saved shader manager config to: " + configPath);
        return true;
    }


    std::string ConfigSerializer::getProfilePath(const std::string& gameName,
                                                  const std::string& profileName)
    {
        std::string configsDir = getConfigsDir();
        if (configsDir.empty() || gameName.empty())
            return "";

        if (profileName.empty() || profileName == "default")
            return configsDir + "/" + gameName + ".conf";

        return configsDir + "/" + gameName + "@" + profileName + ".conf";
    }

    std::string ConfigSerializer::ensureGameProfile(const std::string& gameName)
    {
        if (gameName.empty())
            return "";

        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
            return "";

        mkdir(configsDir.c_str(), 0755);

        std::string profilePath = getProfilePath(gameName);

        struct stat st;
        if (stat(profilePath.c_str(), &st) == 0)
        {
            Logger::info("Found existing profile for " + gameName);
            return profilePath;
        }

        std::ofstream file(profilePath);
        if (!file.is_open())
        {
            Logger::err("Could not create profile for " + gameName + ": " + profilePath);
            return "";
        }

        file << "# vkBasalt profile for " << gameName << "\n";
        file << "# Auto-created on first launch. No effects key here means the\n";
        file << "# profile inherits the base vkBasalt.conf chain; saving from the\n";
        file << "# overlay writes an explicit list, and an explicit empty sticks.\n";

        file.close();
        Logger::info("Created default profile for " + gameName + ": " + profilePath);

        setActiveProfile(gameName, "default");

        return profilePath;
    }

    std::vector<std::string> ConfigSerializer::listProfilesForGame(const std::string& gameName)
    {
        std::vector<std::string> profiles;
        if (gameName.empty())
            return profiles;

        std::string configsDir = getConfigsDir();
        if (configsDir.empty())
            return profiles;

        struct stat st;
        std::string defaultPath = configsDir + "/" + gameName + ".conf";
        if (stat(defaultPath.c_str(), &st) == 0)
            profiles.push_back("default");

        std::string prefix = gameName + "@";
        DIR* d = opendir(configsDir.c_str());
        if (!d)
            return profiles;

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() <= 5)
                continue;
            if (name.substr(name.size() - 5) != ".conf")
                continue;
            if (name.substr(0, prefix.size()) != prefix)
                continue;

            std::string profileName = name.substr(prefix.size(), name.size() - prefix.size() - 5);
            if (!profileName.empty())
                profiles.push_back(profileName);
        }
        closedir(d);

        std::sort(profiles.begin() + (profiles.empty() ? 0 : 1), profiles.end());
        return profiles;
    }

    std::string ConfigSerializer::getActiveProfile(const std::string& gameName)
    {
        if (gameName.empty())
            return "default";

        std::string activePath = getConfigsDir() + "/.active_profiles";
        std::ifstream file(activePath);
        if (!file.is_open())
            return "default";

        std::string line;
        while (std::getline(file, line))
        {
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = line.substr(0, eq);
            if (key == gameName)
                return line.substr(eq + 1);
        }

        return "default";
    }

    void ConfigSerializer::setActiveProfile(const std::string& gameName,
                                             const std::string& profileName)
    {
        if (gameName.empty())
            return;

        std::string activePath = getConfigsDir() + "/.active_profiles";

        std::map<std::string, std::string> entries;
        {
            std::ifstream file(activePath);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos)
                        entries[line.substr(0, eq)] = line.substr(eq + 1);
                }
            }
        }

        entries[gameName] = profileName;

        std::string tmpPath = activePath + ".tmp";
        std::ofstream file(tmpPath);
        if (!file.is_open())
            return;

        for (const auto& [key, value] : entries)
            file << key << "=" << value << "\n";

        file.close();
        if (!file.fail())
            std::rename(tmpPath.c_str(), activePath.c_str());
    }

    bool ConfigSerializer::createProfile(const std::string& gameName,
                                          const std::string& profileName,
                                          const std::string& copyFromProfile)
    {
        if (gameName.empty() || profileName.empty() || profileName == "default")
            return false;

        std::string newPath = getProfilePath(gameName, profileName);
        if (newPath.empty())
            return false;

        struct stat st;
        if (stat(newPath.c_str(), &st) == 0)
            return false;  // Already exists

        if (!copyFromProfile.empty())
        {
            std::string srcPath = getProfilePath(gameName, copyFromProfile);
            std::ifstream src(srcPath, std::ios::binary);
            if (src.is_open())
            {
                std::ofstream dst(newPath, std::ios::binary);
                dst << src.rdbuf();
                Logger::info("Created profile " + profileName + " for " + gameName + " (copied from " + copyFromProfile + ")");
                return true;
            }
        }

        std::ofstream file(newPath);
        if (!file.is_open())
            return false;

        file << "# vkBasalt profile '" << profileName << "' for " << gameName << "\n\n";
        file.close();

        Logger::info("Created profile " + profileName + " for " + gameName);
        return true;
    }

    bool ConfigSerializer::deleteProfile(const std::string& gameName,
                                          const std::string& profileName)
    {
        if (profileName.empty() || profileName == "default")
            return false;

        std::string path = getProfilePath(gameName, profileName);
        if (path.empty())
            return false;

        if (std::remove(path.c_str()) == 0)
        {
            Logger::info("Deleted profile " + profileName + " for " + gameName);

            if (getActiveProfile(gameName) == profileName)
                setActiveProfile(gameName, "default");

            return true;
        }

        return false;
    }

    ProfileSettings ConfigSerializer::loadProfileSettings(const std::string& filePath)
    {
        ProfileSettings settings;
        std::ifstream file(filePath);
        if (!file.is_open())
            return settings;

        std::string line;
        while (std::getline(file, line))
        {
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == '#')
                continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t") + 1);
            };
            trim(key);
            trim(value);

            if (key == "safeAntiCheat")
                settings.safeAntiCheat = (value == "true" || value == "on" || value == "1");
        }

        return settings;
    }

    bool ConfigSerializer::saveToPath(
        const std::string& filePath,
        const std::vector<std::string>& effects,
        const std::vector<std::string>& disabledEffects,
        const std::vector<ConfigParam>& params,
        const std::map<std::string, std::string>& effectPaths,
        const std::vector<PreprocessorDefinition>& preprocessorDefs,
        const ProfileSettings& profileSettings)
    {
        std::string tmpPath = filePath + ".tmp";
        std::ofstream file(tmpPath);
        if (!file.is_open())
        {
            Logger::err("Could not open for writing: " + tmpPath);
            return false;
        }

        if (profileSettings.safeAntiCheat)
            file << "safeAntiCheat = true\n\n";

        std::map<std::string, std::vector<const ConfigParam*>> paramsByEffect;
        for (const auto& param : params)
            paramsByEffect[param.effectName].push_back(&param);

        std::map<std::string, std::vector<const PreprocessorDefinition*>> defsByEffect;
        for (const auto& def : preprocessorDefs)
            defsByEffect[def.effectName].push_back(&def);

        for (const auto& [effectName, effectParams] : paramsByEffect)
        {
            file << "# " << effectName << "\n";
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << quoteForConfig(pathIt->second) << "\n";
            for (const auto* param : effectParams)
                file << param->effectName << "." << param->paramName << " = " << param->value << "\n";
            auto defsIt = defsByEffect.find(effectName);
            if (defsIt != defsByEffect.end())
            {
                for (const auto* def : defsIt->second)
                    file << def->effectName << "@" << def->name << " = " << quoteForConfig(def->value) << "\n";
            }
            file << "\n";
        }

        for (const auto& [effectName, effectDefs] : defsByEffect)
        {
            if (paramsByEffect.find(effectName) != paramsByEffect.end())
                continue;
            file << "# " << effectName << "\n";
            auto pathIt = effectPaths.find(effectName);
            if (pathIt != effectPaths.end() && !pathIt->second.empty())
                file << effectName << " = " << quoteForConfig(pathIt->second) << "\n";
            for (const auto* def : effectDefs)
                file << def->effectName << "@" << def->name << " = " << quoteForConfig(def->value) << "\n";
            file << "\n";
        }

        for (const auto& [effectName, path] : effectPaths)
        {
            if (!path.empty() &&
                paramsByEffect.find(effectName) == paramsByEffect.end() &&
                defsByEffect.find(effectName) == defsByEffect.end())
            {
                file << "# " << effectName << "\n";
                file << effectName << " = " << quoteForConfig(path) << "\n\n";
            }
        }

        file << "effects = " << joinEffects(effects) << "\n";

        if (!disabledEffects.empty())
            file << "disabledEffects = " << joinEffects(disabledEffects) << "\n";

        file.close();

        if (file.fail())
        {
            Logger::err("Failed to write config to: " + tmpPath);
            std::remove(tmpPath.c_str());
            return false;
        }

        if (std::rename(tmpPath.c_str(), filePath.c_str()) != 0)
        {
            Logger::err("Failed to rename temp config to: " + filePath);
            std::remove(tmpPath.c_str());
            return false;
        }

        return true;
    }

} // namespace vkBasalt
