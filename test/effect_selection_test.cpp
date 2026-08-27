#include "effect_selection.hpp"

#include <vector>

using namespace vkBasalt;

int main()
{
    const std::vector<std::string> selected{
        "Framework", "ZenRT", "Framework", "Disabled", "ZenRT", "Unknown"};
    std::map<std::string, bool> states{
        {"Framework", true}, {"ZenRT", true}, {"Disabled", false}};

    if (enabledEffectNames(selected, states)
        != std::vector<std::string>({"Framework", "ZenRT", "Framework", "ZenRT"}))
        return 1;

    // No enabled entries is a valid bypass-only request. It must not resurrect
    // the complete selected/UI list.
    if (!enabledEffectNames(selected, {
            {"Framework", false}, {"ZenRT", false}, {"Disabled", false}}).empty())
        return 1;

    // Toggling affects only the construction list. The selected order and
    // repeated instances remain untouched and return in place when enabled.
    states["Disabled"] = true;
    if (enabledEffectNames(selected, states)
        != std::vector<std::string>({
            "Framework", "ZenRT", "Framework", "Disabled", "ZenRT"}))
        return 1;
    if (selected != std::vector<std::string>({
            "Framework", "ZenRT", "Framework", "Disabled", "ZenRT", "Unknown"}))
        return 1;

    const std::vector<std::string> allSelected{
        "cas", "cas.1", "cas", "Framework", "ZenRT"};
    const std::map<std::string, bool> allEnabled{
        {"cas", true}, {"cas.1", true}, {"Framework", true}, {"ZenRT", true}};
    return enabledEffectNames(allSelected, allEnabled) == allSelected ? 0 : 1;
}
