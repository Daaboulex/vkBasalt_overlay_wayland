#ifndef RESHADE_PARSER_HPP_INCLUDED
#define RESHADE_PARSER_HPP_INCLUDED

#include <string>
#include <vector>
#include <memory>

#include "effects/effect_config.hpp"
#include "effects/params/effect_param.hpp"
#include "config.hpp"

namespace vkBasalt
{
    struct ShaderTestResult
    {
        std::string effectName;
        std::string filePath;
        bool success = false;
        bool usesDepth = false;
        bool usesMinPrecision = false;
        std::string errorMessage;
    };

    std::vector<std::unique_ptr<EffectParam>> parseReshadeEffect(
        const std::string& effectName,
        const std::string& effectPath,
        Config* pConfig);

    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath);
    ShaderTestResult testShaderCompilation(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths);

    bool checkShaderUsesDepth(
        const std::string& effectName,
        const std::string& effectPath,
        const std::vector<std::string>& includePaths);

    std::vector<PreprocessorDefinition> extractPreprocessorDefinitions(
        const std::string& effectName,
        const std::string& effectPath);

} // namespace vkBasalt

#endif // RESHADE_PARSER_HPP_INCLUDED
