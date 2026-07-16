#ifndef CONFIG_HPP_INCLUDED
#define CONFIG_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    class Config
    {
    public:
        Config();
        Config(const std::string& path);
        Config(const Config& other);

        void setFallback(Config* fallback) { pFallback = fallback; }

        template<typename T>
        T getOption(const std::string& option, const T& defaultValue = {})
        {
            auto it = overrides.find(option);
            if (it != overrides.end())
            {
                T result = defaultValue;
                parseOverride(it->second, result);
                return result;
            }

            if (options.find(option) != options.end())
            {
                T result = defaultValue;
                parseOption(option, result);
                return result;
            }

            if (pFallback)
                return pFallback->getOption(option, defaultValue);

            return defaultValue;
        }

        template<typename T>
        T getInstanceOption(const std::string& effectName, const std::string& paramName, const T& defaultValue = {})
        {
            return getOption<T>(effectName + "." + paramName, defaultValue);
        }

        void setOverride(const std::string& option, const std::string& value);
        void clearOverrides();
        bool hasOverrides() const { return !overrides.empty(); }

        bool        hasConfigChanged();
        void        reload();
        std::string getConfigFilePath() const { return configFilePath; }

        std::unordered_map<std::string, std::string> getEffectDefinitions() const;

    private:
        std::unordered_map<std::string, std::string> options;
        std::unordered_map<std::string, std::string> overrides;
        std::string                                  configFilePath;
        time_t                                       lastModifiedTime = 0;
        Config*                                      pFallback = nullptr;
        std::chrono::steady_clock::time_point        lastConfigCheckTime{};

        void readConfigLine(std::string line);
        void readConfigFile(std::ifstream& stream);
        void updateLastModifiedTime();

        void parseOption(const std::string& option, int32_t& result);
        void parseOption(const std::string& option, uint32_t& result);
        void parseOption(const std::string& option, float& result);
        void parseOption(const std::string& option, bool& result);
        void parseOption(const std::string& option, std::string& result);
        void parseOption(const std::string& option, std::vector<std::string>& result);

        void parseOverride(const std::string& value, int32_t& result);
        void parseOverride(const std::string& value, uint32_t& result);
        void parseOverride(const std::string& value, float& result);
        void parseOverride(const std::string& value, bool& result);
        void parseOverride(const std::string& value, std::string& result);
        void parseOverride(const std::string& value, std::vector<std::string>& result);
    };
} // namespace vkBasalt

#endif // CONFIG_HPP_INCLUDED
