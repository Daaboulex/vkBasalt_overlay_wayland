# ReShade shader compatibility

Measured, not estimated. Every `.fx` in the shader packs listed below was compiled
through this layer's own compile environment and the result recorded.

**Result as of 2026-07-30: 443 of 503 shaders compile (88%).**

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

## Why the 60 remaining shaders do not compile

**Declares a newer ReShade than this build implements (29, category
`PREPROCESSOR`).** The shader's own `#error` fires and names what it wants. The
FX compiler here is ReShade 4.7.0 (see `src/reshade_fx_version.hpp`), so a shader
requiring 5.1 or 6.0 refuses to load and says so. This is deliberate: reporting a
version we do not implement made these shaders compile against a compiler their
authors had ruled out. Eight of this group are the include-path artifacts
described below rather than version gates.

**Needs compute shaders (8, category `UNSUPPORTED`).** Storage-image writes
(`tex2Dstore`) and compute barriers. This layer's pipeline is fragment-only, so
these cannot run. The compiler error is translated into the requirement — *needs
ReShade 4.8 or newer (uses storage-image writes); this build implements 4.7* —
rather than reported as an unknown identifier, or, as it was previously, compiled
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

- **454 passed before, 443 pass now.** Eleven shaders changed verdict, and one
  (`RealLongExposure`) began passing.
- **Seven of the eleven are compute shaders** — Bessel_Bloom, BilateralCS,
  BX_ToyCurveTool, CMAA_2, FSR1_2X, LocalContrastCS, ReVeil. They previously
  compiled only because atomics, storage writes and barriers were stubbed out to
  no-op expressions. They would have rendered incorrectly with nothing reported.
  They now refuse to load and explain why. An eighth, MartysMods_MXAO, was
  already failing and now reports the same clear reason.
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

The corpus is not yet a build check. To re-measure:

```sh
g++ -std=c++20 -O1 -Isrc -Isrc/reshade -I<spirv-headers>/include \
    tools/test_shaders.cpp src/reshade/*.cpp -o test_shaders
./test_shaders <every directory containing .fx or .fxh>
```

`tools/test_shaders.cpp` shares its compile environment with the layer through
`src/reshade_fx_env.hpp`, so a result here is a result the layer would reproduce.
