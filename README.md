## Fork Notice

This is a fork of [vkBasalt](https://github.com/DadSchoorse/vkBasalt) by [@DadSchoorse](https://github.com/DadSchoorse), via the overlay fork by [@Boux](https://github.com/Boux/vkBasalt_overlay). Most of this fork was written with vibe-coding (AI assistance). The original vkBasalt is a mature, well-tested project; this fork adds experimental features on top.

**Use at your own risk** — it may crash or freeze games. Adding GPU-intensive shaders (e.g., CRT-Guest) to a game already at 100% GPU usage will freeze your system.

---

# vkBasalt Overlay (Wayland Fork)

A Vulkan post-processing layer with an in-game ImGui overlay for real-time effect configuration. Works on both **X11** and **Wayland**.

Feature showcase (slightly outdated): https://www.youtube.com/watch?v=_KJTToAynr0

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
- **Diagnostics** — FPS, frame time, GPU/VRAM usage (AMD GPUs via sysfs)
- **Debug window** — effect state, log viewer, error display
- **Auto-apply** — changes apply after configurable delay
- **Up to 200 effects** with VRAM estimates
- **Graceful error handling** — failed effects show errors instead of crashing

### This Wayland Fork Adds

- **Wayland input blocking** — `wl_proxy_add_listener` interposition wraps game's pointer/keyboard listeners to suppress events when the overlay has focus
- **X11 input blocking** — `XGrabPointer`/`XGrabKeyboard` when overlay is active
- **Sticky button fix** — tracks button state across pointer leave/enter to handle lost releases during compositor grabs (window move/resize)
- **Right-click context menus** on parameter sliders to reset to defaults
- **Depth buffer ready flag** — `bufready_depth` uniform now correctly reports whether depth is available to shaders

### Input Architecture

| Platform | Keyboard | Mouse | Input Blocking |
|----------|----------|-------|----------------|
| **X11** | `XQueryKeymap` via separate `Display*` | `XQueryPointer` | `XGrabPointer`/`XGrabKeyboard` |
| **Wayland** | `wl_keyboard` on private event queue + xkbcommon | `wl_pointer` on private event queue | `wl_proxy_add_listener` interposition |

On Wayland, the overlay creates its own `wl_pointer` and `wl_keyboard` on a separate `wl_event_queue`. When input blocking is enabled, the interposition layer wraps the game's pointer/keyboard listeners and suppresses events (except `leave`, `keymap`, `modifiers`, and `repeat_info` which are always forwarded to maintain correct game state).

### GPU Diagnostics

GPU stats are read from Linux sysfs and currently support **AMD GPUs only** (`amdgpu` kernel module). The diagnostics tab shows:
- GPU busy percentage (`gpu_busy_percent`)
- VRAM usage (`mem_info_vram_used` / `mem_info_vram_total`)
- GTT (shared memory) usage for iGPUs

Intel and NVIDIA GPUs show "GPU stats not available" — these would require vendor-specific APIs (Intel Perf, NVML) that are not yet implemented.

### Depth Buffer

The layer intercepts `vkCreateImage` to detect depth images and adds `VK_IMAGE_USAGE_SAMPLED_BIT` so shaders can sample them. ReShade effects with `semantic = "DEPTH"` textures receive the actual depth buffer, and the `bufready_depth` uniform correctly reports availability.

### ReShade Shader Support

Download shader packs and point the Shader Manager at them:
- https://github.com/crosire/reshade-shaders
- https://github.com/HelelSingh/CRT-Guest-ReShade
- https://github.com/kevinlekiller/reshade-steam-proton

Place shaders in `~/.config/vkBasalt-overlay/reshade/Shaders/` and textures in `~/.config/vkBasalt-overlay/reshade/Textures/`, or use the Shader Manager's browse feature to add directories.

## Installation

**Warning:** You must uninstall the original vkBasalt before installing this fork. Both use the same `ENABLE_VKBASALT` environment variable and cannot coexist (see [why](#why-cant-this-fork-coexist-with-original-vkbasalt)).

### Dependencies

- GCC >= 9
- Meson + Ninja
- Vulkan Headers
- SPIR-V Headers
- glslang (glslangValidator)
- X11 + Xi development files
- wayland-client + wayland-protocols + wayland-scanner
- libxkbcommon

### AUR

```
yay -S vkbasalt-overlay-git
```

### From Source

```bash
git clone https://github.com/Daaboulex/vkBasalt_overlay_wayland.git
cd vkBasalt_overlay_wayland
meson setup --buildtype=release --prefix=/usr build-release
ninja -C build-release
sudo ninja -C build-release install
```

### NixOS

This project can be built with a nix-shell for development:

```bash
nix-shell -p meson ninja pkg-config gcc wayland wayland-protocols wayland-scanner \
  libxkbcommon glslang spirv-headers vulkan-headers vulkan-loader xorg.libX11 xorg.libXi \
  --run "meson setup --buildtype=debug builddir && ninja -C builddir"
```

## Usage

### Test

```bash
ENABLE_VKBASALT=1 vkcube
# or
ENABLE_VKBASALT=1 vkgears
```

### Steam

Add to launch options:
```
ENABLE_VKBASALT=1 %command%
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

### Key Bindings

| Key | Default | Description |
|-----|---------|-------------|
| Toggle Effects | `Home` | Enable/disable all effects |
| Reload Config | `F10` | Reload configuration and recompile shaders |
| Toggle Overlay | `End` | Show/hide the overlay GUI |

### Settings File

`~/.config/vkBasalt-overlay/vkBasalt.conf`:

```ini
# Maximum effects (requires restart, 1-200)
# Higher values use more VRAM (~8 bytes x width x height per slot)
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
```

### Per-Game Profiles

When a game is detected, vkBasalt creates a config directory at `~/.config/vkBasalt-overlay/configs/<game>/`. Multiple named profiles can be created per game from the overlay UI. The active profile is stored in `~/.config/vkBasalt-overlay/configs/<game>/active_profile`.

### Shader Manager

ReShade shader and texture paths are managed through the Shader Manager tab in the overlay. Add parent directories and the manager will recursively discover `Shaders/` and `Textures/` subdirectories. Paths are stored in `~/.config/vkBasalt-overlay/shader_manager.conf`.

## Known Limitations

- Some ReShade shaders with multiple techniques may not work
- Depth buffer access works but is experimental (depends on game's depth format)
- Input blocking may cause issues in some games with custom input handling
- GPU diagnostics are AMD-only (Intel/NVIDIA GPUs not supported)
- ReShade mouse/key uniforms (`mousepoint`, `mousedelta`, `mousebutton`, `key`) are not yet implemented

## Architecture

### Vulkan Layer

vkBasalt is a Vulkan implicit layer that intercepts API calls:
- `vkCreateSwapchainKHR` — creates intermediate images for effect processing
- `vkQueuePresentKHR` — applies effects before presentation
- `vkCreateImage` — detects depth images and adds `SAMPLED_BIT`
- `vkGetSwapchainImagesKHR` — returns wrapped images

Effects read from one image and write to another in a chain.

### Effect System

Built-in effects: CAS (sharpening), DLS (denoised luma sharpening), FXAA, SMAA, Deband, LUT.

ReShade FX effects are compiled at runtime using an embedded ReShade shader compiler. Parameters are managed through the `EffectRegistry` (single source of truth for all runtime parameter values).

### Wayland Interposition

Since `wl_pointer_add_listener` is a `static inline` function in `<wayland-client-protocol.h>`, we cannot interpose it directly. Instead, we interpose on the underlying C function `wl_proxy_add_listener` using symbol visibility and `dlsym(RTLD_NEXT)`. The interposed function:

1. Checks `wl_proxy_get_class()` to identify `wl_pointer` and `wl_keyboard` proxies
2. Skips overlay-owned proxies (registered via `registerOverlayProxy()`)
3. Wraps the game's listener with callbacks that check `isInputBlocked()` before forwarding

## Credits

- Original **vkBasalt** by [@DadSchoorse](https://github.com/DadSchoorse) (Georg Lehmann) — [zlib license](LICENSE)
- **vkBasalt Overlay** by [@Boux](https://github.com/Boux/vkBasalt_overlay) — ImGui overlay, effect management, shader manager
- **Wayland fork** by [@Daaboulex](https://github.com/Daaboulex/vkBasalt_overlay_wayland) — Wayland input, input blocking interposition, depth fix
- **ReShade** shader compiler by [@crosire](https://github.com/crosire)
- **ImGui** by [@ocornut](https://github.com/ocornut)
