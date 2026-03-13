#include "imgui_overlay.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "config_serializer.hpp"
#include "params/field_editor.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace vkBasalt
{
    namespace
    {
        // Render a single preprocessor definition input, returns true if value changed
        void renderPreprocessorDef(PreprocessorDefinition& def, EffectRegistry* registry, const std::string& effectName)
        {
            char valueBuf[64];
            strncpy(valueBuf, def.value.c_str(), sizeof(valueBuf) - 1);
            valueBuf[sizeof(valueBuf) - 1] = '\0';

            ImGui::SetNextItemWidth(80);
            if (ImGui::InputText(def.name.c_str(), valueBuf, sizeof(valueBuf)))
                registry->setPreprocessorDefValue(effectName, def.name, valueBuf);

            if (def.value != def.defaultValue)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(modified)");
            }

            if (ImGui::BeginPopupContextItem("##preproc_reset"))
            {
                if (ImGui::MenuItem("Reset to default"))
                    registry->setPreprocessorDefValue(effectName, def.name, def.defaultValue);
                ImGui::EndPopup();
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Default: %s\nRight-click to reset", def.defaultValue.c_str());
        }

    } // anonymous namespace

    void ImGuiOverlay::renderMainView(const KeyboardState& keyboard)
    {
        if (!pEffectRegistry)
            return;

        // Get a mutable copy of selected effects for this frame
        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();

        // Normal mode - show profile and effect controls

        // Profile section — auto-detected game with profile selector
        if (!activeGameName.empty())
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", activeGameName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Profile dropdown
            static std::vector<std::string> profileList;
            static bool profileListStale = true;
            if (profileListStale)
            {
                profileList = ConfigSerializer::listProfilesForGame(activeGameName);
                profileListStale = false;
            }

            ImGui::Text("Profile:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (ImGui::BeginCombo("##profile", activeProfileName.c_str()))
            {
                for (const auto& profile : profileList)
                {
                    bool selected = (profile == activeProfileName);
                    if (ImGui::Selectable(profile.c_str(), selected))
                    {
                        if (profile != activeProfileName)
                        {
                            // Save current profile before switching
                            if (profileDirty)
                                autoSaveProfile();

                            // Switch to new profile
                            activeProfileName = profile;
                            activeProfilePath = ConfigSerializer::getProfilePath(activeGameName, profile);
                            ConfigSerializer::setActiveProfile(activeGameName, profile);
                            pendingConfigPath = activeProfilePath;
                            applyRequested = true;
                            profileListStale = true;

                            // Load per-profile settings for the new profile
                            ProfileSettings ps = ConfigSerializer::loadProfileSettings(activeProfilePath);
                            profileSafeAntiCheat = ps.safeAntiCheat;
                            if (profileSafeAntiCheat)
                            {
                                settingsManager.setDepthCapture(false);
                                // Depth effects will be disabled after reload completes
                                // (the new config hasn't loaded yet — pendingConfigPath triggers reload)
                            }
                        }
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("+##newprofile"))
                ImGui::OpenPopup("NewProfilePopup");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Create new profile");

            // Only allow deleting non-default profiles
            if (activeProfileName != "default")
            {
                ImGui::SameLine();
                if (ImGui::Button("-##delprofile"))
                    ImGui::OpenPopup("DeleteProfilePopup");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete this profile");
            }

            // New profile popup
            if (ImGui::BeginPopup("NewProfilePopup"))
            {
                static char newProfileName[64] = "";
                ImGui::Text("New profile name:");
                ImGui::SetNextItemWidth(150);
                ImGui::InputText("##newprofname", newProfileName, sizeof(newProfileName));
                ImGui::SameLine();
                ImGui::BeginDisabled(newProfileName[0] == '\0');
                if (ImGui::Button("Create"))
                {
                    if (ConfigSerializer::createProfile(activeGameName, newProfileName, activeProfileName))
                    {
                        // Save current state before switching, then switch
                        if (profileDirty)
                            autoSaveProfile();

                        activeProfileName = newProfileName;
                        activeProfilePath = ConfigSerializer::getProfilePath(activeGameName, newProfileName);
                        ConfigSerializer::setActiveProfile(activeGameName, newProfileName);
                        profileListStale = true;
                        newProfileName[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            // Delete profile confirmation
            if (ImGui::BeginPopup("DeleteProfilePopup"))
            {
                ImGui::Text("Delete profile '%s'?", activeProfileName.c_str());
                if (ImGui::Button("Yes, delete"))
                {
                    ConfigSerializer::deleteProfile(activeGameName, activeProfileName);
                    activeProfileName = "default";
                    activeProfilePath = ConfigSerializer::getProfilePath(activeGameName, "default");
                    ConfigSerializer::setActiveProfile(activeGameName, "default");
                    pendingConfigPath = activeProfilePath;
                    applyRequested = true;
                    profileListStale = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // Safe Anti-Cheat toggle (per-profile)
            if (ImGui::Checkbox("Safe Anti-Cheat", &profileSafeAntiCheat))
            {
                if (profileSafeAntiCheat)
                {
                    settingsManager.setDepthCapture(false);
                    disableDepthEffects();
                    paramsDirty = true;
                }
                profileDirty = true;
                lastChangeTime = std::chrono::steady_clock::now();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Force-disable depth buffer capture for this profile.");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "When enabled:");
                ImGui::BulletText("Depth buffer access is disabled (no wallhack capability)");
                ImGui::BulletText("Only color post-processing effects work (sharpening, color grading, etc.)");
                ImGui::BulletText("Depth-using effects are auto-disabled and hidden from Add Effects");
                ImGui::BulletText("Effects that need depth (SSAO, DoF, fog) will not function");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                    "vkBasalt is a standard Vulkan layer — same as NVIDIA Freestyle or AMD RIS.\n"
                    "With depth capture off, only pixel colors are modified. Completely non-intrusive.");
                ImGui::EndTooltip();
            }
        }
        else
        {
            // Fallback: legacy config UI for unknown executables
            ImGui::Text("Config:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::InputText("##configname", saveConfigName, sizeof(saveConfigName));
            ImGui::SameLine();
            ImGui::BeginDisabled(saveConfigName[0] == '\0');
            if (ImGui::Button("Save"))
                saveCurrentConfig();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("..."))
                inConfigManageMode = true;
        }
        ImGui::Separator();

        bool effectsOn = state.effectsEnabled;
        if (ImGui::Checkbox(effectsOn ? "Effects ON" : "Effects OFF", &effectsOn))
            toggleEffectsRequested = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", settingsManager.getToggleKey().c_str());
        ImGui::Separator();

        // Add Effects button
        if (ImGui::Button("Add Effects..."))
        {
            inSelectionMode = true;
            insertPosition = -1;  // Append to end
            pendingAddEffects.clear();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedEffects.empty());
        if (ImGui::Button("Clear All"))
        {
            selectedEffects.clear();
            pEffectRegistry->clearSelectedEffects();
            paramsDirty = true;
            lastChangeTime = std::chrono::steady_clock::now();
        }
        ImGui::EndDisabled();
        ImGui::Separator();

        // Scrollable effect list (reserve space for footer controls)
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("EffectList", ImVec2(0, -footerHeight), false);

        // Show selected effects with their parameters
        float itemHeight = ImGui::GetFrameHeightWithSpacing();

        // Reset drag target each frame
        dragTargetIndex = -1;

        for (size_t i = 0; i < selectedEffects.size(); i++)
        {
            const std::string& effectName = selectedEffects[i];
            ImGui::PushID(static_cast<int>(i));

            // Highlight drop target
            bool isDropTarget = isDragging && dragSourceIndex != static_cast<int>(i);
            if (isDropTarget)
            {
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImVec2 rowMax = ImVec2(rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + itemHeight);
                if (ImGui::IsMouseHoveringRect(rowMin, rowMax))
                {
                    dragTargetIndex = static_cast<int>(i);
                    ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax, IM_COL32(100, 100, 255, 50));
                }
            }

            // Check if effect failed to compile
            bool effectFailed = pEffectRegistry ? pEffectRegistry->hasEffectFailed(effectName) : false;
            std::string effectError = effectFailed && pEffectRegistry ? pEffectRegistry->getEffectError(effectName) : "";

            // Checkbox to enable/disable effect (read/write via registry)
            // Disabled for failed effects
            if (effectFailed)
                ImGui::BeginDisabled();

            bool effectEnabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effectName) : true;
            if (ImGui::Checkbox("##enabled", &effectEnabled))
            {
                if (pEffectRegistry)
                    pEffectRegistry->setEffectEnabled(effectName, effectEnabled);
                paramsDirty = true;
                lastChangeTime = std::chrono::steady_clock::now();
            }

            if (effectFailed)
                ImGui::EndDisabled();

            ImGui::SameLine();

            // Show failed effects in red
            if (effectFailed)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

            bool treeOpen = ImGui::TreeNode("effect", "%s%s", effectName.c_str(), effectFailed ? " (FAILED)" : "");

            if (effectFailed)
                ImGui::PopStyleColor();

            // Drag from tree node header for reordering
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
            {
                if (!isDragging)
                {
                    isDragging = true;
                    dragSourceIndex = static_cast<int>(i);
                }
            }

            // Right-click context menu
            if (ImGui::BeginPopupContextItem("effect_context"))
            {
                // Toggle ON/OFF
                if (ImGui::MenuItem(effectEnabled ? "Disable" : "Enable"))
                {
                    if (pEffectRegistry)
                    {
                        pEffectRegistry->setEffectEnabled(effectName, !effectEnabled);
                        paramsDirty = true;
                        lastChangeTime = std::chrono::steady_clock::now();
                    }
                }

                // Reset to defaults
                if (ImGui::MenuItem("Reset to Defaults"))
                {
                    for (auto* param : pEffectRegistry->getParametersForEffect(effectName))
                    {
                        FieldEditor* editor = FieldEditorFactory::instance().getEditor(param->getType());
                        if (editor)
                            editor->resetToDefault(*param);
                    }
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }

                ImGui::Separator();

                // Insert effects here
                if (ImGui::MenuItem("Insert effects here..."))
                {
                    insertPosition = static_cast<int>(i);
                    inSelectionMode = true;
                    pendingAddEffects.clear();
                }

                // Remove effect
                if (ImGui::MenuItem("Remove"))
                {
                    std::string removedName = selectedEffects[i];
                    selectedEffects.erase(selectedEffects.begin() + i);
                    pEffectRegistry->setSelectedEffects(selectedEffects);
                    pEffectRegistry->removeEffect(removedName);
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;  // Iterator invalidated — exit loop safely
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();

            if (!treeOpen)
                continue;

            // Show error for failed effects
            if (effectFailed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("Error: %s", effectError.c_str());
                ImGui::PopStyleColor();
                ImGui::TreePop();
                continue;
            }

            // Show preprocessor definitions first (ReShade effects only)
            if (pEffectRegistry)
            {
                auto& defs = pEffectRegistry->getPreprocessorDefs(effectName);
                if (!defs.empty())
                {
                    // Draw background rect behind preprocessor section using channels
                    ImVec2 startPos = ImGui::GetCursorScreenPos();
                    float contentWidth = ImGui::GetContentRegionAvail().x;
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->ChannelsSplit(2);
                    drawList->ChannelsSetCurrent(1);  // Foreground for content

                    if (ImGui::TreeNode("preprocessor", "Preprocessor (%zu)", defs.size()))
                    {
                        ImGui::TextDisabled("Click Apply or press %s to recompile", settingsManager.getReloadKey().c_str());

                        for (size_t defIdx = 0; defIdx < defs.size(); defIdx++)
                        {
                            ImGui::PushID(static_cast<int>(defIdx + 1000));
                            renderPreprocessorDef(defs[defIdx], pEffectRegistry, effectName);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }

                    // Draw background rect on channel 0 (behind content)
                    ImVec2 endPos = ImGui::GetCursorScreenPos();
                    drawList->ChannelsSetCurrent(0);  // Background
                    drawList->AddRectFilled(
                        startPos,
                        ImVec2(startPos.x + contentWidth, endPos.y),
                        IM_COL32(0, 0, 0, 128),  // 50% opacity black
                        0.0f);
                    drawList->ChannelsMerge();
                }
            }

            // Show parameters for this effect
            auto effectParams = pEffectRegistry->getParametersForEffect(effectName);
            for (size_t paramIdx = 0; paramIdx < effectParams.size(); paramIdx++)
            {
                ImGui::PushID(static_cast<int>(paramIdx));
                if (renderFieldEditor(*effectParams[paramIdx]))
                {
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        // Handle drag end and reorder
        if (isDragging && dragSourceIndex >= 0 && dragSourceIndex < static_cast<int>(selectedEffects.size()))
        {
            // Show floating tooltip with dragged effect name
            ImGui::SetTooltip("Moving: %s", selectedEffects[dragSourceIndex].c_str());

            // Check if mouse released
            if (!ImGui::IsMouseDown(0))
            {
                if (dragTargetIndex >= 0 && dragTargetIndex != dragSourceIndex)
                {
                    // Move the effect from source to target
                    std::string moving = selectedEffects[dragSourceIndex];
                    selectedEffects.erase(selectedEffects.begin() + dragSourceIndex);
                    selectedEffects.insert(selectedEffects.begin() + dragTargetIndex, moving);
                    pEffectRegistry->setSelectedEffects(selectedEffects);
                    paramsDirty = true;
                    lastChangeTime = std::chrono::steady_clock::now();
                }
                isDragging = false;
                dragSourceIndex = -1;
                dragTargetIndex = -1;
            }
        }

        ImGui::EndChild();

        ImGui::Separator();
        bool autoApplyVal = settingsManager.getAutoApply();
        if (ImGui::Checkbox("Apply automatically", &autoApplyVal))
        {
            settingsManager.setAutoApply(autoApplyVal);
            settingsManager.save();
        }
        float applyWidth = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine(ImGui::GetWindowWidth() - applyWidth - ImGui::GetStyle().WindowPadding.x);

        // Apply button is always clickable
        if (ImGui::Button("Apply"))
        {
            applyRequested = true;
            paramsDirty = false;
            profileDirty = true;  // Mark for auto-save to profile
        }
        // Note: Auto-apply is handled globally in imgui_overlay.cpp
    }

} // namespace vkBasalt
