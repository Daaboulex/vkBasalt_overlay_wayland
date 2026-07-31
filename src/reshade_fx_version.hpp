#pragma once

// The ReShade FX feature level this build implements, in ReShade's own encoding
// (major * 10000 + minor * 100 + revision). Shaders gate on it as __RESHADE__.
//
// src/reshade is vendored from crosire/reshade v6.7.3, so the language itself is
// current. This number is deliberately lower, because a shader gates on it to ask
// what the RUNTIME provides, not what the compiler parses. It is the highest
// version whose runtime contract this layer actually keeps:
//
//   4.8  real compute-shader support -- kept: passes declaring ComputeShader are
//        dispatched, storage images are written and barriers are emitted, and the
//        SPIR-V is validated by checks.compute-spirv-is-valid.
//   6.0  .cube LUT files as a texture source -- NOT kept.
//   6.5  HDR swapchain colour space behind BUFFER_COLOR_SPACE -- NOT kept.
//
// 5.x is not claimed yet: its shader-facing runtime additions have not been read
// from the release notes, and this number is never raised on the assumption that
// nothing was added. Raise it only for a version whose contract is kept and shown.
#define VKBASALT_RESHADE_FX_VERSION 40800
