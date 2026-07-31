#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    struct CompiledReshadeEffect
    {
        reshadefx::effect_module module;
        // One module per entry point: this compiler assembles each separately, with the code the
        // others need stripped out.
        std::map<std::string, std::vector<uint32_t>> entryPointSpirv;
        std::vector<std::pair<std::string, uint64_t>> includedFiles;
        std::vector<std::pair<std::string, std::string>> usedMacros;
        bool usesDepth = false;
        std::string warnings;
        std::string error;

        bool ok() const { return error.empty(); }
    };

    // Compiles a ReShade effect, or returns it from the in-memory/disk cache.
    // The cache key covers the macro definitions, include paths, and the .fx
    // content; entries also record every included file's content hash and are
    // invalidated when any of them changes. Compile failures are cached too.
    // The base ReShade macros and compatibility stubs are added internally;
    // pass only the varying definitions (BUFFER_*, user macros).
    // Never returns null. The reshadefx compiler can raise SIGFPE/SIGABRT;
    // callers keep their signal guards around this call.
    std::shared_ptr<const CompiledReshadeEffect> getOrCompileReshadeEffect(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths);

} // namespace vkBasalt
