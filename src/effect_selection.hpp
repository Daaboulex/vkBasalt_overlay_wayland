#ifndef EFFECT_SELECTION_HPP_INCLUDED
#define EFFECT_SELECTION_HPP_INCLUDED

#include <map>
#include <string>
#include <vector>

namespace vkBasalt
{
    inline std::vector<std::string> enabledEffectNames(
        const std::vector<std::string>& selectedEffects,
        const std::map<std::string, bool>& enabledStates)
    {
        std::vector<std::string> enabled;
        enabled.reserve(selectedEffects.size());
        for (const std::string& effectName : selectedEffects)
        {
            const auto state = enabledStates.find(effectName);
            if (state != enabledStates.end() && state->second)
                enabled.push_back(effectName);
        }
        return enabled;
    }
}

#endif
