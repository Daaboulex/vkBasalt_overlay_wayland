#include "imgui_overlay.hpp"
#include "reshade_parser.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

#include <filesystem>
#include <set>

#ifdef __linux__
#include <malloc.h>  // malloc_trim — reclaim fragmented heap memory
#endif

#include "imgui/imgui.h"

namespace vkBasalt
{
    void ImGuiOverlay::startShaderTest()
    {
        if (shaderTestRunning || shaderTestComplete)
            return;

        // Ensure shader manager paths are loaded
        if (!shaderMgrInitialized)
        {
            ShaderManagerConfig config = ConfigSerializer::loadShaderManagerConfig();
            shaderMgrParentDirs = config.parentDirectories;
            shaderMgrShaderPaths = config.discoveredShaderPaths;
            shaderMgrTexturePaths = config.discoveredTexturePaths;
            shaderMgrInitialized = true;
        }

        // Build test queue from all .fx files in discovered shader paths
        shaderTestQueue.clear();
        shaderTestResults.clear();
        shaderTestCurrentIndex = 0;
        shaderTestComplete = false;
        shaderTestDuplicateCount = 0;

        // Cache include paths once — avoids re-reading shader_manager.conf per shader
        ShaderManagerConfig smConfig = ConfigSerializer::loadShaderManagerConfig();
        shaderTestIncludePaths = smConfig.discoveredShaderPaths;

        std::set<std::string> seenCanonicalPaths;
        for (const auto& shaderPath : shaderMgrShaderPaths)
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(shaderPath))
                {
                    if (!entry.is_regular_file())
                        continue;
                    std::string ext = entry.path().extension().string();
                    if (ext != ".fx" && ext != ".FX")
                        continue;

                    // Deduplicate by canonical path — catches symlinks pointing
                    // to the same nix store file across shader packs
                    std::string filePath = entry.path().string();
                    std::string canonical = filePath;
                    try { canonical = std::filesystem::canonical(entry.path()).string(); }
                    catch (...) {}
                    if (seenCanonicalPaths.count(canonical))
                    {
                        shaderTestDuplicateCount++;
                        continue;
                    }
                    seenCanonicalPaths.insert(canonical);

                    std::string effectName = entry.path().stem().string();
                    shaderTestQueue.emplace_back(effectName, filePath);
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
                // Skip inaccessible directories
            }
        }

        if (!shaderTestQueue.empty())
        {
            shaderTestRunning = true;
            Logger::info("Shader test auto-started: " + std::to_string(shaderTestQueue.size()) + " shaders");
        }
    }

    void ImGuiOverlay::processShaderTest()
    {
        if (!shaderTestRunning)
            return;

        if (shaderTestCurrentIndex < shaderTestQueue.size())
        {
            const auto& [name, path] = shaderTestQueue[shaderTestCurrentIndex];
            ShaderTestResult result = testShaderCompilation(name, path, shaderTestIncludePaths);
            shaderTestResults.emplace_back(result.effectName, result.filePath,
                result.success, result.errorMessage);
            if (result.usesDepth)
                depthShaders.insert(result.effectName);
            shaderTestCurrentIndex++;

            // Reclaim fragmented heap memory every 25 shaders to prevent
            // OOM from accumulating freed-but-unreturned allocations
#ifdef __linux__
            if (shaderTestCurrentIndex % 25 == 0)
                malloc_trim(0);
#endif
        }
        else
        {
            shaderTestRunning = false;
            shaderTestComplete = true;
#ifdef __linux__
            malloc_trim(0);  // Final cleanup after all tests
#endif
            Logger::info("Shader test complete: tested " +
                std::to_string(shaderTestResults.size()) + " shaders");
        }
    }

    void ImGuiOverlay::renderShaderTestSection()
    {
        // Test All Shaders button
        ImGui::Spacing();
        if (shaderTestRunning)
        {
            // Show progress while testing
            float progress = shaderTestQueue.empty() ? 1.0f :
                static_cast<float>(shaderTestCurrentIndex) / static_cast<float>(shaderTestQueue.size());
            ImGui::ProgressBar(progress, ImVec2(-1, 0),
                ("Testing " + std::to_string(shaderTestCurrentIndex) + "/" +
                 std::to_string(shaderTestQueue.size())).c_str());
        }
        else
        {
            if (ImGui::Button("Test All Shaders"))
                startShaderTest();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Test all .fx shaders for compilation errors");
        }

        // Show test results summary if complete
        if (shaderTestComplete && !shaderTestResults.empty())
        {
            ImGui::SameLine();
            int passCount = 0, failCount = 0;
            for (const auto& [name, path, success, error] : shaderTestResults)
            {
                if (success)
                    passCount++;
                else
                    failCount++;
            }
            int depthCount = static_cast<int>(depthShaders.size());
            if (failCount == 0)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "All %d passed!", passCount);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%d passed, %d failed", passCount, failCount);
            if (depthCount > 0)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "(%d use depth)", depthCount);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("These shaders require depth buffer access.\nBlocked when Safe Anti-Cheat is enabled.");
            }

            // Show duplicate warning if any were skipped
            if (shaderTestDuplicateCount > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "(%d duplicates skipped)", shaderTestDuplicateCount);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Identical files (symlinks or copies) found in multiple shader paths.\nEach unique file is tested exactly once.");
            }
        }
    }

    // Render detailed test results in collapsible sections
    // Call this separately after the main shader manager content
    static void renderShaderTestResults(
        const std::vector<std::tuple<std::string, std::string, bool, std::string>>& results,
        const std::set<std::string>& depthShaderNames)
    {
        if (results.empty())
            return;

        // Count failures and depth shaders for headers
        int failCount = 0;
        int depthCount = 0;
        for (const auto& [name, path, success, error] : results)
        {
            if (!success)
                failCount++;
            else if (depthShaderNames.count(name))
                depthCount++;
        }

        // Show failed shaders first (if any)
        if (failCount > 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
            bool failedOpen = ImGui::TreeNode("FailedShaders", "Failed Shaders (%d)", failCount);
            ImGui::PopStyleColor();

            if (failedOpen)
            {
                for (const auto& [name, path, success, error] : results)
                {
                    if (success)
                        continue;

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    // Use path as unique ID to avoid conflicts with duplicate names
                    if (ImGui::TreeNode(path.c_str(), "%s", name.c_str()))
                    {
                        ImGui::PopStyleColor();
                        ImGui::TextDisabled("Path: %s", path.c_str());
                        ImGui::TextWrapped("Error: %s", error.c_str());
                        ImGui::TreePop();
                    }
                    else
                    {
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered() && !error.empty())
                            ImGui::SetTooltip("%s", error.c_str());
                    }
                }
                ImGui::TreePop();
            }
        }

        // Show depth shaders (if any)
        if (depthCount > 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            bool depthOpen = ImGui::TreeNode("DepthShaders", "Depth Shaders (%d)", depthCount);
            ImGui::PopStyleColor();

            if (depthOpen)
            {
                for (const auto& [name, path, success, error] : results)
                {
                    if (!success || !depthShaderNames.count(name))
                        continue;

                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", path.c_str());

                    if (!error.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "(warnings)");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", error.c_str());
                    }
                }
                ImGui::TreePop();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("These shaders require depth buffer access.\nBlocked when Safe Anti-Cheat is enabled.");
        }

        // Show passed shaders (excluding depth shaders — they have their own section)
        int safeCount = static_cast<int>(results.size()) - failCount - depthCount;
        if (safeCount > 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            bool passedOpen = ImGui::TreeNode("PassedShaders", "Safe Shaders (%d)", safeCount);
            ImGui::PopStyleColor();

            if (passedOpen)
            {
                for (const auto& [name, path, success, error] : results)
                {
                    if (!success || depthShaderNames.count(name))
                        continue;

                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", path.c_str());

                    // Show warnings if any
                    if (!error.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "(warnings)");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", error.c_str());
                    }
                }
                ImGui::TreePop();
            }
        }
    }

} // namespace vkBasalt

// Expose renderShaderTestResults for use by view_shader_manager.cpp
namespace vkBasalt
{
    void renderShaderTestResultsUI(
        const std::vector<std::tuple<std::string, std::string, bool, std::string>>& results,
        const std::set<std::string>& depthShaderNames)
    {
        renderShaderTestResults(results, depthShaderNames);
    }
}
