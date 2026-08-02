#pragma once

// The ReShade FX feature level this build implements, in ReShade's own encoding
// (major * 10000 + minor * 100 + revision); shaders gate on it as __RESHADE__.
// The vendored compiler is crosire/reshade 6.7.3; this number is the highest
// level whose RUNTIME contract the layer keeps (compute, .cube LUTs, colour
// space defines), held to that by the shader corpus baseline.
#define VKBASALT_RESHADE_FX_VERSION 60500
