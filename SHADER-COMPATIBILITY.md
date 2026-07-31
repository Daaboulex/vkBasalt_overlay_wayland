# ReShade shader compatibility

Measured, not estimated. Every `.fx` in the shader packs listed below was compiled
through this layer's own compile environment and the result recorded.

**Result as of 2026-07-30: 461 of 504 shaders compile to valid SPIR-V (91%).**

Every module this compiler emits is now valid: none compiles to SPIR-V a driver
would reject. 43 do not compile. Compiling is not the same as
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

<!-- BEGIN GENERATED SUPPORT TABLE -->

Generated by `scripts/shader-support-table.sh` from `test/shader-corpus-baseline.txt`.
Do not edit by hand.

| state | count | what it means |
| --- | --- | --- |
| Supported | 471 | Compiles, and every module it emits passes `spirv-val`. |
| Supported, untested | 30 | Same, but the compiler raised a warning worth checking in a game. |
| Unsupported | 3 | Does not compile, for the reasons listed below. |

Compiling to valid SPIR-V is verified for every shader here by the corpus.
Rendering correctly is not: that needs the shader run against a real game,
and a shader reading depth needs the depth buffer wired to show anything.

### Supported, untested (30)

ATMOSPHERE,AdaptiveTint BX_XChannelCurve,CRTCX CRTHyllian,CRTcgwg ColorIsolation2,CoolRetroTerminal Deconverge,DrawOverlay FILMDECK,JPEG LeiFX_OA,NTSC_XOT Phosphor,Pong RadiantGI,RemoveTint SnowScape,Stats cDots,cLetterBox cSpace,cTransform dh_uber_rt,mdapt scanlines-abs,vt220 vt220_frame,vt220_night

### Unsupported (3)

- `BX_XIV_ChromakeyPlus` needs the REST addon in Final Fantasy XIV under DirectX 11. A Vulkan layer cannot provide it.
- `BaBa_SSR` calls `tex2Dgather`, which ReShade 6.7.3 does not define, so it does not compile there either.
- `SuperDepth3D_WoWvx` branches on a vector. ReShade rejects that with X3019; only DXC accepts it, by taking the first component.

### Supported (471)

3DFX,3DToElse 4xBRZ,AO ASCII,AdaptiveColorGrading AdaptiveFog,AdaptiveTonemapper AdvancedAutoHDR,Anaglyph_to_SBS_or_TAB ArcaneBloom,AreaCopy AreaDiscard,ArtifactColors ArtisticVignette,AspectRatio AspectRatioComposition,AspectRatioMultiGrid AspectRatioSuite,BAYER BETA_ZenRCAO,BX_ToyCurveTool BaBa_DTLAA,BaBa_Deband BaBa_FakeHDR,BaBa_Flow BaBa_Flow_Lite,BaBa_GI BaBa_MiAO,BaBa_NeoSSAO BaBa_Outline,BaBa_PHDR BaBa_SSR_Lite,BaBa_Sharpen_NIS BaBa_Sharpen_Neural,BaBa_Sharpen_Residual BaBa_VividTone,BaBa_XeGTAO BasicCRT,BeforeAfter Bessel_Bloom,BilateralCS BilateralComic,BloomingHDR Border,BulgePinch Bumpmapping,CA CAS,CEOG CMAA_2,CMYK CRT,CRT-Frutbunn CRT-NewPixie,CRT-Yee64 CRT-Yeetron,CRTAperture CRTCaligari,CRTEasymode CRTFakeLottes,CRTGeom CRTGeomMOD,CRTKurg CRTLottes,CRTLottes2 CRTPi,CRTPotatoCool CRTPotatoWarm,CRTRefresh CRTSim,CRT_Lottes CRT_Yee64,CRT_Yeetron CanvasFog,CanvasMask Cartoon,Cathode CathodeRayTube,Censor Checkerboard,ChromaSubSampling Chromakey,ChromaticAberration Chromaticity,ChromaticitySimplified CinematicDOF,Clarity Clarity2,CobraMask ColShift,ColorInversion ColorIsolation,ColorLab ColorMAME,ColorMask ColorMatrix,ColorMod ColorSort_CS,ColorToAlpha ColorfulPoster,Comic Compare,Composition Composition,ContrastSharpening ContrastStretch,ConvertColorSpace CrossProcess,CubeLUT1D CubeLUT3D,Cursor CurvedMonitor,Curves DH_UI_TextDetect,DINN DINN,DINN DLAA_Plus,DOSGame DPX,Daltonize Deband,Deblur DeepFry,Defocus Dehaze,DepthAlpha DepthDarkness,DepthHaze DepthSharpen,DepthSharpenStaticDof Depth_Cues,Depth_Tool Dimension_Plus,DirectionalDepthBlur DisplayDepth,DisplayMod Dither,DownsampleSSAA DropShadow,Droste Drunk,EGAfilter Emboss,Emphasize ExtendedLevels,EyeAdaption FGFXEnergyConservativeFilmGrain,FGFXFastCascadedSeparableBlur16X FGFXLargeScalePerceptualObscuranceIrradiance,FSR1_2X FXAA,FakeHDR FastSharpen,FilmGrain FilmicAnamorphSharpen,FilmicSharpen Fisheye,Fisheye Flair,Flashlight FlexibleCA,Flip Flipbook,FocalDOF FramerateLimiter,FreezeShot Frequency_CS,GAUSSIAN GI,GTU GaussianBloom,GaussianBlurCS GlobalAlpha,GloomAO GoldenRatio,GrainSpread Gravity,Gravity_CS Greyscale,GuestCRT HDRBloom,HDRMotionBlur HDRSaturation,Halation Halftone,Heightfog HexLensFlare,HotsamplingHelper HueFX,Image ImageAdjustment,ImageSharpening Interlaced,Interlacing KeepUI,LUT LUTTools,Layer LensDistort,Letterbox LevelIO,Levels LiftGammaGain,LightPersistance Limbo_Mod,LiquidLens LocalContrast,LocalContrastCS LumaLines,LumaSharpen MAMEPostProc,MBMB MCAmber,MCGreen MCOrange,MMJCelShader MagicBorder,MagicHDR MagnifyingGlass,MartysMods_CHROMATICABERRATION MartysMods_FILMGRAIN,MartysMods_FLOYDSTEINBERG MartysMods_HALFTONE,MartysMods_LAUNCHPAD MartysMods_LOCALLAPLACIAN,MartysMods_LONGEXPOSURE MartysMods_MXAO,MartysMods_NVSHARPEN MartysMods_SCENEWEAVER,MartysMods_SHARPEN MartysMods_SMAA,MartysMods_SOLARIS MartysMods_TODDYHANCER,MaskGlowAdvanced MattiasCRT,MeshEdges MetaCRT,MinimalColorGrading MonitorGamma,Monochrome MotionFocus,Mouse MultiFX,MultiLUT MultiTonePoster,N64_3Point NFAA,NTSCCustom NTSC_RetroArch,NTSC_RetroArch_NoScanlines NeoBloom,NormalMap Nostalgia,Oilify OrtonBloom,Overlay PAL,PD80_01A_RT_Correct_Contrast PD80_01B_RT_Correct_Color,PD80_01_Color_Gamut PD80_02_Bloom,PD80_02_Bonus_LUT_pack PD80_02_Cinetools_LUT,PD80_02_LUT_Creator PD80_03_Color_Space_Curves,PD80_03_Curved_Levels PD80_03_Filmic_Adaptation,PD80_03_Levels PD80_03_Shadows_Midtones_Highlights,PD80_04_BlacknWhite PD80_04_Color_Balance,PD80_04_Color_Gradients PD80_04_Color_Isolation,PD80_04_Color_Temperature PD80_04_Contrast_Brightness_Saturation,PD80_04_Magical_Rectangle PD80_04_Saturation_Limit,PD80_04_Selective_Color PD80_04_Selective_Color_v2,PD80_04_Technicolor PD80_05_Sharpening,PD80_06_Chromatic_Aberration PD80_06_Depth_Slicer,PD80_06_Film_Grain PD80_06_Luma_Fade,PD80_06_Posterize_Pixelate PPFX_Bloom,PPFX_Godrays PPFX_SSDO,PandaFX PatternShading,PerfectPerspective PiecewiseFilmicTonemap,Pirate_Bloom Pirate_Depth_DOF,Pirate_Depth_GI Pirate_FXAA,PixelShifter Polynomial_Barrel_Distortion_for_HMDs,PowerVR2 Prism,R57_PAL R57_PAL_NEW,RGBLCD RadialSlitScan,ReVeil RealLongExposure,Reinhard Rendepth,Resizer RetroCRT,RetroFog RetroTV,RetroTint Ripple,SCurve SMAA,Sepia SharpContrast,SimpleBloom SimpleGrain,Sketch SkySave,SlitScan SmartDeNoise,SmartNoise Smart_Sharp,SplicedRadials Splitscreen,Spotlight StageDepth,StageDepthPlus StageDepthPlus_WithDepthBufferMod,SunsetFilter SuperDepth3D,SurfaceBlur Swirl,TVCRTPixels Technicolor,Technicolor Technicolor2,Template Template,Temporal_AA ThreeColorGradient,TiltShift TinyPlanet,Tonemap TrackingRays,Trails TripleMonitor,UIDetect UIMask,UIMaskCreator UnrealLens,Unsharp VHS,VHSPro VHS_RA,VRS_Map VRToolkit,Vectorscope VerticalPreviewer,Vibrance Vignette,VirtualNose VirtualResolution,WatchDogs Wave,Waveform WhitepointFixer,WinUaeMaskGlow ZebraLines,ZenWork Zenteon_ATA,Zenteon_FilmGrain Zenteon_Framework,Zenteon_LocalContrast Zenteon_Motion,Zenteon_PaletteMap Zenteon_SSAO_History,Zenteon_Sharpen Zenteon_TurboGI,Zenteon_XenonBloom ZigZag,anamorpho blur,cAutoExposure cBloom,cBlurH cBlurV,cCAS cCheckerBoard,cDLAA cEnsor,cFXAA cFlow,cGhost cLayer,cLens cLipse,cMotionBlur cMotionStabilization,cNoiseBlur cNormalize,cQuantize cRCAS,cSolidColor cThreshold,cmyk_halftone_dot crt-nes-mini,crt-royale dh_Lain,dh_ahoh dh_ambient_remove,dh_anime dh_canvas,dh_pastel_bug dh_sync,dh_uber_mask dh_uber_motion,dh_undither dh_uniformity_correction,hyperblur kContour,kDatamosh kMirror,lcd-grid lilium__blue_noise_dithering,lilium__cas_hdr lilium__filmgrain,lilium__hdr_and_sdr_analysis lilium__hdr_black_floor_fix,lilium__hdr_brightness_adjustment lilium__inverse_tone_mapping,lilium__map_sdr_into_hdr lilium__rcas_hdr,lilium__sdr_trc_fix lilium__test_pattern_generator,lilium__tone_mapping lumenite_AnamorphicBloom,lumenite_Kernel lumenite_LSAO,lumenite_QuantAO lumenite_QuantMotion,lumenite_RTAO lumenite_SSSR,lumenite_TRAA nGlide_3DFX,ntsc pCamera,pColorNoise pColors,pPalettePosterize qUINT_bloom,qUINT_deband qUINT_dof,qUINT_lightroom qUINT_mxao,qUINT_sharp qUINT_ssr,scanlines-fract sgenpt-mix,shades_TFAA smolbbsoop_HDR_Converter,smolbbsoop_RadialBlur vort_Motion,vort_Static zfast_crt

<!-- END GENERATED SUPPORT TABLE -->
