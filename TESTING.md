# Testing

## Automated, run these

Five surfaces, none of which need a GPU. Four are one command each; the flake
checks run on every commit.

| What | Command | Proves |
| --- | --- | --- |
| Build checks | `nix flake check` | Every invariant the layer relies on, each ablation-verified |
| Shader corpus | `scripts/shader-corpus.sh` | Every shader in ReShade's official index still compiles, and the SPIR-V it emits is valid; diffed against `test/shader-corpus-baseline.txt` |
| Layer order | `scripts/layer-matrix.sh` | The layer survives a frame-generation layer above and below it, on lavapipe with a mock that copies lsfg-vk's present behaviour |
| End to end | `scripts/e2e-smoke.sh` | The built layer loads into a real application, compiles and applies an effect, writes its cache under `$XDG_CACHE_HOME` and its config under `$XDG_CONFIG_HOME`, and creates nothing anywhere else |
| Queue waits | `scripts/queue-wait-bench.sh` | What a queue drain costs against waiting on our own submission |

The corpus needs network access, because it clones the shader packs. The other
three do not.

`scripts/shader-corpus.sh --record` accepts a run as the new baseline. Do that
only when the change in verdicts is the point of the commit.

## Manual: lsfg-order-testing protocol

The section below is the hand protocol for testing against real lsfg-vk on
hardware, which `scripts/layer-matrix.sh` approximates but cannot replace.

### lsfg-order-testing protocol

Goal of this branch: make the efficient layer order work --
`game -> vkBasalt -> LSFG-VK -> (MangoHud)` -- so vkBasalt processes only the
REAL frames (60), and LSFG generates afterwards (120). Today that order black
screens or crashes; only the wasteful reverse order works (vkBasalt processing
all 120 generated frames).

What changed in this branch:

- vkBasalt no longer REPLACES the swapchain usage flags the game (or another
  layer) asked for -- it adds its own on top. Dropped flags are a classic
  cause of black output when another layer needs them.
- vkBasalt no longer holds its internal lock while the layer below it presents.
  LSFG presents several frames and waits for images INSIDE that call; holding
  the lock across it can freeze the whole game (black screen, sound dead, no
  crash) -- exactly one of the two reported symptoms.
- Debug logging that tells us, from one run, which side broke and where.

Everything below is copy-paste. Please run all steps in order.

## 0. Clean up previous builds (once)

List what is installed before deleting anything:

```sh
ls -l ~/.local/share/vulkan/implicit_layer.d/ 2>/dev/null
ls -l /usr/local/share/vulkan/implicit_layer.d/ 2>/dev/null
ls -l /usr/share/vulkan/implicit_layer.d/ | grep -i basalt
ls -l ~/.local/lib/libvkbasalt* ~/.local/lib/vkbasalt/ 2>/dev/null
ls -l /usr/local/lib/libvkbasalt* /usr/local/lib/vkbasalt/ 2>/dev/null
```

Remove every vkBasalt manifest + library YOU installed by hand (old
vkBasalt-overlay and previous builds of this fork). Two copies of the same
layer name make the loader pick one at random -- tests are meaningless until
only ONE is left:

```sh
rm -f ~/.local/share/vulkan/implicit_layer.d/vkBasalt*.json
rm -f ~/.local/lib/libvkbasalt*.so; rm -rf ~/.local/lib/vkbasalt
sudo rm -f /usr/local/share/vulkan/implicit_layer.d/vkBasalt*.json
sudo rm -f /usr/local/lib/libvkbasalt*.so; sudo rm -rf /usr/local/lib/vkbasalt
```

If `/usr/share/vulkan/implicit_layer.d/` has a vkBasalt json, check who owns
it first -- `pacman -Qo /usr/share/vulkan/implicit_layer.d/vkBasalt.json` --
and remove the PACKAGE (`sudo pacman -R vkbasalt`) rather than the file.

## 1. Build dependencies (CachyOS / Arch)

```sh
sudo pacman -S --needed base-devel meson ninja glslang libx11 libxi \
  wayland wayland-protocols libxkbcommon vulkan-headers
```

## 2. Build + install this branch (to your user, no sudo)

```sh
git clone https://github.com/Daaboulex/vkBasalt_overlay_wayland.git
cd vkBasalt_overlay_wayland
git switch lsfg-order-testing
meson setup build --prefix="$HOME/.local" --buildtype=debugoptimized
ninja -C build install
```

Verify exactly one manifest is present and the loader sees the layer:

```sh
ls ~/.local/share/vulkan/implicit_layer.d/
vulkaninfo 2>/dev/null | grep -i -E 'VKBASALT|LSFGVK'
```

## 3. How the layer ORDER is controlled

`VK_INSTANCE_LAYERS` is the lever, and **the FIRST name is closest to the
game**. Measured here against a frame-generation mock (counting vkBasalt's
presents per generated frame: 1 = vkBasalt above it, 2 = below it):

| VK_INSTANCE_LAYERS | who is closer to the game |
|---|---|
| `...VKBASALT...:...LSFGVK...` | vkBasalt (the efficient order: real frames only) |
| `...LSFGVK...:...VKBASALT...` | LSFG (vkBasalt then also processes generated frames) |

Two rules that bite:

- A layer you do NOT name still loads if it is enabled implicitly. So to keep
  a layer out of the run, set its own off-switch: `MANGOHUD=0`,
  `DISABLE_LSFGVK=1`, or leave `ENABLE_VKBASALT` unset.
- Naming a layer in `VK_INSTANCE_LAYERS` enables it EVEN IF its disable
  variable is set -- `DISABLE_LSFGVK=1` plus naming LSFG still loads LSFG.

The ordering works whether or not `ENABLE_VKBASALT=1` is also set (both
verified), so keep `ENABLE_VKBASALT=1` for the runs below.

## 4. Test runs

Use the SAME game and settings you used when you saw the black screen, and
the other game that crashed. Keep your usual LSFG profile (x2). Turn MangoHud
OFF for all runs -- one variable at a time; we add it back after this works.

### Run A -- the order we are fixing (vkBasalt processes real frames)

```sh
export ENABLE_VKBASALT=1
export MANGOHUD=0
export VKBASALT_LOG_LEVEL=debug
export VKBASALT_LOG_FILE=/tmp/vkb-A.log
export VK_LOADER_DEBUG=error,warn,layer
export VK_INSTANCE_LAYERS=VK_LAYER_VKBASALT_OVERLAY_post_processing:VK_LAYER_LSFGVK_frame_generation
<start the game exactly as you normally do> &> /tmp/loader-A.log
```

### Run B -- today's working order (regression check, nothing should break)

Same block, but swap the two names:

```sh
export VKBASALT_LOG_FILE=/tmp/vkb-B.log
export VK_INSTANCE_LAYERS=VK_LAYER_LSFGVK_frame_generation:VK_LAYER_VKBASALT_OVERLAY_post_processing
<game> &> /tmp/loader-B.log
```

Repeat A and B for the second (crashing) game with file names
`/tmp/vkb-A2.log`, `/tmp/loader-A2.log`, `/tmp/vkb-B2.log`,
`/tmp/loader-B2.log`.

For Steam games, run the same exports in a terminal and start the game from
that terminal (`steam steam://rungameid/<id>`), or put the variables in front
of `%command%` in the launch options -- but the terminal way captures the
loader log properly.

## 5. What success looks like

- Run A: game plays, display shows the generated FPS (e.g. 120), and
  `/tmp/vkb-A.log` shows `present cycle N` lines ticking at the REAL fps
  (e.g. 60) -- that is the proof vkBasalt now processes only real frames.
- Run B: behaves exactly like your current setup (nothing regressed), and its
  `present cycle` lines tick at the GENERATED fps (120) -- the waste we are
  removing.

The two cycle rates are also how you (and I) confirm the order actually took
effect: same number in both runs would mean the order did not change.

## 6. Send back (success or failure, always)

1. The 8 files: `/tmp/vkb-A.log`, `/tmp/vkb-B.log`, `/tmp/vkb-A2.log`,
   `/tmp/vkb-B2.log`, `/tmp/loader-A.log`, `/tmp/loader-B.log`,
   `/tmp/loader-A2.log`, `/tmp/loader-B2.log`
2. One line per run: worked / black screen (sound on or off?) / crash
3. `vulkaninfo --summary &> /tmp/vkinfo.txt` and send that too

If a run freezes, wait ~10 seconds, then grab the logs before killing it --
the last lines in `vkb-*.log` are the diagnosis.

## 7. Going back to your previous build

```sh
rm -f ~/.local/share/vulkan/implicit_layer.d/vkBasalt-overlay.json
rm -f ~/.local/lib/libvkbasalt-overlay.so
```

then reinstall whichever build you used before.
