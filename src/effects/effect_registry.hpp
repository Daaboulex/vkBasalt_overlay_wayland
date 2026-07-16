#ifndef EFFECT_REGISTRY_HPP_INCLUDED
#define EFFECT_REGISTRY_HPP_INCLUDED

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>
#include <filesystem>

#include "effect_config.hpp"
#include "config.hpp"

namespace vkBasalt
{
    class EffectRegistry
    {
    public:
        void initialize(Config* pConfig);

        const std::vector<EffectConfig>& getAllEffects() const
        {
            // No mutex: same-thread (overlay) use only; cross-thread callers use
            // getEnabledEffects(), which copies. Do not hold across effect mutations.
            return effects;
        }

        std::vector<const EffectConfig*> getEnabledEffects() const;

        std::vector<std::unique_ptr<EffectParam>> getAllParameters() const;

        void setEffectEnabled(const std::string& effectName, bool enabled);

        bool isEffectEnabled(const std::string& effectName) const;

        std::map<std::string, bool> getEffectEnabledStates() const;

        void setParameterValue(const std::string& effectName, const std::string& paramName, float value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, int value);
        void setParameterValue(const std::string& effectName, const std::string& paramName, bool value);

        EffectParam* getParameter(const std::string& effectName, const std::string& paramName);
        const EffectParam* getParameter(const std::string& effectName, const std::string& paramName) const;

        std::vector<EffectParam*> getParametersForEffect(const std::string& effectName);

        Config* getConfig() const { return pConfig; }

        static bool isBuiltInEffect(const std::string& name);

        void ensureEffect(const std::string& name, const std::string& effectPath = "");

        void removeEffect(const std::string& name);

        bool hasEffect(const std::string& name) const;

        std::string getEffectFilePath(const std::string& name) const;

        std::string getEffectType(const std::string& name) const;

        bool isEffectBuiltIn(const std::string& name) const;

        bool hasEffectFailed(const std::string& name) const;

        std::string getEffectError(const std::string& name) const;

        void setEffectError(const std::string& name, const std::string& error);

        std::vector<PreprocessorDefinition>& getPreprocessorDefs(const std::string& effectName);
        const std::vector<PreprocessorDefinition>& getPreprocessorDefs(const std::string& effectName) const;

        void setPreprocessorDefValue(const std::string& effectName, const std::string& macroName, const std::string& value);

        const std::vector<std::string>& getSelectedEffects() const { return selectedEffects; }
        void setSelectedEffects(const std::vector<std::string>& effects);
        void clearSelectedEffects();

        bool isInitializedFromConfig() const { return initializedFromConfig; }

        void initializeSelectedEffectsFromConfig();

    private:
        std::vector<EffectConfig> effects;
        std::vector<std::string> selectedEffects;
        bool initializedFromConfig = false;
        Config* pConfig = nullptr;
        mutable std::mutex mutex;

        void initBuiltInEffect(const std::string& instanceName, const std::string& effectType);

        void initReshadeEffect(const std::string& name, const std::string& path);

        // Internal helpers (assume mutex is held)
        EffectConfig* findEffect(const std::string& effectName);
        const EffectConfig* findEffect(const std::string& effectName) const;
        EffectParam* findParam(EffectConfig& effect, const std::string& paramName);
        const EffectParam* findParam(const EffectConfig& effect, const std::string& paramName) const;
    };

} // namespace vkBasalt

#endif // EFFECT_REGISTRY_HPP_INCLUDED
