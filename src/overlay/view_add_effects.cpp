#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "reshade_parser.hpp"
#include "config_serializer.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>

#include "imgui/imgui.h"

namespace vkBasalt
{
    static bool matchesSearch(const std::string& text, const char* search)
    {
        if (!search || !search[0])
            return true;
        std::string lowerText = text;
        std::string lowerSearch = search;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
        return lowerText.find(lowerSearch) != std::string::npos;
    }

    void ImGuiOverlay::renderAddEffectsView()
    {
        if (!pEffectRegistry)
            return;

        // Pre-compile all shaders so depth detection does not lag per effect.
        if (profileSafeAntiCheat && !shaderTestComplete && !shaderTestRunning)
            startShaderTest();

        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && addEffectsSearch[0] != '\0')
        {
            addEffectsSearch[0] = '\0';
        }

        if (!ImGui::IsAnyItemActive())
        {
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < io.InputQueueCharacters.Size; i++)
            {
                ImWchar c = io.InputQueueCharacters[i];
                if (c >= 32 && c < 127)  // Printable ASCII
                {
                    size_t len = strlen(addEffectsSearch);
                    if (len < sizeof(addEffectsSearch) - 1)
                    {
                        addEffectsSearch[len] = static_cast<char>(c);
                        addEffectsSearch[len + 1] = '\0';
                    }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && addEffectsSearch[0] != '\0')
            {
                size_t len = strlen(addEffectsSearch);
                if (len > 0)
                    addEffectsSearch[len - 1] = '\0';
            }
        }

        size_t maxEffectsLimit = static_cast<size_t>(settingsManager.getMaxEffects());
        if (insertPosition >= 0)
            ImGui::Text("Insert Effects at position %d (max %zu)", insertPosition, maxEffectsLimit);
        else
            ImGui::Text("Add Effects (max %zu)", maxEffectsLimit);
        if (shaderTestRunning && profileSafeAntiCheat)
        {
            float progress = shaderTestQueue.empty() ? 1.0f :
                static_cast<float>(shaderTestCurrentIndex) / static_cast<float>(shaderTestQueue.size());
            ImGui::ProgressBar(progress, ImVec2(-1, 0),
                ("Checking shaders " + std::to_string(shaderTestCurrentIndex) + "/" +
                 std::to_string(shaderTestQueue.size())).c_str());
        }
        ImGui::Separator();

        size_t currentCount = selectedEffects.size();
        size_t pendingCount = pendingAddEffects.size();
        size_t totalCount = currentCount + pendingCount;

        std::vector<std::string> builtinEffects = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};

        auto isNameUsed = [&](const std::string& name) {
            if (std::find(selectedEffects.begin(), selectedEffects.end(), name) != selectedEffects.end())
                return true;
            for (const auto& p : pendingAddEffects)
                if (p.first == name)
                    return true;
            return false;
        };

        auto getNextInstanceName = [&](const std::string& effectType) -> std::string {
            if (!isNameUsed(effectType))
                return effectType;
            for (int n = 2; n <= 99; n++)
            {
                std::string candidate = effectType + "." + std::to_string(n);
                if (!isNameUsed(candidate))
                    return candidate;
            }
            return effectType + ".99";
        };

        auto isDepthEffect = [&](const std::string& effectType) -> bool {
            if (!profileSafeAntiCheat)
                return false;

            static const std::set<std::string> safeBuiltins = {"cas", "dls", "fxaa", "smaa", "deband", "lut"};
            if (safeBuiltins.count(effectType))
                return false;

            if (depthShaders.count(effectType))
                return true;

            if (checkedShaders.count(effectType) || shaderTestComplete)
                return false;

            auto it = state.effectPaths.find(effectType);
            if (it == state.effectPaths.end())
                return false;

            checkedShaders.insert(effectType);
            ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
            if (checkShaderUsesDepth(effectType, it->second, smConfig.discoveredShaderPaths))
            {
                depthShaders.insert(effectType);
                return true;
            }
            return false;
        };

        auto renderAddButton = [&](const std::string& effectType, const std::string& tooltip = "") {
            bool atLimit = totalCount >= maxEffectsLimit;
            bool depthBlocked = profileSafeAntiCheat && isDepthEffect(effectType);

            if (atLimit || depthBlocked)
                ImGui::BeginDisabled();

            if (ImGui::Button(effectType.c_str(), ImVec2(-1, 0)))
            {
                std::string instanceName = getNextInstanceName(effectType);
                pendingAddEffects.push_back({instanceName, effectType});
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (depthBlocked)
                    ImGui::SetTooltip("Requires depth buffer (blocked by Safe Anti-Cheat)");
                else if (!tooltip.empty())
                    ImGui::SetTooltip("%s", tooltip.c_str());
            }

            if (atLimit || depthBlocked)
                ImGui::EndDisabled();
        };

        float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        float contentHeight = -footerHeight;
        float columnWidth = ImGui::GetContentRegionAvail().x * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f;

        ImGui::BeginChild("EffectList", ImVec2(columnWidth, contentHeight), true);

        bool hasSearch = addEffectsSearch[0] != '\0';

        if (hasSearch)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##search", addEffectsSearch, sizeof(addEffectsSearch), ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleColor();
            ImGui::TextDisabled("ESC to clear");
            ImGui::Separator();
        }
        else
        {
            ImGui::Text("Available:");
            ImGui::TextDisabled("(type to search)");
            ImGui::Separator();
        }

        std::vector<std::string> sortedCurrentConfig = state.currentConfigEffects;
        std::vector<std::string> sortedDefaultConfig = state.defaultConfigEffects;
        std::sort(sortedCurrentConfig.begin(), sortedCurrentConfig.end());
        std::sort(sortedDefaultConfig.begin(), sortedDefaultConfig.end());

        bool hasBuiltinMatches = false;
        for (const auto& effectType : builtinEffects)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasBuiltinMatches = true;
                break;
            }
        }
        if (hasBuiltinMatches)
        {
            if (!hasSearch)
                ImGui::Text("Built-in:");
            for (const auto& effectType : builtinEffects)
            {
                if (matchesSearch(effectType, addEffectsSearch))
                    renderAddButton(effectType);
            }
        }

        bool hasCurrentMatches = false;
        for (const auto& effectType : sortedCurrentConfig)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasCurrentMatches = true;
                break;
            }
        }
        if (hasCurrentMatches)
        {
            if (hasBuiltinMatches || !hasSearch)
                ImGui::Separator();
            if (!hasSearch)
                ImGui::Text("ReShade (%s):", state.configName.c_str());
            for (const auto& effectType : sortedCurrentConfig)
            {
                if (!matchesSearch(effectType, addEffectsSearch))
                    continue;
                auto it = state.effectPaths.find(effectType);
                std::string path = (it != state.effectPaths.end()) ? it->second : "";
                renderAddButton(effectType, path);
            }
        }

        bool hasDefaultMatches = false;
        for (const auto& effectType : sortedDefaultConfig)
        {
            if (matchesSearch(effectType, addEffectsSearch))
            {
                hasDefaultMatches = true;
                break;
            }
        }
        if (hasDefaultMatches)
        {
            if (hasCurrentMatches || hasBuiltinMatches || !hasSearch)
                ImGui::Separator();
            if (!hasSearch)
                ImGui::Text("ReShade (all):");
            for (const auto& effectType : sortedDefaultConfig)
            {
                if (!matchesSearch(effectType, addEffectsSearch))
                    continue;
                auto it = state.effectPaths.find(effectType);
                std::string path = (it != state.effectPaths.end()) ? it->second : "";
                renderAddButton(effectType, path);
            }
        }

        if (hasSearch && !hasBuiltinMatches && !hasCurrentMatches && !hasDefaultMatches)
            ImGui::TextDisabled("No effects match '%s'", addEffectsSearch);

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("PendingList", ImVec2(columnWidth, contentHeight), true);
        ImGui::Text("Will add (%zu):", pendingCount);
        ImGui::Separator();

        for (size_t i = 0; i < pendingAddEffects.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("x"))
            {
                pendingAddEffects.erase(pendingAddEffects.begin() + i);
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            const auto& [instanceName, effectType] = pendingAddEffects[i];
            if (instanceName != effectType)
                ImGui::Text("%s (%s)", instanceName.c_str(), effectType.c_str());
            else
                ImGui::Text("%s", instanceName.c_str());
            ImGui::PopID();
        }

        if (pendingAddEffects.empty())
            ImGui::TextDisabled("Click effects to add...");

        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::Button("Done"))
        {
            int pos = (insertPosition >= 0 && insertPosition <= static_cast<int>(selectedEffects.size()))
                      ? insertPosition : static_cast<int>(selectedEffects.size());
            for (const auto& [instanceName, effectType] : pendingAddEffects)
            {
                selectedEffects.insert(selectedEffects.begin() + pos, instanceName);
                pos++;  // Insert subsequent effects after the previous one
                pEffectRegistry->ensureEffect(instanceName, effectType);
                pEffectRegistry->setEffectEnabled(instanceName, true);
            }
            if (!pendingAddEffects.empty())
            {
                pEffectRegistry->setSelectedEffects(selectedEffects);
                applyRequested = true;
            }
            pendingAddEffects.clear();
            insertPosition = -1;
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            pendingAddEffects.clear();
            insertPosition = -1;
            inSelectionMode = false;
            addEffectsSearch[0] = '\0';
        }
    }

} // namespace vkBasalt
