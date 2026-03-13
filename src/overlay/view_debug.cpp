#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "config_serializer.hpp"
#include "settings_manager.hpp"
#include "logger.hpp"

#include <cctype>
#include <cstring>
#include <fstream>

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::renderDebugWindow()
    {
        bool shouldShow = settingsManager.getShowDebugWindow();

        // Enable/disable history collection based on debug window visibility
        if (shouldShow && !Logger::isHistoryEnabled())
            Logger::setHistoryEnabled(true);
        else if (!shouldShow && Logger::isHistoryEnabled())
            Logger::setHistoryEnabled(false);

        if (!shouldShow)
            return;

        // Local bool for ImGui window close button
        bool showDebugWindow = true;
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Debug Window", &showDebugWindow))
        {
            ImGui::End();
            if (!showDebugWindow)
            {
                settingsManager.setShowDebugWindow(false);
                settingsManager.save();
            }
            return;
        }

        if (!showDebugWindow)
        {
            settingsManager.setShowDebugWindow(false);
            settingsManager.save();
        }

        if (ImGui::BeginTabBar("DebugTabs"))
        {
            // Effects tab
            if (ImGui::BeginTabItem("Effects"))
            {
                debugWindowTab = 0;

                if (!pEffectRegistry)
                {
                    ImGui::TextDisabled("Effect registry not available");
                    ImGui::EndTabItem();
                    ImGui::EndTabBar();
                    ImGui::End();
                    return;
                }

                const auto& effects = pEffectRegistry->getAllEffects();
                ImGui::Text("Total Effects: %zu", effects.size());
                ImGui::Separator();

                for (const auto& effect : effects)
                {
                    bool open = ImGui::TreeNode(effect.name.c_str(), "[%s] %s",
                        effect.type == EffectType::BuiltIn ? "BuiltIn" : "ReShade",
                        effect.name.c_str());

                    if (open)
                    {
                        ImGui::TextDisabled("Type: %s", effect.effectType.c_str());
                        ImGui::TextDisabled("Enabled: %s", effect.enabled ? "true" : "false");

                        if (!effect.filePath.empty())
                            ImGui::TextDisabled("Path: %s", effect.filePath.c_str());

                        if (effect.hasFailed())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            ImGui::TextWrapped("Error: %s", effect.compileError.c_str());
                            ImGui::PopStyleColor();
                        }

                        if (!effect.parameters.empty())
                        {
                            if (ImGui::TreeNode("Parameters", "Parameters (%zu)", effect.parameters.size()))
                            {
                                for (const auto& param : effect.parameters)
                                {
                                    const char* typeName = param->getTypeName();
                                    auto serialized = param->serialize();
                                    std::string valueStr;
                                    for (size_t i = 0; i < serialized.size(); i++)
                                    {
                                        if (i > 0) valueStr += ", ";
                                        if (!serialized[i].first.empty())
                                            valueStr += serialized[i].first + "=";
                                        valueStr += serialized[i].second;
                                    }
                                    ImGui::BulletText("[%s] %s = %s", typeName, param->name.c_str(), valueStr.c_str());
                                }
                                ImGui::TreePop();
                            }
                        }

                        if (!effect.preprocessorDefs.empty())
                        {
                            if (ImGui::TreeNode("Preprocessor", "Preprocessor Defs (%zu)", effect.preprocessorDefs.size()))
                            {
                                for (const auto& def : effect.preprocessorDefs)
                                    ImGui::BulletText("%s = %s (default: %s)", def.name.c_str(), def.value.c_str(), def.defaultValue.c_str());
                                ImGui::TreePop();
                            }
                        }

                        ImGui::TreePop();
                    }
                }

                ImGui::EndTabItem();
            }

            // Log tab
            if (ImGui::BeginTabItem("Log"))
            {
                debugWindowTab = 1;

                // Only capture keyboard when this window is focused
                bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                // Handle ESC to clear search
                if (windowFocused && ImGui::IsKeyPressed(ImGuiKey_Escape) && debugLogSearch[0] != '\0')
                    debugLogSearch[0] = '\0';

                // Capture keyboard input for seamless search (only when focused and no widget active)
                if (windowFocused && !ImGui::IsAnyItemActive())
                {
                    ImGuiIO& io = ImGui::GetIO();
                    for (int i = 0; i < io.InputQueueCharacters.Size; i++)
                    {
                        ImWchar c = io.InputQueueCharacters[i];
                        if (c >= 32 && c < 127)
                        {
                            size_t len = strlen(debugLogSearch);
                            if (len < sizeof(debugLogSearch) - 1)
                            {
                                debugLogSearch[len] = static_cast<char>(c);
                                debugLogSearch[len + 1] = '\0';
                            }
                        }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && debugLogSearch[0] != '\0')
                    {
                        size_t len = strlen(debugLogSearch);
                        if (len > 0)
                            debugLogSearch[len - 1] = '\0';
                    }
                }

                bool hasSearch = debugLogSearch[0] != '\0';

                if (hasSearch)
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                    ImGui::InputText("##logsearch", debugLogSearch, sizeof(debugLogSearch), ImGuiInputTextFlags_AutoSelectAll);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::Button("x"))
                        debugLogSearch[0] = '\0';
                    ImGui::Separator();
                }

                // Filter checkboxes + actions
                ImGui::Text("Filters:");
                ImGui::SameLine();
                ImGui::Checkbox("Trace", &debugLogFilters[0]);
                ImGui::SameLine();
                ImGui::Checkbox("Debug", &debugLogFilters[1]);
                ImGui::SameLine();
                ImGui::Checkbox("Info", &debugLogFilters[2]);
                ImGui::SameLine();
                ImGui::Checkbox("Warn", &debugLogFilters[3]);
                ImGui::SameLine();
                ImGui::Checkbox("Error", &debugLogFilters[4]);
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                    Logger::clearHistory();
                ImGui::SameLine();
                if (ImGui::Button("Export"))
                {
                    std::string exportPath = ConfigSerializer::getBaseConfigDir() + "/vkbasalt-log.txt";
                    auto history = Logger::getHistory();
                    std::ofstream out(exportPath);
                    if (out.is_open())
                    {
                        for (const auto& entry : history)
                            out << "[" << Logger::levelName(entry.level) << "] " << entry.message << "\n";
                        out.close();
                        Logger::info("Log exported to " + exportPath);
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Save log to ~/.config/vkBasalt-overlay/vkbasalt-log.txt");

                if (!hasSearch)
                    ImGui::TextDisabled("Type to search...");

                ImGui::Separator();

                // Log output in scrolling region
                ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                auto history = Logger::getHistory();

                static ImVec4 levelColors[5] = {
                    ImVec4(0.5f, 0.5f, 0.5f, 1.0f),  // Trace - gray
                    ImVec4(0.4f, 0.7f, 1.0f, 1.0f),  // Debug - light blue
                    ImVec4(0.8f, 0.8f, 0.8f, 1.0f),  // Info - white-ish
                    ImVec4(1.0f, 0.8f, 0.3f, 1.0f),  // Warn - yellow
                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  // Error - red
                };

                auto containsIgnoreCase = [](const std::string& haystack, const char* needle) {
                    if (!needle || needle[0] == '\0')
                        return true;
                    std::string lowerHaystack = haystack;
                    std::string lowerNeedle = needle;
                    for (auto& c : lowerHaystack) c = std::tolower(c);
                    for (auto& c : lowerNeedle) c = std::tolower(c);
                    return lowerHaystack.find(lowerNeedle) != std::string::npos;
                };

                for (const auto& entry : history)
                {
                    uint32_t levelIdx = static_cast<uint32_t>(entry.level);
                    if (levelIdx >= 5)
                        continue;
                    if (!debugLogFilters[levelIdx])
                        continue;
                    if (hasSearch && !containsIgnoreCase(entry.message, debugLogSearch))
                        continue;

                    ImGui::PushStyleColor(ImGuiCol_Text, levelColors[levelIdx]);
                    ImGui::TextUnformatted(("[" + std::string(Logger::levelName(entry.level)) + "] " + entry.message).c_str());
                    ImGui::PopStyleColor();
                }

                // Auto-scroll: stick to bottom unless user scrolled up manually
                bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f;
                if (atBottom && !hasSearch)
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

} // namespace vkBasalt
