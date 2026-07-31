#pragma once

// The compile environment every ReShade .fx sees: the macros it is given and the
// features this build refuses. Header-only and included by both the layer and the
// standalone shader tester, so the tester cannot report a result the layer would
// not produce.

#include <string>

#include "reshade/effect_preprocessor.hpp"
#include "reshade_fx_version.hpp"

namespace vkBasalt
{
    inline void addReshadeBaseMacros(reshadefx::preprocessor& pp)
    {
        pp.add_macro_definition("__RESHADE__", std::to_string(VKBASALT_RESHADE_FX_VERSION));
        pp.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", "1");
        pp.add_macro_definition("__RENDERER__", "0x20000");

        // This compiler takes a sampling offset as an extra argument rather than under a separate
        // name, so the older spellings are aliased onto it. The per-component gathers are native
        // here and must not be redefined, or they resolve to a function that no longer exists.
        pp.append_string("#define tex2Doffset(s, coords, offset) tex2D(s, coords, offset)\n"
                         "#define tex2Dlodoffset(s, coords, offset) tex2Dlod(s, coords, offset)\n"
                         "#define tex2DgatherRoffset(s, coords, offset) tex2DgatherR(s, coords, offset)\n"
                         "#define tex2DgatherGoffset(s, coords, offset) tex2DgatherG(s, coords, offset)\n"
                         "#define tex2DgatherBoffset(s, coords, offset) tex2DgatherB(s, coords, offset)\n"
                         "#define tex2DgatherAoffset(s, coords, offset) tex2DgatherA(s, coords, offset)\n"
                         "#define ddx_fine(x) ddx(x)\n"
                         "#define ddy_fine(x) ddy(x)\n"
                         "#define ddx_coarse(x) ddx(x)\n"
                         "#define ddy_coarse(x) ddy(x)\n");
    }

    // Translates a compiler error into a plain sentence naming the ReShade
    // version the shader wants, rather than an identifier the reader has never
    // heard of. Empty when the failure is something else.
    //
    // This reads the ERROR, never the source. Scanning the source for these
    // tokens refuses shaders that merely mention one in a comment or behind
    // their own fallback define, and those compile perfectly well.
    inline std::string reshadeUnsupportedFeature(const std::string& compilerError)
    {
        struct Feature
        {
            const char* token;
            const char* needs;
            const char* what;
        };
        static const Feature features[] = {
            {"ComputeShader", "4.8", "compute shaders"},
            {"atomicAdd", "4.8", "texture atomics"},
            {"atomicMin", "4.8", "texture atomics"},
            {"atomicMax", "4.8", "texture atomics"},
            {"atomicExchange", "4.8", "texture atomics"},
            {"atomicCompSwap", "4.8", "texture atomics"},
            {"tex2Dstore", "4.8", "storage-image writes"},
            {"min16float", "4.8", "minimum-precision types"},
        };

        for (const auto& f : features)
        {
            if (compilerError.find(f.token) == std::string::npos)
                continue;

            int major = VKBASALT_RESHADE_FX_VERSION / 10000;
            int minor = (VKBASALT_RESHADE_FX_VERSION / 100) % 100;
            return std::string("needs ReShade ") + f.needs + " or newer (uses " + f.what + "); this build implements "
                   + std::to_string(major) + "." + std::to_string(minor);
        }
        return {};
    }
} // namespace vkBasalt
