#include "imgui_overlay.hpp"
#include "config_serializer.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <fstream>
#include <filesystem>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
#include <unordered_map>
#include <dlfcn.h>

#include "imgui/imgui.h"

namespace vkBasalt
{
    namespace
    {
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


        enum class GpuVendor { Unknown, AMD, Intel, NVIDIA };

        using nvmlReturn_t = unsigned int;
        using nvmlDevice_t = void*;
        struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
        struct nvmlMemory_t { unsigned long long total; unsigned long long free; unsigned long long used; };
        struct nvmlPciInfo_t
        {
            char busIdLegacy[16];
            unsigned int domain;
            unsigned int bus;
            unsigned int device;
            unsigned int pciDeviceId;
            unsigned int pciSubSystemId;
            char busId[32];
        };

        static constexpr nvmlReturn_t NVML_SUCCESS = 0;

        struct NvmlState
        {
            void* lib = nullptr;
            bool initialized = false;

            nvmlReturn_t (*Init)() = nullptr;
            nvmlReturn_t (*Shutdown)() = nullptr;
            nvmlReturn_t (*DeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*) = nullptr;
            nvmlReturn_t (*DeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
            nvmlReturn_t (*DeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*) = nullptr;
            nvmlReturn_t (*DeviceGetCount)(unsigned int*) = nullptr;
            nvmlReturn_t (*DeviceGetPciInfo)(nvmlDevice_t, nvmlPciInfo_t*) = nullptr;
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
            nvml.DeviceGetPciInfo = (decltype(nvml.DeviceGetPciInfo))dlsym(nvml.lib, "nvmlDeviceGetPciInfo_v3");

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

            Logger::info("NVML initialized for GPU diagnostics");
            return true;
        }

        struct GpuEntry
        {
            GpuVendor    vendor = GpuVendor::Unknown;
            std::string  name;
            std::string  pciAddress;
            std::string  drmCardPath;
            std::vector<std::string> drmNodes;
            uint16_t     pciDeviceId = 0;
            bool         vulkanUsable = false;
            bool         software     = false;
            bool         isRenderer   = false;
            uint32_t     apiVersion   = 0;
            nvmlDevice_t nvmlDevice   = nullptr;
            std::string  note;
            float        busyPercent = -1.0f;
            float        vramUsedMB  = -1.0f;
            float        vramTotalMB = -1.0f;
            float        gttUsedMB   = -1.0f;
            float        gttTotalMB  = -1.0f;
            std::vector<std::string> users;
        };

        static std::vector<GpuEntry> gpuInventory;

        template<typename T>
        bool readSysfs(const std::string& path, T& value)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            file >> value;
            return !file.fail();
        }

        static uint16_t readHexId(const std::string& path)
        {
            std::ifstream f(path);
            if (!f.is_open())
                return 0;
            uint16_t id = 0;
            f >> std::hex >> id;
            return id;
        }

        static std::string pciSlotFromUevent(const std::string& devicePath)
        {
            std::ifstream f(devicePath + "/uevent");
            std::string line;
            while (std::getline(f, line))
            {
                if (line.rfind("PCI_SLOT_NAME=", 0) == 0)
                {
                    std::string slot = line.substr(14);
                    std::transform(slot.begin(), slot.end(), slot.begin(), [](unsigned char c) { return std::tolower(c); });
                    return slot;
                }
            }
            return "";
        }

        static void enumerateDrmCards(std::vector<GpuEntry>& inventory)
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm"))
                {
                    std::string node = entry.path().filename().string();
                    bool isCard = node.rfind("card", 0) == 0 && node.find('-') == std::string::npos;
                    bool isRender = node.rfind("renderD", 0) == 0;
                    if (!isCard && !isRender)
                        continue;

                    std::string devicePath = entry.path().string() + "/device";
                    std::string pci = pciSlotFromUevent(devicePath);
                    if (pci.empty())
                        continue;

                    GpuEntry* target = nullptr;
                    for (auto& e : inventory)
                        if (e.pciAddress == pci)
                            target = &e;
                    if (!target)
                    {
                        GpuEntry e;
                        e.pciAddress = pci;
                        uint16_t vendorId = readHexId(devicePath + "/vendor");
                        e.pciDeviceId = readHexId(devicePath + "/device");
                        switch (vendorId)
                        {
                            case 0x1002: e.vendor = GpuVendor::AMD; e.name = "AMD GPU " + pci; break;
                            case 0x8086: e.vendor = GpuVendor::Intel; e.name = "Intel GPU " + pci; break;
                            case 0x10de: e.vendor = GpuVendor::NVIDIA; e.name = "NVIDIA GPU " + pci; break;
                            default: e.name = "GPU " + pci; break;
                        }
                        e.note = "no Vulkan driver reachable for this device";
                        inventory.push_back(std::move(e));
                        target = &inventory.back();
                    }

                    if (isCard)
                        target->drmCardPath = entry.path().string();
                    target->drmNodes.push_back("/dev/dri/" + node);
                }
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                Logger::debug(std::string("GPU inventory: sysfs scan failed: ") + e.what());
            }
        }

        static bool physicalDeviceHasPciBusInfo(LogicalDevice* device, VkPhysicalDevice pd)
        {
            if (!device->vki.EnumerateDeviceExtensionProperties)
                return false;
            uint32_t count = 0;
            if (device->vki.EnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
                return false;
            std::vector<VkExtensionProperties> exts(count);
            device->vki.EnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, "VK_EXT_pci_bus_info") == 0)
                    return true;
            return false;
        }

        static void attachVulkanDevices(LogicalDevice* device, std::vector<GpuEntry>& inventory)
        {
            if (!device || !device->instance || !device->vki.EnumeratePhysicalDevices)
                return;

            uint32_t count = 0;
            if (device->vki.EnumeratePhysicalDevices(device->instance, &count, nullptr) != VK_SUCCESS || count == 0)
                return;
            std::vector<VkPhysicalDevice> physicalDevices(count);
            device->vki.EnumeratePhysicalDevices(device->instance, &count, physicalDevices.data());

            for (VkPhysicalDevice pd : physicalDevices)
            {
                VkPhysicalDeviceProperties props = {};
                device->vki.GetPhysicalDeviceProperties(pd, &props);

                std::string pci;
                if (device->vki.GetPhysicalDeviceProperties2 && physicalDeviceHasPciBusInfo(device, pd))
                {
                    VkPhysicalDevicePCIBusInfoPropertiesEXT pciProps = {};
                    pciProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
                    VkPhysicalDeviceProperties2 props2 = {};
                    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                    props2.pNext = &pciProps;
                    device->vki.GetPhysicalDeviceProperties2(pd, &props2);
                    char buf[24];
                    snprintf(buf, sizeof(buf), "%04x:%02x:%02x.%x", pciProps.pciDomain, pciProps.pciBus, pciProps.pciDevice, pciProps.pciFunction);
                    pci = buf;
                }

                GpuEntry* match = nullptr;
                if (!pci.empty())
                    for (auto& e : inventory)
                        if (e.pciAddress == pci)
                            match = &e;
                if (!match)
                {
                    int candidates = 0;
                    GpuEntry* byId = nullptr;
                    for (auto& e : inventory)
                        if (!e.vulkanUsable && e.pciDeviceId != 0 && e.pciDeviceId == (props.deviceID & 0xFFFF))
                        {
                            byId = &e;
                            candidates++;
                        }
                    if (candidates == 1)
                        match = byId;
                }
                if (!match)
                {
                    inventory.push_back({});
                    match = &inventory.back();
                }

                match->vulkanUsable = true;
                match->name = props.deviceName;
                match->apiVersion = props.apiVersion;
                match->software = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
                match->isRenderer = (pd == device->physicalDevice);
                match->note = match->software ? "software rasterizer" : "";
                if (match->vendor == GpuVendor::Unknown)
                {
                    switch (props.vendorID)
                    {
                        case 0x1002: match->vendor = GpuVendor::AMD; break;
                        case 0x8086: match->vendor = GpuVendor::Intel; break;
                        case 0x10de: match->vendor = GpuVendor::NVIDIA; break;
                        default: break;
                    }
                }
            }
        }

        static void attachNvmlDevices(std::vector<GpuEntry>& inventory)
        {
            bool anyNvidia = false;
            for (const auto& e : inventory)
                anyNvidia = anyNvidia || e.vendor == GpuVendor::NVIDIA;
            if (!anyNvidia || !initNvml())
                return;

            unsigned int count = 0;
            if (nvml.DeviceGetCount && nvml.DeviceGetCount(&count) == NVML_SUCCESS && nvml.DeviceGetPciInfo)
            {
                for (unsigned int i = 0; i < count; i++)
                {
                    nvmlDevice_t handle = nullptr;
                    if (nvml.DeviceGetHandleByIndex(i, &handle) != NVML_SUCCESS)
                        continue;
                    nvmlPciInfo_t info = {};
                    if (nvml.DeviceGetPciInfo(handle, &info) != NVML_SUCCESS)
                        continue;
                    char buf[24];
                    snprintf(buf, sizeof(buf), "%04x:%02x:%02x.0", info.domain & 0xFFFF, info.bus, info.device);
                    for (auto& e : inventory)
                        if (e.pciAddress == buf)
                            e.nvmlDevice = handle;
                }
                return;
            }

            nvmlDevice_t handle = nullptr;
            if (nvml.DeviceGetHandleByIndex(0, &handle) == NVML_SUCCESS)
                for (auto& e : inventory)
                    if (e.vendor == GpuVendor::NVIDIA && !e.nvmlDevice)
                    {
                        e.nvmlDevice = handle;
                        break;
                    }
        }

        static void sampleDrmUsers(std::vector<GpuEntry>& inventory)
        {
            std::unordered_map<std::string, GpuEntry*> nodeToEntry;
            for (auto& e : inventory)
            {
                e.users.clear();
                for (const auto& n : e.drmNodes)
                    nodeToEntry[n] = &e;
            }
            if (nodeToEntry.empty())
                return;

            try
            {
                for (const auto& proc : std::filesystem::directory_iterator("/proc"))
                {
                    const std::string pid = proc.path().filename().string();
                    if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos)
                        continue;

                    std::error_code ec;
                    std::set<GpuEntry*> hits;
                    for (const auto& fd : std::filesystem::directory_iterator(proc.path() / "fd", ec))
                    {
                        std::error_code lec;
                        const std::filesystem::path target = std::filesystem::read_symlink(fd.path(), lec);
                        if (lec)
                            continue;
                        const auto it = nodeToEntry.find(target.string());
                        if (it != nodeToEntry.end())
                            hits.insert(it->second);
                    }
                    if (ec || hits.empty())
                        continue;

                    std::ifstream comm(proc.path() / "comm");
                    std::string name;
                    std::getline(comm, name);
                    if (name.empty())
                        name = pid;
                    for (GpuEntry* e : hits)
                        if (e->users.size() < 6 && std::find(e->users.begin(), e->users.end(), name) == e->users.end())
                            e->users.push_back(name);
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
            }
        }

        static void sampleGpuStats(GpuEntry& e)
        {
            e.busyPercent = -1.0f;
            e.vramUsedMB = e.vramTotalMB = -1.0f;
            e.gttUsedMB = e.gttTotalMB = -1.0f;

            if (!e.drmCardPath.empty() && (e.vendor == GpuVendor::AMD || e.vendor == GpuVendor::Intel))
            {
                const std::string dev = e.drmCardPath + "/device";
                if (e.vendor == GpuVendor::AMD)
                {
                    int busy = 0;
                    if (readSysfs(dev + "/gpu_busy_percent", busy))
                        e.busyPercent = static_cast<float>(busy);
                }
                else
                {
                    int actFreq = 0, maxFreq = 0;
                    if (readSysfs(dev + "/gt_act_freq_mhz", actFreq) && readSysfs(dev + "/gt_max_freq_mhz", maxFreq) && maxFreq > 0)
                        e.busyPercent = std::min(100.0f, (static_cast<float>(actFreq) / static_cast<float>(maxFreq)) * 100.0f);
                }

                uint64_t used = 0, total = 0;
                if (readSysfs(dev + "/mem_info_vram_used", used) && readSysfs(dev + "/mem_info_vram_total", total) && total > 0)
                {
                    e.vramUsedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                    e.vramTotalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                }
                if (readSysfs(dev + "/mem_info_gtt_used", used) && readSysfs(dev + "/mem_info_gtt_total", total) && total > 0)
                {
                    e.gttUsedMB = static_cast<float>(used) / (1024.0f * 1024.0f);
                    e.gttTotalMB = static_cast<float>(total) / (1024.0f * 1024.0f);
                }
                return;
            }

            if (e.nvmlDevice && nvml.lib)
            {
                nvmlUtilization_t util = {};
                if (nvml.DeviceGetUtilizationRates(e.nvmlDevice, &util) == NVML_SUCCESS)
                    e.busyPercent = static_cast<float>(util.gpu);
                nvmlMemory_t mem = {};
                if (nvml.DeviceGetMemoryInfo(e.nvmlDevice, &mem) == NVML_SUCCESS && mem.total > 0)
                {
                    e.vramUsedMB = static_cast<float>(mem.used) / (1024.0f * 1024.0f);
                    e.vramTotalMB = static_cast<float>(mem.total) / (1024.0f * 1024.0f);
                }
            }
        }

        static GpuEntry* rendererEntry()
        {
            for (auto& e : gpuInventory)
                if (e.isRenderer)
                    return &e;
            return nullptr;
        }

        static void buildGpuInventory(LogicalDevice* device)
        {
            gpuInventory.clear();
            enumerateDrmCards(gpuInventory);
            attachVulkanDevices(device, gpuInventory);
            attachNvmlDevices(gpuInventory);

            std::stable_sort(gpuInventory.begin(), gpuInventory.end(), [](const GpuEntry& a, const GpuEntry& b) {
                if (a.isRenderer != b.isRenderer)
                    return a.isRenderer;
                if (a.software != b.software)
                    return b.software;
                return a.pciAddress < b.pciAddress;
            });

            for (const auto& e : gpuInventory)
                Logger::info("GPU inventory: " + e.name + (e.pciAddress.empty() ? "" : " at " + e.pciAddress)
                             + (e.vulkanUsable ? " (Vulkan)" : " (no Vulkan)") + (e.isRenderer ? " [renderer]" : ""));
        }

        static RingBuffer<float, 300> frameTimeHistory;
        static RingBuffer<float, 300> gpuUsageHistory;
        static RingBuffer<float, 300> vramUsageHistory;
        static RingBuffer<float, 300> gttUsageHistory;
        static std::chrono::steady_clock::time_point lastFrameTime;
        static std::string detectedGameName;
        static std::string autoDetectedConfig;


        void drawGraph(const char* label, const char* id, RingBuffer<float, 300>& history, float minVal, float maxVal,
                       const char* overlayFmt, ImVec4 color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f))
        {
            ImGui::Text("%s", label);

            float data[300];
            history.copyTo(data);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

            char overlay[64];
            snprintf(overlay, sizeof(overlay), overlayFmt, history.size() > 0 ? history.get(history.size() - 1) : 0.0f);

            ImGui::PlotLines(id, data, static_cast<int>(history.size()), 0, overlay,
                            minVal, maxVal, ImVec2(-1, 60));

            ImGui::PopStyleColor(2);

            if (history.size() > 0)
            {
                ImGui::TextDisabled("Min: %.1f  Avg: %.1f  Max: %.1f",
                    history.min(), history.avg(), history.max());
            }
        }
    }

    void ImGuiOverlay::renderDiagnosticsView()
    {
        static bool initialized = false;
        if (!initialized)
        {
            buildGpuInventory(pLogicalDevice);
            detectedGameName = ConfigSerializer::detectGameName();
            autoDetectedConfig = ConfigSerializer::autoDetectConfig();
            lastFrameTime = std::chrono::steady_clock::now();
            initialized = true;
        }

        auto now = std::chrono::steady_clock::now();
        float frameTimeMs = std::chrono::duration<float, std::milli>(now - lastFrameTime).count();
        lastFrameTime = now;

        // Only record if reasonable (avoid spikes from tab switching)
        if (frameTimeMs > 0.1f && frameTimeMs < 500.0f)
            frameTimeHistory.push(frameTimeMs);

        // Fixed wall-clock sampling keeps the overhead constant at any frame rate.
        static auto lastGpuSampleTime = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGpuSampleTime).count() >= 200)
        {
            lastGpuSampleTime = now;

            for (auto& entry : gpuInventory)
                sampleGpuStats(entry);

            if (GpuEntry* renderer = rendererEntry())
            {
                if (renderer->busyPercent >= 0)
                    gpuUsageHistory.push(renderer->busyPercent);
                if (renderer->vramTotalMB > 0)
                    vramUsageHistory.push((renderer->vramUsedMB / renderer->vramTotalMB) * 100.0f);
                if (renderer->gttTotalMB > 0)
                    gttUsageHistory.push((renderer->gttUsedMB / renderer->gttTotalMB) * 100.0f);
            }
        }

        static auto lastUsersSampleTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUsersSampleTime).count() >= 3000)
        {
            lastUsersSampleTime = now;
            sampleDrmUsers(gpuInventory);
        }

        ImGui::BeginChild("DiagnosticsContent", ImVec2(0, 0), false);

        float avgFrameTime = frameTimeHistory.avg();
        float fps = avgFrameTime > 0 ? 1000.0f / avgFrameTime : 0;
        float fps1Low = frameTimeHistory.max() > 0 ? 1000.0f / frameTimeHistory.max() : 0;

        if (conflictingLayerLoaded())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Conflict: the unforked vkBasalt is loaded too");
            ImGui::TextWrapped("Loaded from: %s", conflictingLayerPath().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Both layers share ENABLE_VKBASALT, so both are active: effects apply twice and\n"
                                  "DISABLE_VKBASALT cannot turn off one without the other. The path above names\n"
                                  "which install (system package, container, or Proton bundle) brought it.");
            ImGui::Separator();
        }

        ImGui::Text("Performance");
        ImGui::Separator();

        ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
        ImGui::PushFont(font, font->LegacySize);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("(1%% low: %.0f)", fps1Low);

        ImGui::Spacing();

        drawGraph("Frame Time", "##frametime", frameTimeHistory, 0.0f, 50.0f, "%.1f ms",
                  ImVec4(0.4f, 0.8f, 0.4f, 1.0f));

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::Text("GPUs (%zu)", gpuInventory.size());
        ImGui::Separator();

        if (gpuInventory.empty())
        {
            ImGui::TextDisabled("No GPU could be enumerated -- neither sysfs nor the Vulkan instance answered");
        }

        for (const GpuEntry& entry : gpuInventory)
        {
            if (entry.isRenderer)
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s  --  rendering the effects", entry.name.c_str());
            else
                ImGui::Text("%s", entry.name.c_str());

            std::string meta;
            if (!entry.pciAddress.empty())
                meta += entry.pciAddress;
            if (entry.vulkanUsable)
            {
                if (!meta.empty())
                    meta += "  |  ";
                meta += "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(entry.apiVersion)) + "."
                        + std::to_string(VK_API_VERSION_MINOR(entry.apiVersion));
            }
            if (!entry.note.empty())
            {
                if (!meta.empty())
                    meta += "  |  ";
                meta += entry.note;
            }
            if (!meta.empty())
                ImGui::TextDisabled("%s", meta.c_str());

            if (!entry.users.empty())
            {
                std::string users = "used by: ";
                for (size_t i = 0; i < entry.users.size(); i++)
                    users += (i ? ", " : "") + entry.users[i];
                ImGui::TextDisabled("%s", users.c_str());
            }
            else if (!entry.drmNodes.empty() && !entry.isRenderer)
            {
                ImGui::TextDisabled("used by: nothing in this session");
            }

            if (entry.isRenderer)
            {
                ImGui::Spacing();
                if (gpuUsageHistory.size() > 0)
                {
                    const char* usageLabel = (entry.vendor == GpuVendor::Intel) ? "GPU Frequency" : "GPU Usage";
                    drawGraph(usageLabel, "##gpuusage", gpuUsageHistory, 0.0f, 100.0f, "%.0f%%",
                              ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
                    if (entry.vendor == GpuVendor::Intel)
                        ImGui::TextDisabled("(estimated from frequency ratio)");
                    ImGui::Spacing();
                }

                if (entry.vramTotalMB > 0)
                {
                    ImGui::Text("VRAM: %.0f / %.0f MB", entry.vramUsedMB, entry.vramTotalMB);
                    ImGui::ProgressBar(entry.vramUsedMB / entry.vramTotalMB, ImVec2(-1, 0));
                }
                if (entry.gttTotalMB > 0)
                {
                    ImGui::Text("GTT (shared): %.0f / %.0f MB", entry.gttUsedMB, entry.gttTotalMB);
                    ImGui::ProgressBar(entry.gttUsedMB / entry.gttTotalMB, ImVec2(-1, 0));
                    ImGui::Spacing();
                    drawGraph("Memory Usage", "##gttusage", gttUsageHistory, 0.0f, 100.0f, "%.0f%%",
                              ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
                }
                else if (entry.vramTotalMB > 0 && vramUsageHistory.size() > 0)
                {
                    ImGui::Spacing();
                    drawGraph("VRAM Usage", "##vramusage", vramUsageHistory, 0.0f, 100.0f, "%.0f%%",
                              ImVec4(0.6f, 0.4f, 0.8f, 1.0f));
                }
                if (entry.busyPercent < 0 && entry.vramTotalMB <= 0)
                    ImGui::TextDisabled("no sensors readable for this device");
            }
            else
            {
                std::string stats;
                if (entry.busyPercent >= 0)
                    stats += "usage " + std::to_string(static_cast<int>(entry.busyPercent)) + "%";
                if (entry.vramTotalMB > 0)
                {
                    if (!stats.empty())
                        stats += "   ";
                    char vram[48];
                    snprintf(vram, sizeof(vram), "VRAM %.1f / %.1f GB", entry.vramUsedMB / 1024.0f, entry.vramTotalMB / 1024.0f);
                    stats += vram;
                }
                if (!stats.empty())
                    ImGui::TextDisabled("%s", stats.c_str());
                else if (!entry.software)
                    ImGui::TextDisabled("no sensors readable for this device");
            }

            ImGui::Spacing();
        }

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
        ImGui::TextDisabled("Report issues:");
        ImGui::TextLinkOpenURL("github.com/Daaboulex/vkBasalt_overlay_wayland/issues", "https://github.com/Daaboulex/vkBasalt_overlay_wayland/issues");

        ImGui::EndChild();
    }

} // namespace vkBasalt
