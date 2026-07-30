# ReShade shader compatibility

Measured, not estimated. Every `.fx` in the shader packs listed below was compiled
through this layer's own compile environment and the result recorded.

**Result as of 2026-07-30: 459 of 504 shaders compile to valid SPIR-V (91%).**

Two more compile but emit SPIR-V a driver would reject; they are counted as
failures here, not as passes. 43 do not compile. Compiling is not the same as
working, so the corpus validates every module it emits and an invalid one is
demoted rather than reported green.

## What was tested

The corpus is every package in ReShade's own official index,
[`EffectPackages.ini`](https://github.com/crosire/reshade-shaders/blob/list/EffectPackages.ini)
(`crosire/reshade-shaders`, `list` branch) — not a hand-picked selection. 41 of the
43 listed repositories were reachable and were cloned at their default branch;
`JakobPCoder/Reshade-Shades` could not be fetched.

Every directory containing a `.fx` or `.fxh` became an include path, so cross-pack
includes resolve as they would in a real install.

akgunter/crt-royale-reshade, AlexTuduran/FGFX, AlucardDH/dh-reshade-shaders,
AnastasiaGals/Ann-ReShade, BarbatosBachiko/Reshade-Shaders,
BlueSkyDefender/AstrayFX, BlueSkyDefender/Depth3D, brussell1/Shaders,
CeeJayDK/SweetFX, crosire/reshade-shaders, Daodan317081/reshade-shaders,
EndlesslyFlowering/ReShade_HDR_shaders, Filoppi/PumboAutoHDR, FransBouma/OtisFX,
Fubaxiusz/fubax-shaders, GimleLarpes/potatoFX, IAmTreyM/SHADERDECK,
liuxd17thu/BX-Shade, LordKobra/CobraFX, LordOfLunacy/Insane-Shaders,
luluco250/FXShaders, martymcmodding/iMMERSE, martymcmodding/METEOR,
martymcmodding/qUINT, Matsilagi/RSRetroArch, MaxG2D/ReshadeSimpleHDRShaders,
Mortalitas/GShade-Shaders, nullfrctl/reshade-shaders, originalnicodr/CorgiFX,
outmode/rendepth-reshade, P0NYSLAYSTATION/Scaling-Shaders, papadanku/CShade,
prod80/prod80-ReShade-Repository, PthoEastCoast/Ptho-FX, Radegast-FFXIV/Warp-FX,
retroluxfilm/reshade-vrtoolkit, smolbbsoop/smolbbsoopshaders,
umar-afzaal/LumeniteFX, vortigern11/vort_Shaders, yplebedev/BFBFX,
Zenteon/ZenteonFX

## Why the remaining shaders do not work

**Declares a newer ReShade than this build implements (29, category
`PREPROCESSOR`).** The shader's own `#error` fires and names what it wants. The
FX compiler here is ReShade 4.7.0 (see `src/reshade_fx_version.hpp`), so a shader
requiring 5.1 or 6.0 refuses to load and says so. This is deliberate: reporting a
version we do not implement made these shaders compile against a compiler their
authors had ruled out. Eight of this group are the include-path artifacts
described below rather than version gates.

**Needs atomics on a storage image (category `UNSUPPORTED`).** Atomics on
`groupshared` memory work. Atomics addressing a texel of a storage image do not:
they need a typed storage image with an `R32i`/`R32ui` format, and no shader in
this corpus uses one. The compiler error is translated into the requirement
rather than reported as an unknown identifier, or, as it once was, compiled
against no-op stubs into an image that renders incorrectly with nothing said.

The translation reads the compiler's error, never the shader source. Scanning the
source for these tokens refuses shaders that merely mention one in a comment or
behind their own fallback define; measured against this corpus that cost six
working shaders, which is why it is done the other way round.

**Other language gaps (22, category `PARSE`).** Individual parse errors in the
shaders or their includes, from FX syntax this compiler predates.

**Harness artifacts, not product defects (9).** Eight shaders expect
`ReShade.fxh` one directory above their own, a layout the corpus run does not
reproduce; one is a macro collision (`WRAPMODE`, `SCALE`) that only occurs
because all packs are compiled together with every include path at once. A
normal install hits neither.

## Effect on an existing installation

Compared against a build from before these changes, over the same corpus:

- **454 passed before this work, 443 after the honesty fixes, 449 now.**
- **Compute shaders now compile for real.** Bessel_Bloom, BilateralCS,
  BX_ToyCurveTool, FSR1_2X and LocalContrastCS compile through the real compute
  path: a `GLCompute` entry point with a `LocalSize` execution mode, storage
  images, `tex2Dstore` and `barrier()`. They briefly stopped compiling when the
  no-op stubs were removed, which was the correct intermediate state — a stub
  renders wrong and says nothing.
- **Compute passes are dispatched.** A pass with a `ComputeShader` builds a
  compute pipeline, binds its storage images, and dispatches the group counts
  from `DispatchSizeX/Y/Z`, with layout transitions around the dispatch so a
  later pass reads what it wrote. `groupshared` is real workgroup memory: it was
  lexed as `static` before, which made it a private per-invocation copy that
  `barrier()` guarded nothing about — a wrong image with nothing reported.
- **Atomics on `groupshared` memory work, and half-float conversion is
  componentwise.** `f32tof16`/`f16tof32` were scalar-only, so a vector call
  silently truncated to one component and carried on with a warning. Fixing that
  is what unlocked the whole MartysMods pack, MXAO and SMAA included.
- **The generated SPIR-V is validated in the build.** A smoke shader exercising
  every compute feature is compiled and run through `spirv-val`, and the
  disassembly is checked for the compute entry point, the thread group size, the
  image write, the barrier and the workgroup variable.
- **Four are prod80 shaders** — pCamera, pColorNoise, pColors, pPalettePosterize.
  None uses compute. They fail because `Oklab.fxh` declares
  `ReShade 5.1+ is required`, and that check is now honoured. Previously the
  version was reported as the largest representable integer, so every such gate
  passed regardless of what this build implements.

Nothing else changes for an existing install. Settings move to their own
`settings.conf`, and the previous location is still read, so keybindings and
options carry over. The shader cache is invalidated once, so the first launch
after upgrading recompiles.

## Reproducing this

The corpus is a repeatable gate with a recorded baseline:

```sh
scripts/shader-corpus.sh            # re-run and diff against test/shader-corpus-baseline.txt
scripts/shader-corpus.sh --record   # accept this run as the new baseline
```

It clones the packages listed in `EffectPackages.ini`, so it needs network
access and cannot be a sandboxed flake check. Any shader that changes verdict in
either direction fails the run and is printed.

`tools/test_shaders.cpp` shares its compile environment with the layer through
`src/reshade_fx_env.hpp`, so a result here is a result the layer would reproduce.
