# vkBasalt Overlay (Wayland Fork)

<!-- BEGIN generated:badges -->
[![CI](https://github.com/Daaboulex/vkBasalt_overlay_wayland/actions/workflows/ci.yml/badge.svg)](https://github.com/Daaboulex/vkBasalt_overlay_wayland/actions/workflows/ci.yml)
[![NixOS unstable](https://img.shields.io/badge/NixOS-unstable-78C0E8?logo=nixos&logoColor=white)](https://nixos.org)
[![License: Zlib](https://img.shields.io/badge/License-Zlib-blue.svg)](./LICENSE)
<!-- END generated:badges -->

> **Fork Notice.** This is a fork of [vkBasalt](https://github.com/DadSchoorse/vkBasalt) by [@DadSchoorse](https://github.com/DadSchoorse), via the overlay fork by [@Boux](https://github.com/Boux/vkBasalt_overlay). Most of this fork was written with vibe-coding (AI assistance). The original vkBasalt is a mature, well-tested project; this fork adds experimental features on top.
>
> **Use at your own risk** — it may crash or freeze games. Adding GPU-intensive shaders (e.g., CRT-Guest) to a game already at 100% GPU usage will freeze your system.

A Vulkan post-processing layer with an in-game ImGui overlay for real-time effect configuration. Works on both **X11** and **Wayland**.

Feature showcase (slightly outdated): <https://www.youtube.com/watch?v=_KJTToAynr0>

<details>
  <summary>Click to view screenshots</summary>
  <img width="1920" height="1080" alt="Screenshot_20251231_184224" src="https://github.com/user-attachments/assets/06f05dfd-b429-4f1d-bb5d-b9d49a1719b1" />
  <img width="1920" height="1080" alt="Screenshot_20251231_183856" src="https://github.com/user-attachments/assets/3ba85dc9-d3de-4795-bd3a-6bbc2028e0dd" />
  <img width="1920" height="1080" alt="Screenshot_20251231_183700" src="https://github.com/user-attachments/assets/195e44df-1cd6-47bd-b543-5ee431b53483" />
</details>

## Features

Upstream vkBasalt requires editing config files and restarting. This fork adds:

- **In-game overlay** (`End` key) with dockable/undockable tab windows
- **Add/remove/reorder effects** without restart (drag to reorder)
- **Parameter sliders** for all types (float, int, uint, bool, vectors)
- **Preprocessor definitions** editor for ReShade `#define` values
- **Multiple effect instances** (e.g., cas, cas.1, cas.2)
- **Per-game profiles** with auto-detection and profile switching
- **Save/load named configs**
- **Shader manager** — browse directories, discover and load ReShade shaders
- **Diagnostics** — FPS, frame time, GPU/VRAM usage (AMD, Intel, NVIDIA)
- **Debug window** — effect state, log viewer, error display
- **Auto-apply** — changes apply after configurable delay
- **Up to 200 effects** with VRAM estimates
- **Safe Anti-Cheat mode** — per-profile toggle that blocks depth-using shaders and disables depth capture, keeping safe shaders like Vibrance usable
- **Shader test tool** — batch-tests all `.fx` shaders for compilation errors and depth usage
- **Graceful error handling** — failed effects show errors instead of crashing

### This Wayland Fork Adds

- **Wayland input blocking** — `wl_proxy_add_listener` interposition wraps game's pointer/keyboard listeners to suppress events when the overlay has focus
- **X11 input blocking** — `XGrabPointer`/`XGrabKeyboard` when overlay is active
- **Reliable Wayland mouse input** — time-based auto-release handles missing button releases from compositor grabs; motion-aware idle detection keeps buttons held during drags at any framerate
- **Game pointer mirroring** — interpose layer mirrors button state from the game's pointer to the overlay, ensuring reliable press/release tracking via Wayland implicit grab
- **Right-click context menus** on parameter sliders to reset to defaults
- **Depth buffer ready flag** — `bufready_depth` uniform now correctly reports whether depth is available to shaders

### Input Architecture

| Platform | Keyboard | Mouse | Input Blocking |
|----------|----------|-------|----------------|
| **X11** | `XQueryKeymap` via separate `Display*` | `XQueryPointer` | `XGrabPointer`/`XGrabKeyboard` |
| **Wayland** | `wl_keyboard` on private event queue + xkbcommon | `wl_pointer` on private event queue | `wl_proxy_add_listener` interposition |

On Wayland, the overlay creates its own `wl_pointer` and `wl_keyboard` on a separate `wl_event_queue`. When input blocking is enabled, the interposition layer wraps the game's pointer/keyboard listeners and suppresses events (except `leave`, `keymap`, `modifiers`, and `repeat_info` which are always forwarded to maintain correct game state).

### GPU Diagnostics

The diagnostics tab auto-detects your GPU vendor via PCI vendor ID and reads stats accordingly:

| Vendor | GPU Usage | VRAM | How |
|--------|-----------|------|-----|
| **AMD** | Direct (`gpu_busy_percent`) | `mem_info_vram_*` + GTT | sysfs (`amdgpu` driver) |
| **Intel** | Frequency ratio estimate | If available (Arc discrete) | sysfs (`i915`/`xe` driver) |
| **NVIDIA** | Direct (NVML) | Direct (NVML) | Runtime `dlopen("libnvidia-ml.so.1")` |

- **AMD**: Full support — GPU utilization, dedicated VRAM, GTT (shared memory for iGPUs)
- **Intel**: GPU frequency ratio (`gt_act_freq_mhz / gt_max_freq_mhz`) as a utilization estimate. VRAM is available on discrete Arc GPUs if the driver exposes it.
- **NVIDIA**: Uses NVML (NVIDIA Management Library) loaded at runtime via `dlopen`. No build-time dependency — if `libnvidia-ml.so.1` is not present, GPU stats are simply unavailable. Install `nvidia-utils` (or equivalent) for NVML support.
- **Unknown/unsupported**: FPS and frame time are always shown. GPU-specific stats gracefully degrade to "not available".

### Depth Buffer

The layer intercepts `vkCreateImage` to detect depth images and adds `VK_IMAGE_USAGE_SAMPLED_BIT` so shaders can sample them. ReShade effects with `semantic = "DEPTH"` textures receive the actual depth buffer, and the `bufready_depth` uniform correctly reports availability.

### Safe Anti-Cheat Mode

Per-profile toggle (`safeAntiCheat = true`) that:

- Forces `depthCapture = off` — no depth buffer binding
- Blocks shaders that use depth at runtime (hidden in Add Effects, shows tooltip explaining why)
- Auto-tests all shaders on first Add Effects open (one per frame, progress bar shown)

It does **not** hide the layer. The Vulkan loader answers an application's
pre-instance layer query from the installed manifests, so vkBasalt stays visible
however this is set. What the setting removes is depth access, so no effect can
see through geometry.

**Depth detection** uses SPIR-V call graph analysis: builds a per-function call graph from the compiled shader bytecode, then BFS from entry points to check if any reachable function loads the depth sampler. This distinguishes shaders that merely *include* depth declarations (via `ReShade.fxh`) from those that actually *use* depth at runtime.

### ReShade Shader Support

The ReShade FX compiler is vendored from upstream 6.7.3, so the shader language is
current. Of the 504 shaders in ReShade's official package index, **501 compile and
every module they emit passes `spirv-val`**; the three that do not, and why, are in
[SHADER-COMPATIBILITY.md](SHADER-COMPATIBILITY.md) alongside the full list.

That is a measurement of compiling, not of rendering. A shader that compiles can
still look wrong, and one that reads depth shows nothing until the depth buffer is
wired, so the same file marks which shaders have been run and which have only been
compiled.

One effect costs between 6 and 13 microseconds a frame on an RX 9070 XT across
repeated runs of `scripts/perf-bench.sh`. The spread is the benchmark's, not the
layer's: at this scale a single figure would be quoted more precisely than it can
be measured. Effects compute at full precision by default even where a shader
asks for `half`, which avoids the flicker half precision causes in anything that
accumulates; effects that declare reduced-precision math show a per-effect
"Half precision" toggle in the overlay for opting into 16-bit where the shader
tolerates it. Heavy shader packs cost far more than the layer around them.

Download shader packs and point the Shader Manager at them:

- <https://github.com/crosire/reshade-shaders>
- <https://github.com/HelelSingh/CRT-Guest-ReShade>
- <https://github.com/kevinlekiller/reshade-steam-proton>

Place shaders in `~/.config/vkBasalt-overlay/reshade/Shaders/` and textures in `~/.config/vkBasalt-overlay/reshade/Textures/`, or use the Shader Manager's browse feature to add directories.

<!-- BEGIN generated:upstream -->
## Upstream

| | |
|---|---|
| **Project** | Fork of [DadSchoorse/vkBasalt](https://github.com/DadSchoorse/vkBasalt) via [Boux/vkBasalt_overlay](https://github.com/Boux/vkBasalt_overlay) |
| **License** | zlib |
| **Tracked** | Not tracked -- both upstreams are dormant; this fork is the continuation |

<!-- END generated:upstream -->

<!-- BEGIN generated:installation -->
## Installation

Add as a flake input:

```nix
{
  inputs.vkBasalt_overlay_wayland = {
    url = "github:Daaboulex/vkBasalt_overlay_wayland";
    inputs.nixpkgs.follows = "nixpkgs";
  };
}
```

Then add the overlay:

```nix
nixpkgs.overlays = [ inputs.vkBasalt_overlay_wayland.overlays.default ];
```

<!-- END generated:installation -->

### Arch and CachyOS

A PKGBUILD is in `packaging/arch/`:

```sh
cd packaging/arch && makepkg -si
```

It conflicts with `vkbasalt` and `vkbasalt-overlay-git` on purpose. All of them
read `ENABLE_VKBASALT`, so two installed at once means two active in the same
process: effects apply twice and disabling one disables neither. The layer warns
when it finds another in its process, but not installing both is better.

### 32-bit games

The layer only applies to games whose architecture it was built for. For 32-bit
games (many older titles under Proton/Wine without WoW64), add the 32-bit build
alongside the 64-bit one:

```nix
hardware.graphics = {
  enable = true;
  enable32Bit = true;
  extraPackages = [ pkgs.vkbasalt-overlay ];
  extraPackages32 = [ inputs.vkBasalt_overlay_wayland.packages.x86_64-linux.vkbasalt-overlay-i686 ];
};
```

The layer manifests carry `library_arch`, so the Vulkan loader picks the right
build per process.

## Usage

### Test

```bash
ENABLE_VKBASALT=1 vkcube
# or
ENABLE_VKBASALT=1 vkgears
```

### gamescope

gamescope removes `ENABLE_VKBASALT` from the environment it gives the program it
launches, so the layer loads into gamescope itself and never into the game. Set
it again inside:

```sh
gamescope -W 2560 -H 1440 -- env ENABLE_VKBASALT=1 %command%
```

Setting it outside gamescope applies the layer to gamescope's own output instead,
which is occasionally what you want and usually not.

### Steam

Add to launch options:

```text
ENABLE_VKBASALT=1 %command%
```

Example with Proton optimizations and GameMode:

```text
ENABLE_VKBASALT=1 PROTON_ENABLE_WAYLAND=1 PROTON_USE_NTSYNC=1 DXVK_ASYNC=1 PROTON_FSR4_UPGRADE=1 gamemoderun %command%
```

### Lutris

1. Right-click game -> Configure
2. System options -> Environment variables
3. Add `ENABLE_VKBASALT` = `1`

### Debug Logging

```bash
ENABLE_VKBASALT=1 VKBASALT_LOG_LEVEL=debug ./game
```

Log levels: `trace`, `debug`, `info`, `warn`, `error`, `none`

To log to a file: `VKBASALT_LOG_FILE=/tmp/vkbasalt.log`

### Why can't this fork coexist with original vkBasalt?

This fork **cannot** be installed alongside the original vkBasalt because both must use the same `ENABLE_VKBASALT` environment variable. Gamescope and other Vulkan compositors [filter known layer environment variables](https://github.com/Boux/vkBasalt_overlay/issues/5#issuecomment-3706694598) to prevent layers from loading twice (on both the compositor and nested apps). Using a different env var name would break this filtering, causing the overlay and all active effects to render twice when using gamescope.

The library and layer names are still different to avoid file conflicts:

- Library: `libvkbasalt-overlay.so` (vs `libvkbasalt.so`)
- Layer: `VK_LAYER_VKBASALT_OVERLAY_post_processing` (vs `VK_LAYER_VKBASALT_post_processing`)
- Layer JSON: `vkBasalt-overlay.json` (vs `vkBasalt.json`)

In theory, you could change the env var in `/usr/share/vulkan/implicit_layer.d/vkBasalt-overlay.json`, but only do that if you never use gamescope.

## Configuration

Configuration is stored in `~/.config/vkBasalt-overlay/`. All required config files and subfolders are generated on first run.

Two files, with one writer each. `vkBasalt.conf` holds effects, shader paths and
tuning, and is yours to edit — the overlay never rewrites it. `settings.conf`
holds the overlay's own settings (key bindings, effect slots, auto-apply) and is
written by the overlay when you change them there. Upgrading from a build that
kept settings in `vkBasalt.conf` needs nothing: that location is still read, so
existing key bindings carry over.

### Key Bindings

| Key | Default | Description |
|-----|---------|-------------|
| Toggle Effects | `Home` | Enable/disable all effects |
| Reload Config | `F10` | Reload configuration and recompile shaders |
| Toggle Overlay | `End` | Show/hide the overlay GUI |

### Settings File

`~/.config/vkBasalt-overlay/vkBasalt.conf`:

```ini
# Upper bound on how many effects may be active (1-200)
# Video memory is allocated for the effects actually in use and grows as you add
# more, so raising this reserves nothing on its own
maxEffects = 10

# Key bindings
toggleKey = Home
reloadKey = F10
overlayKey = End

# Startup behavior
enableOnLaunch = true
depthCapture = false

# Overlay options
overlayBlockInput = false
autoApplyDelay = 200  # ms delay before auto-applying changes

# Optional diagnostics (off means no query pool or timestamp commands)
effectGpuTiming = false

# Optional: keep ReShade UI parameters in per-image uniform buffers
liveReshadeUniforms = false
```

### Per-Game Profiles

When a game is detected, vkBasalt creates a config directory at `~/.config/vkBasalt-overlay/configs/<game>/`. Multiple named profiles can be created per game from the overlay UI. The active profile is stored in `~/.config/vkBasalt-overlay/configs/<game>/active_profile`.

### Shader Manager

ReShade shader and texture paths are managed through the Shader Manager tab in the overlay. Add parent directories and the manager will recursively discover `Shaders/` and `Textures/` subdirectories. Paths are stored in `~/.config/vkBasalt-overlay/shader_manager.conf`.

## Anti-Cheat Safety

vkBasalt is a **read-only visual filter** — it applies post-processing shaders to the final rendered image, similar to NVIDIA Freestyle, AMD Adrenalin filters, or monitor-level color adjustments. It does **not**:

- Modify game memory or game files
- Read game state (player positions, health, etc.)
- Inject code into the game process
- Provide any competitive advantage (no wallhacks, aimbots, ESP)
- Intercept or modify network traffic

**Anti-cheat compatibility varies by game and platform:**

- **On Linux/Proton**, EAC and BattlEye have limited kernel-level access compared to Windows. Vulkan implicit layers like vkBasalt and MangoHud generally work because the anti-cheat cannot deeply inspect the Vulkan layer chain. No confirmed bans from vkBasalt have been reported.
- **On Windows**, some games actively block ReShade and even NVIDIA Freestyle (e.g., Arc Raiders blocks both). vkBasalt falls in the same category as ReShade from an anti-cheat perspective — it is a third-party rendering layer, not a whitelisted vendor feature.
- **Per-game policies**: Anti-cheat detection is configured per-game by the developer. A game that allows vkBasalt today could block it tomorrow. The original vkBasalt FAQ says: *"Will vkBasalt get me banned? Maybe. To my knowledge this hasn't happened yet but don't blame me if your frog dies."*

**No guarantee can be made** — use at your own discretion. vkBasalt only applies visual post-processing and provides zero competitive advantage, but anti-cheat systems don't always distinguish between cosmetic and malicious modifications.

## Known Limitations

- Only a shader's first technique runs; a shader defining several will not behave as its author intended
- Compute shaders run: `ComputeShader` passes are dispatched with their declared thread group and dispatch sizes, including storage images (`tex2Dstore`), `barrier()` and `groupshared` memory. Texture atomics are still refused with an explanation rather than compiled into something that renders incorrectly
- The FX compiler is vendored from ReShade 6.7.3; `__RESHADE__` reports the highest level whose runtime contract this layer keeps (see `src/reshade_fx_version.hpp`), so a shader demanding more refuses to load and names the version it wants
- Depth buffer access works but is experimental (depends on game's depth format)
- Input blocking may cause issues in some games with custom input handling; on Wayland under wine it needs the `LD_AUDIT` shim that `vkbasalt-run` sets, and the layer says so when it cannot block
- Intel GPU usage is estimated from frequency ratio (not direct utilization)
- NVIDIA stats require `libnvidia-ml.so.1` (install `nvidia-utils`)

[SHADER-COMPATIBILITY.md](SHADER-COMPATIBILITY.md) records how many shaders from
ReShade's official package index compile, and why each of the rest does not.

## Architecture

### Vulkan Layer

vkBasalt is a Vulkan implicit layer that intercepts API calls:

- `vkCreateSwapchainKHR` — creates intermediate images for effect processing
- `vkQueuePresentKHR` — applies effects before presentation
- `vkCreateImage` — detects depth images and adds `SAMPLED_BIT`
- `vkGetSwapchainImagesKHR` — returns wrapped images

Effects read from one image and write to another in a chain.

### Effect System

Built-in effects: CAS (sharpening), RCAS (FSR 1's sharpener — solves for the
maximum sharpness before clipping and limits sharpening of detected noise, where
CAS maps local contrast to sharpness more simply), DLS (denoised luma sharpening),
FXAA, SMAA, Deband, LUT.

Upscaling is deliberately absent. A layer at this position receives a frame the
game has already drawn at full size, so there is nothing smaller to upscale from
and no performance to recover; and AMD specifies FSR's upscaling pass to run
before UI compositing and with a negative mip bias, neither of which is available
here. For a genuine render-scale use gamescope's `-F fsr`, or Proton's fullscreen
FSR, both of which sit above the game's resolution choice rather than below it.

Reporting a smaller surface size to make the game draw less does not work either,
and not for the reason you might expect: DXVK would honour it, but the game's
backbuffer is sized above Vulkan and DXVK simply scales that full-size frame down
into the smaller swapchain. The game still draws every pixel, and the picture is
resampled twice.

FSR 1 itself is available as a community ReShade shader (`FSR1_2X` and similar),
which this fork compiles. Those are quality filters that supersample and come
back down, so they cost more GPU time rather than less.

ReShade FX effects are compiled at runtime using an embedded ReShade shader compiler. Parameters are managed through the `EffectRegistry` (single source of truth for all runtime parameter values).

### Wayland Interposition

Since `wl_pointer_add_listener` is a `static inline` function in `<wayland-client-protocol.h>`, we cannot interpose it directly. Instead, we interpose on the underlying C function `wl_proxy_add_listener` using symbol visibility and `dlsym(RTLD_NEXT)`. The interposed function:

1. Checks `wl_proxy_get_class()` to identify `wl_pointer` and `wl_keyboard` proxies
2. Skips overlay-owned proxies (registered via `registerOverlayProxy()`)
3. Wraps the game's listener with callbacks that check `isInputBlocked()` before forwarding

## Development

```bash
nix develop                  # dev shell with pre-commit hooks
nix flake check --no-build   # eval check (fast)
nix build                    # build package (runs the meson tests too)
nix fmt                      # format with treefmt
```

Building by hand on any distro:

```bash
meson setup build
ninja -C build
meson test -C build --print-errorlogs
```

`meson setup` ends with a feature summary naming the wayland version built
against and which pointer events this build handles. The tests need no GPU and
no network; [TESTING.md](TESTING.md) lists the deeper surfaces.

## Credits

- Original **vkBasalt** by [@DadSchoorse](https://github.com/DadSchoorse) (Georg Lehmann) — [zlib license](LICENSE)
- **vkBasalt Overlay** by [@Boux](https://github.com/Boux/vkBasalt_overlay) — ImGui overlay, effect management, shader manager
- **Wayland fork** by [@Daaboulex](https://github.com/Daaboulex/vkBasalt_overlay_wayland) — Wayland input, input blocking interposition, depth fix
- **ReShade** shader compiler by [@crosire](https://github.com/crosire)
- **ImGui** by [@ocornut](https://github.com/ocornut)

<!-- BEGIN generated:footer -->
<!-- END generated:footer -->
