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
//   6.0  .cube LUT files as a texture source -- kept: parsed and uploaded as a 3D
//        image, and sampled through a 3D sampler by scripts/e2e-smoke.sh.
//   6.5  HDR swapchain colour space behind BUFFER_COLOR_SPACE -- NOT kept, which
//        is why this stops at 6.0.
//
// What backs 6.0 is measurement, not a reading of every release note between here
// and it: all 504 shaders in the official index were compiled at this level, six
// more pass than at 4.8, none regressed, and none of the 2291 emitted modules is
// invalid. That is evidence about the shaders that exist, not proof that no 5.x
// addition went unnoticed, and the difference is why 6.5 stays unclaimed: there
// the missing capability is known and named.
#define VKBASALT_RESHADE_FX_VERSION 60500
