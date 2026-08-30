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
        bool usesMinPrecision = false;
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
    // relaxMinPrecision lets the driver compute a shader's declared half or
    // min16float math at 16 bits; it is part of the cache key, so both variants
    // of an effect coexist on disk.
    // liveUniforms keeps ordinary ReShade parameters in the runtime uniform
    // buffer. It is opt-in and part of the cache key; false preserves the
    // specialization-constant path.
    std::shared_ptr<const CompiledReshadeEffect> getOrCompileReshadeEffect(
        const std::string& fxPath,
        const std::vector<std::pair<std::string, std::string>>& macroDefinitions,
        const std::vector<std::string>& includePaths,
        bool relaxMinPrecision = false,
        bool liveUniforms = false);

    // Serializes and deserializes the entry in memory, then compares every field
    // the renderer reads. Returns the first field that failed to round-trip, or
    // an empty string when the cache is lossless for this entry.
    std::string cacheRoundTripMismatch(const CompiledReshadeEffect& e);

    // The renderer decides depth use from the SPIR-V call graph: a shader counts
    // only when an entry point can reach a load of a DEPTH-semantic sampler.
    bool moduleUsesDepth(const reshadefx::effect_module& module,
                         const std::map<std::string, std::vector<uint32_t>>& entryPointSpirv);

} // namespace vkBasalt
