// Standalone shader compilation tester for vkBasalt-overlay ReShade .fx files.
// Links against libreshade.a only — no Vulkan, no ImGui, no full vkbasalt deps.
//
// Compile:
//   g++ -std=c++20 -O2 -I../src -I../src/reshade \
//       tools/test_shaders.cpp \
//       -Lbuilddir/src/reshade -lreshade \
//       -o builddir/test_shaders
//
// Usage:
//   ./builddir/test_shaders [shader_dir ...]
//
// If no directories given, reads include paths from
// ~/.config/vkBasalt-overlay/shader_manager.conf and tests all .fx files found.

#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>
#include <setjmp.h>
#include <string>
#include <vector>

#include "reshade/effect_preprocessor.hpp"
#include "reshade/effect_parser.hpp"
#include "reshade/effect_codegen.hpp"
#include "reshade/effect_module.hpp"

namespace fs = std::filesystem;

// --- Signal-safe crash recovery (mirrors reshade_parser.cpp) ---

static thread_local sigjmp_buf s_jmpBuf;
static thread_local volatile sig_atomic_t s_jmpActive = 0;
static thread_local volatile sig_atomic_t s_caughtSignal = 0;

static void crashHandler(int sig)
{
    if (s_jmpActive)
    {
        s_caughtSignal = sig;
        siglongjmp(s_jmpBuf, 1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers()
{
    static bool installed = false;
    if (installed)
        return;
    struct sigaction sa = {};
    sa.sa_handler = crashHandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    installed = true;
}

// --- Preprocessor setup (mirrors addStandardMacros from reshade_parser.cpp) ---

static void addStandardMacros(reshadefx::preprocessor& pp)
{
    pp.add_macro_definition("__RESHADE__", std::to_string(INT_MAX));
    pp.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", "1");
    pp.add_macro_definition("__RENDERER__", "0x20000");
    pp.add_macro_definition("BUFFER_WIDTH", "1920");
    pp.add_macro_definition("BUFFER_HEIGHT", "1080");
    pp.add_macro_definition("BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)");
    pp.add_macro_definition("BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)");
    pp.add_macro_definition("BUFFER_COLOR_DEPTH", "8");
    pp.add_macro_definition("BUFFER_COLOR_BIT_DEPTH", "BUFFER_COLOR_DEPTH");

    // Function-like macros must use append_string (raw struct lacks parameter substitution markers)
    pp.append_string(
        "#define tex2DgatherR(s, coords) tex2Dgather(s, coords, 0)\n"
        "#define tex2DgatherG(s, coords) tex2Dgather(s, coords, 1)\n"
        "#define tex2DgatherB(s, coords) tex2Dgather(s, coords, 2)\n"
        "#define tex2DgatherA(s, coords) tex2Dgather(s, coords, 3)\n"
        "#define float2x3 matrix<float, 2, 3>\n"
        "#define float2x4 matrix<float, 2, 4>\n"
        "#define float3x2 matrix<float, 3, 2>\n"
        "#define float3x4 matrix<float, 3, 4>\n"
        "#define float4x2 matrix<float, 4, 2>\n"
        "#define float4x3 matrix<float, 4, 3>\n"
        "#define ddx_fine(x) ddx(x)\n"
        "#define ddy_fine(x) ddy(x)\n"
        "#define ddx_coarse(x) ddx(x)\n"
        "#define ddy_coarse(x) ddy(x)\n"
    );
}

// --- shader_manager.conf parser (standalone, avoids ConfigSerializer dependency) ---

struct ShaderManagerConfig
{
    std::vector<std::string> parentDirectories;
    std::vector<std::string> discoveredShaderPaths;
    std::vector<std::string> discoveredTexturePaths;
};

static ShaderManagerConfig loadShaderManagerConfig()
{
    ShaderManagerConfig config;
    const char* home = getenv("HOME");
    if (!home)
        return config;

    std::string configPath = std::string(home) + "/.config/vkBasalt-overlay/shader_manager.conf";
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        std::cerr << "warning: could not open " << configPath << "\n";
        return config;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(key);
        trim(value);

        if (key == "parentDir" && !value.empty())
            config.parentDirectories.push_back(value);
        else if (key == "shaderPath" && !value.empty())
            config.discoveredShaderPaths.push_back(value);
        else if (key == "texturePath" && !value.empty())
            config.discoveredTexturePaths.push_back(value);
    }

    return config;
}

// --- Error categorization ---

enum class ErrorCategory
{
    Preprocessor,
    Parse,
    Signal,
    Exception
};

static const char* categoryName(ErrorCategory cat)
{
    switch (cat)
    {
        case ErrorCategory::Preprocessor: return "PREPROCESSOR";
        case ErrorCategory::Parse:        return "PARSE";
        case ErrorCategory::Signal:       return "SIGNAL";
        case ErrorCategory::Exception:    return "EXCEPTION";
    }
    return "UNKNOWN";
}

static ErrorCategory categorizeError(const std::string& msg)
{
    if (msg.find("SIGFPE") != std::string::npos || msg.find("SIGABRT") != std::string::npos)
        return ErrorCategory::Signal;
    if (msg.find("Exception") != std::string::npos || msg.find("exception") != std::string::npos)
        return ErrorCategory::Exception;
    if (msg.find("Preprocessor") != std::string::npos || msg.find("Failed to load") != std::string::npos)
        return ErrorCategory::Preprocessor;
    return ErrorCategory::Parse;
}

// --- Test result ---

struct TestResult
{
    std::string name;
    std::string path;
    bool success = false;
    bool usesDepth = false;
    std::string errorMessage;
    ErrorCategory category = ErrorCategory::Parse;
};

// --- Core test function (mirrors testShaderCompilation) ---

static TestResult testShader(
    const std::string& effectName,
    const std::string& effectPath,
    const std::vector<std::string>& includePaths)
{
    TestResult result;
    result.name = effectName;
    result.path = effectPath;

    installCrashHandlers();
    if (sigsetjmp(s_jmpBuf, 1) != 0)
    {
        s_jmpActive = 0;
        std::string sigName = (s_caughtSignal == SIGFPE) ? "SIGFPE" : "SIGABRT";
        result.success = false;
        result.errorMessage = sigName + " signal during shader compilation";
        result.category = ErrorCategory::Signal;
        return result;
    }
    s_jmpActive = 1;

    try
    {
        reshadefx::preprocessor preprocessor;
        addStandardMacros(preprocessor);
        for (const auto& path : includePaths)
            preprocessor.add_include_path(path);

        if (!preprocessor.append_file(effectPath))
        {
            result.success = false;
            result.errorMessage = "Failed to load shader file";
            std::string ppErrors = preprocessor.errors();
            if (!ppErrors.empty())
                result.errorMessage += ": " + ppErrors;
            result.category = ErrorCategory::Preprocessor;
            s_jmpActive = 0;
            return result;
        }

        std::string ppErrors = preprocessor.errors();
        if (!ppErrors.empty() && ppErrors.find("preprocessor error:") != std::string::npos)
        {
            result.success = false;
            result.errorMessage = "Preprocessor errors: " + ppErrors;
            result.category = ErrorCategory::Preprocessor;
            s_jmpActive = 0;
            return result;
        }

        reshadefx::parser parser;
        auto codegen = std::unique_ptr<reshadefx::codegen>(
            reshadefx::create_codegen_spirv(true, true, true, true));

        if (!parser.parse(std::move(preprocessor.output()), codegen.get()))
        {
            result.success = false;
            result.errorMessage = "Parse errors: " + parser.errors();
            result.category = ErrorCategory::Parse;
            s_jmpActive = 0;
            return result;
        }

        std::string parseErrors = parser.errors();
        if (!parseErrors.empty())
        {
            // Warnings — still considered success
            result.success = true;
            result.errorMessage = "Warnings: " + parseErrors;
            s_jmpActive = 0;
            return result;
        }

        reshadefx::module module;
        codegen->write_result(module);

        for (const auto& tex : module.textures)
        {
            if (tex.semantic == "DEPTH")
            {
                result.usesDepth = true;
                break;
            }
        }

        result.success = true;
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.errorMessage = "Exception: " + std::string(e.what());
        result.category = ErrorCategory::Exception;
    }
    catch (...)
    {
        result.success = false;
        result.errorMessage = "Unknown exception during compilation";
        result.category = ErrorCategory::Exception;
    }

    s_jmpActive = 0;
    return result;
}

// --- Collect .fx files from a directory ---

static std::vector<fs::path> collectFxFiles(const std::string& dir)
{
    std::vector<fs::path> files;
    if (!fs::is_directory(dir))
    {
        std::cerr << "warning: not a directory: " << dir << "\n";
        return files;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir,
             fs::directory_options::follow_directory_symlink |
             fs::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        // Case-insensitive .fx check
        if (ext == ".fx" || ext == ".FX")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

// --- Main ---

int main(int argc, char* argv[])
{
    // Collect shader directories and include paths
    std::vector<std::string> shaderDirs;
    std::vector<std::string> includePaths;

    if (argc > 1)
    {
        // Directories from command line — also use them as include paths
        for (int i = 1; i < argc; i++)
        {
            shaderDirs.push_back(argv[i]);
            includePaths.push_back(argv[i]);
        }
    }
    else
    {
        // Read from shader_manager.conf
        auto config = loadShaderManagerConfig();
        includePaths = config.discoveredShaderPaths;

        if (includePaths.empty())
        {
            // Fallback default
            std::string defaultDir = "/etc/vkBasalt-overlay/reshade/Shaders";
            includePaths.push_back(defaultDir);
        }

        // Search all discovered shader paths for .fx files
        shaderDirs = includePaths;
    }

    // Also add texture paths as include paths (some shaders include from there)
    if (argc <= 1)
    {
        auto config = loadShaderManagerConfig();
        for (const auto& tp : config.discoveredTexturePaths)
            includePaths.push_back(tp);
    }

    std::cout << "Include paths:\n";
    for (const auto& p : includePaths)
        std::cout << "  " << p << "\n";
    std::cout << "\n";

    // Collect all .fx files
    std::vector<fs::path> allFiles;
    for (const auto& dir : shaderDirs)
    {
        auto files = collectFxFiles(dir);
        allFiles.insert(allFiles.end(), files.begin(), files.end());
    }

    // Deduplicate (in case overlapping directories)
    std::sort(allFiles.begin(), allFiles.end());
    allFiles.erase(std::unique(allFiles.begin(), allFiles.end()), allFiles.end());

    if (allFiles.empty())
    {
        std::cerr << "No .fx files found in the specified directories.\n";
        return 1;
    }

    std::cout << "Testing " << allFiles.size() << " shader(s)...\n\n";

    // Run tests
    int passCount = 0;
    int failCount = 0;
    int warnCount = 0;
    int depthCount = 0;
    std::vector<TestResult> failures;
    std::vector<TestResult> warnings;

    for (const auto& fxPath : allFiles)
    {
        std::string effectName = fxPath.stem().string();
        std::string effectPath = fxPath.string();

        TestResult result = testShader(effectName, effectPath, includePaths);

        if (result.success)
        {
            if (!result.errorMessage.empty())
            {
                // Has warnings
                std::cout << "WARN  " << effectName << "\n";
                warnCount++;
                warnings.push_back(result);
            }
            else
            {
                std::cout << "PASS  " << effectName;
                if (result.usesDepth)
                {
                    std::cout << "  [uses depth]";
                    depthCount++;
                }
                std::cout << "\n";
            }
            passCount++;
        }
        else
        {
            std::cout << "FAIL  " << effectName << "  [" << categoryName(result.category) << "]\n";
            failCount++;
            failures.push_back(result);
        }
    }

    // --- Summary ---

    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  RESULTS: " << passCount << " passed, " << failCount << " failed";
    if (warnCount > 0)
        std::cout << ", " << warnCount << " warnings";
    if (depthCount > 0)
        std::cout << " (" << depthCount << " use depth)";
    std::cout << "\n";
    std::cout << "  Total:   " << allFiles.size() << " shaders\n";
    std::cout << "========================================\n";

    // --- Grouped failure details ---

    if (!failures.empty())
    {
        // Group by category
        std::vector<std::pair<ErrorCategory, std::vector<const TestResult*>>> grouped;
        auto addToGroup = [&](ErrorCategory cat, const TestResult* r) {
            for (auto& [gc, vec] : grouped)
            {
                if (gc == cat) { vec.push_back(r); return; }
            }
            grouped.push_back({cat, {r}});
        };

        for (const auto& f : failures)
            addToGroup(f.category, &f);

        std::cout << "\n--- FAILURES BY CATEGORY ---\n";
        for (const auto& [cat, results] : grouped)
        {
            std::cout << "\n[" << categoryName(cat) << "] (" << results.size() << " shader(s))\n";
            for (const auto* r : results)
            {
                std::cout << "  " << r->name << "\n";
                std::cout << "    Path:  " << r->path << "\n";
                // Truncate long error messages per line
                std::string msg = r->errorMessage;
                // Print each line of the error indented
                size_t pos = 0;
                bool first = true;
                while (pos < msg.size())
                {
                    size_t nl = msg.find('\n', pos);
                    std::string line = (nl == std::string::npos)
                        ? msg.substr(pos) : msg.substr(pos, nl - pos);
                    if (first)
                        std::cout << "    Error: " << line << "\n";
                    else
                        std::cout << "           " << line << "\n";
                    first = false;
                    if (nl == std::string::npos)
                        break;
                    pos = nl + 1;
                }
            }
        }
    }

    // --- Warning details ---

    if (!warnings.empty())
    {
        std::cout << "\n--- WARNINGS ---\n";
        for (const auto& w : warnings)
        {
            std::cout << "  " << w.name << "\n";
            std::string msg = w.errorMessage;
            size_t pos = 0;
            bool first = true;
            while (pos < msg.size())
            {
                size_t nl = msg.find('\n', pos);
                std::string line = (nl == std::string::npos)
                    ? msg.substr(pos) : msg.substr(pos, nl - pos);
                if (first)
                    std::cout << "    " << line << "\n";
                else
                    std::cout << "    " << line << "\n";
                first = false;
                if (nl == std::string::npos)
                    break;
                pos = nl + 1;
            }
        }
    }

    return (failCount > 0) ? 1 : 0;
}
