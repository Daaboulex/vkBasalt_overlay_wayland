#pragma once

// The ReShade FX feature level this build implements, in ReShade's own encoding
// (major * 10000 + minor * 100 + revision). Shaders gate on it as __RESHADE__.
//
// src/reshade is vendored from crosire/reshade at 39350df, which is the v4.7.0
// tag, plus f32tof16 and f16tof32 backported into the intrinsic table. Re-syncing
// that tree means raising this to the version synced, or shaders silently keep
// taking the older path. 4.8.0 is the first release with real compute-shader
// support; this tree still compiles a ComputeShader as a pixel shader and has no
// storage-image writes or barriers, so 40700 remains the honest ceiling -- the
// backported pair does not move it, because a shader gated on 4.8 wants compute,
// not two conversion functions.
#define VKBASALT_RESHADE_FX_VERSION 40700
