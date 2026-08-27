#include "effect_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>

#include "reshade_parser.hpp"
#include "config_serializer.hpp"
#include "builtin/builtin_effects.hpp"
#include "logger.hpp"

namespace vkBasalt
{

    namespace
    {
        std::unique_ptr<FloatParam> makeFloatParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            float defaultVal,
            float minVal,
            float maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<FloatParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<float>(effectName, name, defaultVal);
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        std::unique_ptr<IntParam> makeIntParam(
            const std::string& effectName,
            const std::string& name,
            const std::string& label,
            int defaultVal,
            int minVal,
            int maxVal,
            Config* pConfig)
        {
            auto p = std::make_unique<IntParam>();
            p->effectName = effectName;
            p->name = name;
            p->label = label;
            p->defaultValue = defaultVal;
            p->value = pConfig->getInstanceOption<int32_t>(effectName, name, defaultVal);
            p->minValue = minVal;
            p->maxValue = maxVal;
            return p;
        }

        std::string searchDirsForEffect(const std::string& name,
                                        const std::vector<std::string>& dirs)
        {
            for (const auto& dir : dirs)
            {
                std::string path = dir + "/" + name + ".fx";
                if (std::filesystem::exists(path))
                    return path;

                path = dir + "/" + name;
                if (std::filesystem::exists(path))
                    return path;
            }
            return "";
        }

        std::string findEffectPath(const std::string& name, Config* pConfig)
        {
            std::string path = pConfig->getOption<std::string>(name, "");
            if (!path.empty() && std::filesystem::exists(path))
                return path;

            std::string includePath = pConfig->getOption<std::string>("reshadeIncludePath", "");
            if (!includePath.empty())
            {
                std::vector<std::string> includeDirs;
                std::stringstream ss(includePath);
                std::string dir;
                while (std::getline(ss, dir, ':'))
                {
                    if (!dir.empty())
                        includeDirs.push_back(dir);
                }
                path = searchDirsForEffect(name, includeDirs);
                if (!path.empty())
                    return path;
            }

            ShaderManagerConfig shaderMgrConfig = ConfigSerializer::loadShaderManagerConfig();
            path = searchDirsForEffect(name, shaderMgrConfig.discoveredShaderPaths);
            if (!path.empty())
                return path;

            return "";
        }
    } // anonymous namespace

    bool EffectRegistry::isBuiltInEffect(const std::string& name)
    {
        return BuiltInEffects::instance().isBuiltIn(name);
    }

    void EffectRegistry::initialize(Config* pConfig)
    {
        std::lock_guard<std::mutex> lock(mutex);
        this->pConfig = pConfig;
        effects.clear();

        std::vector<std::string> effectNames = pConfig->getOption<std::vector<std::string>>("effects");
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects");

        // Build set for quick lookup
        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        for (const auto& name : effectNames)
        {
            std::string storedValue = pConfig->getOption<std::string>(name, "");

            if (!storedValue.empty() && isBuiltInEffect(storedValue))
            {
                initBuiltInEffect(name, storedValue);
            }
            else if (isBuiltInEffect(name))
            {
                initBuiltInEffect(name, name);
            }
            else
            {
                std::string effectPath = findEffectPath(name, pConfig);
                if (effectPath.empty())
                {
                    Logger::err("EffectRegistry: could not find effect file for: " + name);
                    continue;
                }
                initReshadeEffect(name, effectPath);
            }

            if (!effects.empty() && disabledSet.count(name))
                effects.back().enabled = false;
        }

        Logger::debug("EffectRegistry: initialized " + std::to_string(effects.size()) + " effects");
        ++buildStateRevision;
    }

    void EffectRegistry::initBuiltInEffect(const std::string& instanceName, const std::string& effectType)
    {
        const auto* def = BuiltInEffects::instance().getDef(effectType);
        if (!def)
        {
            Logger::err("Unknown built-in effect type: " + effectType);
            return;
        }

        EffectConfig config;
        config.name = instanceName;
        config.effectType = effectType;
        config.type = EffectType::BuiltIn;
        config.enabled = true;

        for (const auto& paramDef : def->params)
        {
            if (paramDef.type == ParamType::Float)
            {
                config.parameters.push_back(
                    makeFloatParam(instanceName, paramDef.name, paramDef.label,
                                   paramDef.defaultFloat, paramDef.minFloat, paramDef.maxFloat, pConfig));
            }
            else if (paramDef.type == ParamType::Int)
            {
                config.parameters.push_back(
                    makeIntParam(instanceName, paramDef.name, paramDef.label,
                                 paramDef.defaultInt, paramDef.minInt, paramDef.maxInt, pConfig));
            }
        }

        effects.push_back(std::move(config));
    }

    void EffectRegistry::initReshadeEffect(const std::string& name, const std::string& path)
    {
        EffectConfig config;
        config.name = name;
        config.filePath = path;
        config.type = EffectType::ReShade;
        config.enabled = true;

        std::error_code ec;
        config.fileModTime = std::filesystem::last_write_time(path, ec);

        std::filesystem::path p(path);
        config.effectType = p.stem().string();

        ShaderTestResult testResult = testShaderCompilation(name, path);
        if (!testResult.success)
        {
            config.compileError = testResult.errorMessage;
            config.enabled = false;  // Disable failed effects by default
            Logger::err("EffectRegistry: failed to compile " + name + ": " + testResult.errorMessage);
        }
        else
        {
            config.parameters = parseReshadeEffect(name, path, pConfig);

            config.usesMinPrecision  = testResult.usesMinPrecision;
            config.allowHalfPrecision = pConfig->getInstanceOption<bool>(name, "halfPrecision", false);

            config.preprocessorDefs = extractPreprocessorDefinitions(name, path);

            for (auto& def : config.preprocessorDefs)
            {
                std::string configKey = name + "@" + def.name;
                std::string savedValue = pConfig->getOption<std::string>(configKey, "");
                if (!savedValue.empty())
                {
                    def.value = savedValue;
                    Logger::debug("EffectRegistry: loaded preprocessor def " + configKey + " = " + savedValue);
                }
            }

            Logger::debug("EffectRegistry: loaded ReShade effect " + name + " with " +
                          std::to_string(config.parameters.size()) + " parameters and " +
                          std::to_string(config.preprocessorDefs.size()) + " preprocessor defs");
        }

        effects.push_back(std::move(config));
    }

    std::vector<const EffectConfig*> EffectRegistry::getEnabledEffects() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<const EffectConfig*> enabled;

        for (const auto& effect : effects)
        {
            if (effect.enabled)
                enabled.push_back(&effect);
        }

        return enabled;
    }

    std::vector<std::unique_ptr<EffectParam>> EffectRegistry::getAllParameters() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::unique_ptr<EffectParam>> params;

        for (const auto& effect : effects)
        {
            for (const auto& p : effect.parameters)
                params.push_back(p->clone());
        }

        return params;
    }

    EffectConfig* EffectRegistry::findEffect(const std::string& effectName)
    {
        for (auto& effect : effects)
        {
            if (effect.name == effectName)
                return &effect;
        }
        return nullptr;
    }

    const EffectConfig* EffectRegistry::findEffect(const std::string& effectName) const
    {
        for (const auto& effect : effects)
        {
            if (effect.name == effectName)
                return &effect;
        }
        return nullptr;
    }

    EffectParam* EffectRegistry::findParam(EffectConfig& effect, const std::string& paramName)
    {
        for (auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    const EffectParam* EffectRegistry::findParam(const EffectConfig& effect, const std::string& paramName) const
    {
        for (const auto& param : effect.parameters)
        {
            if (param->name == paramName)
                return param.get();
        }
        return nullptr;
    }

    void EffectRegistry::setEffectEnabled(const std::string& effectName, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (effect && effect->enabled != enabled)
        {
            effect->enabled = enabled;
            ++buildStateRevision;
        }
    }

    bool EffectRegistry::isEffectEnabled(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffect(effectName);
        return effect ? effect->enabled : false;
    }

    std::map<std::string, bool> EffectRegistry::getEffectEnabledStates() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        std::map<std::string, bool> states;
        for (const auto& effect : effects)
            states[effect.name] = effect.enabled;
        return states;
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, float value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Float
            && static_cast<FloatParam*>(param)->value != value)
        {
            static_cast<FloatParam*>(param)->value = value;
            ++buildStateRevision;
        }
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Int
            && static_cast<IntParam*>(param)->value != value)
        {
            static_cast<IntParam*>(param)->value = value;
            ++buildStateRevision;
        }
    }

    void EffectRegistry::setParameterValue(const std::string& effectName, const std::string& paramName, bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        EffectParam* param = findParam(*effect, paramName);
        if (param && param->getType() == ParamType::Bool
            && static_cast<BoolParam*>(param)->value != value)
        {
            static_cast<BoolParam*>(param)->value = value;
            ++buildStateRevision;
        }
    }

    EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName)
    {
        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
    }

    const EffectParam* EffectRegistry::getParameter(const std::string& effectName, const std::string& paramName) const
    {
        std::lock_guard<std::mutex> lock(mutex);

        const EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return nullptr;

        return findParam(*effect, paramName);
    }

    std::vector<EffectParam*> EffectRegistry::getParametersForEffect(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<EffectParam*> result;

        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return result;

        for (auto& param : effect->parameters)
            result.push_back(param.get());

        return result;
    }

    bool EffectRegistry::hasEffect(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return findEffect(name) != nullptr;
    }

    std::string EffectRegistry::getEffectFilePath(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->filePath : "";
    }

    std::string EffectRegistry::getEffectType(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->effectType : "";
    }

    bool EffectRegistry::isEffectBuiltIn(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? (effect->type == EffectType::BuiltIn) : false;
    }

    bool EffectRegistry::hasEffectFailed(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->hasFailed() : false;
    }

    std::string EffectRegistry::getEffectError(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(name);
        return effect ? effect->compileError : "";
    }

    void EffectRegistry::setEffectError(
        const std::string& name, const std::string& error, bool disableEffect)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(name);
        if (effect)
        {
            bool changed = effect->compileError != error;
            effect->compileError = error;
            if (disableEffect)
            {
                changed = changed || effect->enabled;
                effect->enabled = false;
            }
            if (changed)
                ++buildStateRevision;
        }
    }

    void EffectRegistry::ensureEffect(const std::string& instanceName, const std::string& effectType)
    {
        std::string type = effectType.empty() ? instanceName : effectType;

        std::string path;
        bool isBuiltIn = isBuiltInEffect(type);
        if (!isBuiltIn)
        {
            path = findEffectPath(type, pConfig);
            if (path.empty() || !std::filesystem::exists(path))
            {
                Logger::warn("EffectRegistry::ensureEffect: could not find effect file for: " + type);
                return;
            }
        }

        std::lock_guard<std::mutex> lock(mutex);

        EffectConfig* existing = findEffect(instanceName);
        if (existing)
        {
            if (!isBuiltIn && !path.empty())
            {
                std::error_code ec;
                auto currentModTime = std::filesystem::last_write_time(path, ec);
                if (!ec && currentModTime != existing->fileModTime)
                {
                    Logger::info("EffectRegistry: shader file changed on disk, re-parsing: " + instanceName);
                    effects.erase(
                        std::remove_if(effects.begin(), effects.end(),
                            [&](const EffectConfig& e) { return e.name == instanceName; }),
                        effects.end());
                }
                else
                {
                    return;
                }
            }
            else
            {
                return;
            }
        }

        if (isBuiltIn)
            initBuiltInEffect(instanceName, type);
        else
            initReshadeEffect(instanceName, path);
        ++buildStateRevision;
    }

    void EffectRegistry::removeEffect(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t oldSize = effects.size();
        effects.erase(
            std::remove_if(effects.begin(), effects.end(),
                [&](const EffectConfig& e) { return e.name == name; }),
            effects.end());
        if (effects.size() != oldSize)
            ++buildStateRevision;
    }

    bool EffectRegistry::effectUsesMinPrecision(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(effectName);
        return effect && effect->usesMinPrecision;
    }

    bool EffectRegistry::getAllowHalfPrecision(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(effectName);
        return effect && effect->allowHalfPrecision;
    }

    void EffectRegistry::setAllowHalfPrecision(const std::string& effectName, bool allow)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(effectName);
        if (effect && effect->allowHalfPrecision != allow)
        {
            effect->allowHalfPrecision = allow;
            ++buildStateRevision;
        }
    }

    std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(effectName);
        if (effect)
            return effect->preprocessorDefs;
        static thread_local std::vector<PreprocessorDefinition> emptyDefs;
        emptyDefs.clear();
        return emptyDefs;
    }

    const std::vector<PreprocessorDefinition>& EffectRegistry::getPreprocessorDefs(const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(effectName);
        if (effect)
            return effect->preprocessorDefs;
        static thread_local const std::vector<PreprocessorDefinition> emptyDefs;
        return emptyDefs;
    }

    std::vector<PreprocessorDefinition> EffectRegistry::getCompilePreprocessorDefs(
        const std::string& effectName) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const EffectConfig* effect = findEffect(effectName);
        return effect ? effect->preprocessorDefs : std::vector<PreprocessorDefinition>{};
    }

    void EffectRegistry::setPreprocessorDefValue(const std::string& effectName, const std::string& macroName, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EffectConfig* effect = findEffect(effectName);
        if (!effect)
            return;

        for (auto& def : effect->preprocessorDefs)
        {
            if (def.name == macroName)
            {
                if (def.value != value)
                {
                    def.value = value;
                    ++buildStateRevision;
                }
                return;
            }
        }
    }

    void EffectRegistry::setSelectedEffects(const std::vector<std::string>& effects)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (selectedEffects != effects)
        {
            selectedEffects = effects;
            ++buildStateRevision;
        }
    }

    void EffectRegistry::clearSelectedEffects()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!selectedEffects.empty())
        {
            selectedEffects.clear();
            ++buildStateRevision;
        }
    }

    void EffectRegistry::initializeSelectedEffectsFromConfig()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (initializedFromConfig || !pConfig)
                return;
        }

        std::vector<std::string> configEffects = pConfig->getOption<std::vector<std::string>>("effects", {});
        std::vector<std::string> disabledEffects = pConfig->getOption<std::vector<std::string>>("disabledEffects", {});

        std::set<std::string> disabledSet(disabledEffects.begin(), disabledEffects.end());

        {
            std::lock_guard<std::mutex> lock(mutex);
            selectedEffects = configEffects;
            ++buildStateRevision;
        }

        for (const auto& effectName : configEffects)
            ensureEffect(effectName);

        for (const auto& effectName : configEffects)
        {
            bool enabled = (disabledSet.find(effectName) == disabledSet.end());
            setEffectEnabled(effectName, enabled);
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            initializedFromConfig = true;
            ++buildStateRevision;
        }
        Logger::debug("EffectRegistry: initialized " + std::to_string(configEffects.size()) +
                      " effects from config (" + std::to_string(disabledEffects.size()) + " disabled)");
    }

    uint64_t EffectRegistry::getBuildStateRevision() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return buildStateRevision;
    }

    std::string EffectRegistry::getBuildStateSignature(
        const std::vector<std::string>& orderedEffects) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::ostringstream signature;
        const auto append = [&signature](const std::string& value) {
            signature << value.size() << ':' << value << ';';
        };

        append(std::to_string(buildStateRevision));
        append("<selected>");
        for (const std::string& name : selectedEffects)
            append(name);
        append("<registry-enabled>");
        for (const EffectConfig& effect : effects)
        {
            append(effect.name);
            append(effect.enabled ? "1" : "0");
        }

        append("<ordered-build>");
        for (const std::string& name : orderedEffects)
        {
            append(name);
            const EffectConfig* effect = findEffect(name);
            if (effect == nullptr)
            {
                append("<missing>");
                continue;
            }

            append(effect->effectType);
            append(effect->filePath);
            append(std::to_string(static_cast<int>(effect->type)));
            append(effect->enabled ? "1" : "0");
            append(effect->allowHalfPrecision ? "1" : "0");
            append(effect->compileError);
            append(std::to_string(effect->fileModTime.time_since_epoch().count()));
            for (const auto& parameter : effect->parameters)
            {
                append(parameter->name);
                for (const auto& [suffix, value] : parameter->serialize())
                {
                    append(suffix);
                    append(value);
                }
            }
            for (const auto& definition : effect->preprocessorDefs)
            {
                append(definition.name);
                append(definition.value);
            }
        }
        return signature.str();
    }

} // namespace vkBasalt
