#include "imgui_overlay.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"

#include <fstream>
#include <filesystem>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <dlfcn.h>

#include "imgui/imgui.h"

namespace vkBasalt
{
    // Build version - increment this each build
    static constexpr int BUILD_NUMBER = 14;
    static constexpr const char* BUILD_DATE = "2026-03-10";
    namespace
    {
        // Ring buffer for storing history
        template<typename T, size_t N>
        class RingBuffer
        {
        public:
            void push(T value)
            {
                data[writeIndex] = value;
                writeIndex = (writeIndex + 1) % N;
                if (count < N)
                    count++;
            }

            T get(size_t i) const
            {
                if (i >= count)
                    return T{};
                size_t idx = (writeIndex + N - count + i) % N;
                return data[idx];
            }

            size_t size() const { return count; }
            static constexpr size_t capacity() { return N; }

            T min() const
            {
                if (count == 0) return T{};
                T m = get(0);
                for (size_t i = 1; i < count; i++)
                    m = std::min(m, get(i));
                return m;
            }

            T max() const
            {
                if (count == 0) return T{};
                T m = get(0);
                for (size_t i = 1; i < count; i++)
                    m = std::max(m, get(i));
                return m;
            }

            T avg() const
            {
                if (count == 0) return T{};
                T sum = T{};
                for (size_t i = 0; i < count; i++)
                    sum += get(i);
                return sum / static_cast<T>(count);
            }

            // Get data as contiguous array for ImGui plotting
            void copyTo(float* out) const
            {
                for (size_t i = 0; i < count; i++)
                    out[i] = static_cast<float>(get(i));
            }

        private:
            T data[N] = {};
            size_t writeIndex = 0;
            size_t count = 0;
        };

        // ── GPU vendor detection ────────────────────────────────────────────

        enum class GpuVendor { Unknown, AMD, Intel, NVIDIA };

        struct GpuInfo
        {
            GpuVendor vendor = GpuVendor::Unknown;
            std::string drmCardPath;      // /sys/class/drm/cardN
            std::string vendorName;       // Display name
            bool hasGpuUsage = false;
            bool hasVram = false;
            bool hasGtt = false;
        };

        // ── NVIDIA NVML (runtime dlopen) ────────────────────────────────────

        // NVML types (from nvml.h, but we don't require the header)
        using nvmlReturn_t = unsigned int;
        using nvmlDevice_t = void*;
        struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
        struct nvmlMemory_t { unsigned long long total; unsigned long long free; unsigned long long used; };

        static constexpr nvmlReturn_t NVML_SUCCESS = 0;

        struct NvmlState
        {
            void* lib = nullptr;
            nvmlDevice_t device = nullptr;
            bool initialized = false;

            // Function pointers
            nvmlReturn_t (*Init)() = nullptr;
            nvmlReturn_t (*Shutdown)() = nullptr;
            nvmlReturn_t (*DeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*) = nullptr;
            nvmlReturn_t (*DeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
            nvmlReturn_t (*DeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*) = nullptr;
            nvmlReturn_t (*DeviceGetCount)(unsigned int*) = nullptr;
        };

        static NvmlState nvml;

        static bool initNvml()
        {
            if (nvml.initialized)
                return nvml.lib != nullptr;
            nvml.initialized = true;

            nvml.lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
            if (!nvml.lib)
                nvml.lib = dlopen("libnvidia-ml.so", RTLD_LAZY);
            if (!nvml.lib)
            {
                Logger::debug("NVML not available: " + std::string(dlerror()));
                return false;
            }

            nvml.Init = (decltype(nvml.Init))dlsym(nvml.lib, "nvmlInit_v2");
            if (!nvml.Init)
                nvml.Init = (decltype(nvml.Init))dlsym(nvml.lib, "nvmlInit");
            nvml.Shutdown = (decltype(nvml.Shutdown))dlsym(nvml.lib, "nvmlShutdown");
            nvml.DeviceGetHandleByIndex = (decltype(nvml.DeviceGetHandleByIndex))dlsym(nvml.lib, "nvmlDeviceGetHandleByIndex_v2");
            if (!nvml.DeviceGetHandleByIndex)
                nvml.DeviceGetHandleByIndex = (decltype(nvml.DeviceGetHandleByIndex))dlsym(nvml.lib, "nvmlDeviceGetHandleByIndex");
            nvml.DeviceGetUtilizationRates = (decltype(nvml.DeviceGetUtilizationRates))dlsym(nvml.lib, "nvmlDeviceGetUtilizationRates");
            nvml.DeviceGetMemoryInfo = (decltype(nvml.DeviceGetMemoryInfo))dlsym(nvml.lib, "nvmlDeviceGetMemoryInfo");
            nvml.DeviceGetCount = (decltype(nvml.DeviceGetCount))dlsym(nvml.lib, "nvmlDeviceGetCount_v2");
            if (!nvml.DeviceGetCount)
                nvml.DeviceGetCount = (decltype(nvml.DeviceGetCount))dlsym(nvml.lib, "nvmlDeviceGetCount");

            if (!nvml.Init || !nvml.DeviceGetHandleByIndex || !nvml.DeviceGetUtilizationRates || !nvml.DeviceGetMemoryInfo)
            {
                Logger::debug("NVML: missing required symbols");
                dlclose(nvml.lib);
                nvml.lib = nullptr;
                return false;
            }

            if (nvml.Init() != NVML_SUCCESS)
            {
                Logger::debug("NVML: nvmlInit failed");
                dlclose(nvml.lib);
                nvml.lib = nullptr;
                return false;
            }

            // Get first GPU handle (index 0)
            if (nvml.DeviceGetHandleByIndex(0, &nvml.device) != NVML_SUCCESS)
            {
                Logger::debug("NVML: could not get device handle");
                nvml.Shutdown();
                dlclose(nvml.lib);
                nvml.lib = nullptr;
                return false;
            }

            Logger::info("NVML initialized for GPU diagnostics");
            return true;
        }

        __attribute__((unused)) static void shutdownNvml()
        {
            if (nvml.lib)
            {
                if (nvml.Shutdown)
                    nvml.Shutdown();
                dlclose(nvml.lib);
                nvml.lib = nullptr;
            }
        }

        // ── GPU discovery ───────────────────────────────────────────────────

        // Read PCI vendor ID from DRM card sysfs
        static uint16_t readVendorId(const std::string& cardPath)
        {
            std::ifstream f(cardPath + "/device/vendor");
            if (!f.is_open())
                return 0;
            uint16_t id = 0;
            f >> std::hex >> id;
            return id;
        }

        static GpuInfo findGpu()
        {
            GpuInfo info;

            try
            {
                for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm"))
                {
                    std::string name = entry.path().filename().string();
                    if (name.find("card") != 0 || name.find("-") != std::string::npos)
                        continue;

                    std::string cardPath = entry.path().string();
                    uint16_t vendorId = readVendorId(cardPath);

                    // 0x1002 = AMD, 0x8086 = Intel, 0x10de = NVIDIA
                    if (vendorId == 0x1002)
                    {
                        // AMD: check for gpu_busy_percent
                        if (std::filesystem::exists(cardPath + "/device/gpu_busy_percent"))
                        {
                            info.vendor = GpuVendor::AMD;
                            info.drmCardPath = cardPath;
                            info.vendorName = "AMD";
                            info.hasGpuUsage = true;
                            info.hasVram = std::filesystem::exists(cardPath + "/device/mem_info_vram_total");
                            info.hasGtt = std::filesystem::exists(cardPath + "/device/mem_info_gtt_total");
                            return info;
                        }
                    }
                    else if (vendorId == 0x8086)
                    {
                        // Intel: use frequency ratio as GPU utilization estimate
                        info.vendor = GpuVendor::Intel;
                        info.drmCardPath = cardPath;
                        info.vendorName = "Intel";
                        info.hasGpuUsage = std::filesystem::exists(cardPath + "/device/gt_act_freq_mhz") &&
                                           std::filesystem::exists(cardPath + "/device/gt_max_freq_mhz");
                        // Intel discrete (Arc) may have VRAM via drm_memory_stats
                        info.hasVram = std::filesystem::exists(cardPath + "/device/mem_info_vram_total");
                        info.hasGtt = false;
                        return info;
                    }
                    else if (vendorId == 0x10de)
                    {
                        // NVIDIA: use NVML (runtime dlopen)
                        info.vendor = GpuVendor::NVIDIA;
                        info.drmCardPath = cardPath;
                        info.vendorName = "NVIDIA";
                        if (initNvml())
                        {
                            info.hasGpuUsage = true;
                            info.hasVram = true;
                        }
                        return info;
                    }
                }
            }
            catch (...) {}

            return info;
        }

        // ── Static state ────────────────────────────────────────────────────

        static RingBuffer<float, 300> frameTimeHistory;
        static RingBuffer<float, 300> gpuUsageHistory;
        static RingBuffer<float, 300> vramUsageHistory;
        static RingBuffer<float, 300> gttUsageHistory;
        static std::chrono::steady_clock::time_point lastFrameTime;
        static GpuInfo gpuInfo;
        static std::string detectedGameName;
        static std::string autoDetectedConfig;

        // Read a single value from sysfs
        template<typename T>
        bool readSysfs(const std::string& path, T& value)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            file >> value;
            return !file.fail();
        }

        // ── Per-vendor stat readers ─────────────────────────────────────────

        float getGpuUsage()
        {
            if (gpuInfo.vendor == GpuVendor::AMD)
            {
                int usage = 0;
                if (readSysfs(gpuInfo.drmCardPath + "/device/gpu_busy_percent", usage))
                    return static_cast<float>(usage);
            }
            else if (gpuInfo.vendor == GpuVendor::Intel)
            {
                // Frequency ratio: (actual / max) * 100 as utilization estimate
                int actFreq = 0, maxFreq = 0;
                if (readSysfs(gpuInfo.drmCardPath + "/device/gt_act_freq_mhz", actFreq) &&
                    readSysfs(gpuInfo.drmCardPath + "/device/gt_max_freq_mhz", maxFreq) &&
                    maxFreq > 0)
                {
                    return std::min(100.0f, (static_cast<float>(actFreq) / static_cast<float>(maxFreq)) * 100.0f);
                }
            }
            else if (gpuInfo.vendor == GpuVendor::NVIDIA && nvml.lib)
            {
                nvmlUtilization_t util = {};
                if (nvml.DeviceGetUtilizationRates(nvml.device, &util) == NVML_SUCCESS)
                    return static_cast<float>(util.gpu);
            }

            return -1.0f;
        }

        bool getVramUsage(float& usedMB, float& totalMB)
        {
            if (gpuInfo.vendor == GpuVendor::AMD || gpuInfo.vendor == GpuVendor::Intel)
            {
                uint64_t used = 0, total = 0;
                if (readSysfs(gpuInfo.drmCardPath + "/device/mem_info_vram_used", used) &&
                    readSysfs(gpuInfo.drmCardPath + "/device/mem_info_vram_total", total) &&
                    total > 0)
                {
                    usedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                    totalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                    return true;
                }
            }
            else if (gpuInfo.vendor == GpuVendor::NVIDIA && nvml.lib)
            {
                nvmlMemory_t mem = {};
                if (nvml.DeviceGetMemoryInfo(nvml.device, &mem) == NVML_SUCCESS && mem.total > 0)
                {
                    usedMB = static_cast<float>(mem.used) / (1024.0f * 1024.0f);
                    totalMB = static_cast<float>(mem.total) / (1024.0f * 1024.0f);
                    return true;
                }
            }

            return false;
        }

        bool getGttUsage(float& usedMB, float& totalMB)
        {
            if (gpuInfo.vendor != GpuVendor::AMD || gpuInfo.drmCardPath.empty())
                return false;

            uint64_t used = 0, total = 0;
            if (readSysfs(gpuInfo.drmCardPath + "/device/mem_info_gtt_used", used) &&
                readSysfs(gpuInfo.drmCardPath + "/device/mem_info_gtt_total", total) &&
                total > 0)
            {
                usedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                totalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                return true;
            }

            return false;
        }

        // Helper to draw a graph with label
        void drawGraph(const char* label, const char* id, RingBuffer<float, 300>& history, float minVal, float maxVal,
                       const char* overlayFmt, ImVec4 color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f))
        {
            ImGui::Text("%s", label);

            // Get data for plotting
            float data[300];
            history.copyTo(data);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

            char overlay[64];
            snprintf(overlay, sizeof(overlay), overlayFmt, history.size() > 0 ? history.get(history.size() - 1) : 0.0f);

            ImGui::PlotLines(id, data, static_cast<int>(history.size()), 0, overlay,
                            minVal, maxVal, ImVec2(-1, 60));

            ImGui::PopStyleColor(2);

            // Stats below graph
            if (history.size() > 0)
            {
                ImGui::TextDisabled("Min: %.1f  Avg: %.1f  Max: %.1f",
                    history.min(), history.avg(), history.max());
            }
        }
    }

    void ImGuiOverlay::renderDiagnosticsView()
    {
        // Initialize on first call
        static bool initialized = false;
        if (!initialized)
        {
            gpuInfo = findGpu();
            detectedGameName = ConfigSerializer::detectGameName();
            autoDetectedConfig = ConfigSerializer::autoDetectConfig();
            lastFrameTime = std::chrono::steady_clock::now();
            initialized = true;

            if (gpuInfo.vendor != GpuVendor::Unknown)
                Logger::info("Diagnostics: Found " + gpuInfo.vendorName + " GPU at " + gpuInfo.drmCardPath);
            else
                Logger::info("Diagnostics: No supported GPU found");
        }

        // Calculate frame time
        auto now = std::chrono::steady_clock::now();
        float frameTimeMs = std::chrono::duration<float, std::milli>(now - lastFrameTime).count();
        lastFrameTime = now;

        // Only record if reasonable (avoid spikes from tab switching)
        if (frameTimeMs > 0.1f && frameTimeMs < 500.0f)
            frameTimeHistory.push(frameTimeMs);

        // Sample GPU stats (less frequently to reduce overhead)
        static int sampleCounter = 0;
        if (++sampleCounter >= 10)  // Every 10 frames
        {
            sampleCounter = 0;

            float gpuUsage = getGpuUsage();
            if (gpuUsage >= 0)
                gpuUsageHistory.push(gpuUsage);

            float vramUsed, vramTotal;
            if (getVramUsage(vramUsed, vramTotal))
                vramUsageHistory.push((vramUsed / vramTotal) * 100.0f);

            float gttUsed, gttTotal;
            if (getGttUsage(gttUsed, gttTotal))
                gttUsageHistory.push((gttUsed / gttTotal) * 100.0f);
        }

        ImGui::BeginChild("DiagnosticsContent", ImVec2(0, 0), false);

        // Frame rate and timing
        float avgFrameTime = frameTimeHistory.avg();
        float fps = avgFrameTime > 0 ? 1000.0f / avgFrameTime : 0;
        float fps1Low = frameTimeHistory.max() > 0 ? 1000.0f / frameTimeHistory.max() : 0;

        ImGui::Text("Performance");
        ImGui::Separator();

        // Big FPS display
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("(1%% low: %.0f)", fps1Low);

        ImGui::Spacing();

        // Frame time graph
        drawGraph("Frame Time", "##frametime", frameTimeHistory, 0.0f, 50.0f, "%.1f ms",
                  ImVec4(0.4f, 0.8f, 0.4f, 1.0f));

        ImGui::Spacing();
        ImGui::Spacing();

        // GPU stats
        if (gpuInfo.vendor != GpuVendor::Unknown)
        {
            ImGui::Text("GPU (%s)", gpuInfo.vendorName.c_str());
            ImGui::Separator();

            if (gpuInfo.hasGpuUsage)
            {
                float currentGpuUsage = getGpuUsage();
                if (currentGpuUsage >= 0)
                {
                    const char* usageLabel = (gpuInfo.vendor == GpuVendor::Intel)
                        ? "GPU Frequency" : "GPU Usage";
                    drawGraph(usageLabel, "##gpuusage", gpuUsageHistory, 0.0f, 100.0f, "%.0f%%",
                              ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
                    if (gpuInfo.vendor == GpuVendor::Intel)
                        ImGui::TextDisabled("(estimated from frequency ratio)");
                    ImGui::Spacing();
                }
            }

            float vramUsed, vramTotal;
            if (getVramUsage(vramUsed, vramTotal))
            {
                ImGui::Text("VRAM: %.0f / %.0f MB", vramUsed, vramTotal);
                ImGui::ProgressBar(vramUsed / vramTotal, ImVec2(-1, 0));
            }

            float gttUsed, gttTotal;
            if (getGttUsage(gttUsed, gttTotal))
            {
                ImGui::Text("GTT (shared): %.0f / %.0f MB", gttUsed, gttTotal);
                ImGui::ProgressBar(gttUsed / gttTotal, ImVec2(-1, 0));

                ImGui::Spacing();
                drawGraph("Memory Usage", "##gttusage", gttUsageHistory, 0.0f, 100.0f, "%.0f%%",
                          ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
            }
            else if (getVramUsage(vramUsed, vramTotal))
            {
                ImGui::Spacing();
                drawGraph("VRAM Usage", "##vramusage", vramUsageHistory, 0.0f, 100.0f, "%.0f%%",
                          ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
            }
        }
        else
        {
            ImGui::Spacing();
            ImGui::TextDisabled("GPU stats not available");
            ImGui::TextDisabled("(No AMD/Intel/NVIDIA GPU detected via sysfs)");
        }

        // Game info
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Text("Game");
        ImGui::Separator();
        if (!detectedGameName.empty())
        {
            ImGui::Text("Executable: %s", detectedGameName.c_str());
            if (!autoDetectedConfig.empty())
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Config: %s.conf (auto-detected)", autoDetectedConfig.c_str());
            else
                ImGui::TextDisabled("No per-game config found");
        }
        else
        {
            ImGui::TextDisabled("Could not detect game executable");
        }

        // Credits and build info
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Credits");
        ImGui::TextDisabled("Original vkBasalt by");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("@DadSchoorse", "https://github.com/DadSchoorse/vkBasalt");
        ImGui::TextDisabled("Overlay fork by");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("@Boux", "https://github.com/Boux/vkBasalt_overlay");
        ImGui::TextDisabled("Wayland fork by");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("@Daaboulex", "https://github.com/Daaboulex/vkBasalt_overlay_wayland");

        ImGui::Spacing();
        ImGui::TextDisabled("Build #%d (%s)", BUILD_NUMBER, BUILD_DATE);
        ImGui::TextDisabled("Report issues:");
        ImGui::TextLinkOpenURL("github.com/Daaboulex/vkBasalt_overlay_wayland/issues", "https://github.com/Daaboulex/vkBasalt_overlay_wayland/issues");

        ImGui::EndChild();
    }

} // namespace vkBasalt
