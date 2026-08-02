#!/usr/bin/env bash
# End-to-end: build the layer, load it into a real Vulkan application on lavapipe,
# compile and apply a ReShade effect, and audit every file it creates.
#
#   scripts/e2e-smoke.sh
#
# Set VKBASALT_E2E_ICD to run against a driver other than the bundled lavapipe.
#
# Runs against an isolated HOME, so anything written outside the XDG directories
# is pollution and is reported as a failure rather than being missed.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

OUT=$(nix build "$ROOT#default" --no-link --print-out-paths 2>/dev/null)
[ -d "$OUT/share/vulkan/implicit_layer.d" ] || { echo "could not build the layer"; exit 1; }

TOOLS=$(nix build nixpkgs#vulkan-tools --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
MESA=$(nix build nixpkgs#mesa --no-link --print-out-paths 2>/dev/null)
[ -n "$TOOLS" ] && [ -n "$LOADER" ] && [ -n "$MESA" ] || { echo "could not get vulkan tools, loader and mesa"; exit 1; }

# The system temp directory is commonly noexec, and the loader dlopens from here.
WORK=$(mktemp -d "$ROOT/.cache/e2e.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/e2e.XXXXXX")
} || exit 1
trap 'rm -rf "$WORK"' EXIT

FAKE_HOME="$WORK/home"
mkdir -p "$FAKE_HOME"

cat > "$WORK/lvp.json" <<EOF
{
    "ICD": {
        "api_version": "1.4.354",
        "library_arch": "64",
        "library_path": "$MESA/lib/libvulkan_lvp.so"
    },
    "file_format_version": "1.0.1"
}
EOF

mkdir -p "$FAKE_HOME/.config/vkBasalt-overlay"
cp "$ROOT/test/language/macro_spacing.fx" "$WORK/macro_spacing.fx"
cp "$ROOT/test/lut/cube_lut.fx" "$WORK/cube_lut.fx"
cp "$ROOT/test/compute_smoke.fx" "$WORK/compute_smoke.fx"
mkdir -p "$FAKE_HOME/.config/vkBasalt-overlay/reshade/Textures"
cp "$ROOT/test/lut/identity.cube" "$FAKE_HOME/.config/vkBasalt-overlay/reshade/Textures/identity.cube"
cat > "$FAKE_HOME/.config/vkBasalt-overlay/vkBasalt.conf" <<EOF
effects = macro_spacing:cube_lut:compute_smoke
macro_spacing = $WORK/macro_spacing.fx
cube_lut = $WORK/cube_lut.fx
compute_smoke = $WORK/compute_smoke.fx
reshadeIncludePath = $WORK
enableOnLaunch = True
EOF

run_vkcube() { # run_vkcube <logfile>
    (
        export HOME="$FAKE_HOME"
        export XDG_CONFIG_HOME="$FAKE_HOME/.config"
        export XDG_CACHE_HOME="$FAKE_HOME/.cache"
        export XDG_DATA_HOME="$FAKE_HOME/.local/share"
        export XDG_DATA_DIRS="$OUT/share:/usr/share"
        export VK_DRIVER_FILES="${VKBASALT_E2E_ICD:-$WORK/lvp.json}"
        export LD_LIBRARY_PATH="$LOADER/lib"
        export PATH="$TOOLS/bin:$PATH"
        export ENABLE_VKBASALT=1
        export VKBASALT_LOG_LEVEL=debug
        vkcube >/dev/null 2>"$1" &
        pid=$!
        sleep 10
        kill -9 "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    )
}

LOG="$WORK/run.log"
run_vkcube "$LOG"

fail=0
check() { # check <description> <pattern>
    if grep -qE "$2" "$LOG"; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1"
        fail=1
    fi
}

echo "=== the layer loaded, compiled the effect and presented ==="
check "layer initialised"          'vkBasalt|SettingsManager'
check "effect compiled"            'macro_spacing|created reshade shaderModule'
check "swapchain set up"           'created fake swapchain images'
check "cube LUT effect compiled"   'cube_lut'
check "compute effect compiled"    'compute_smoke'
check "frames presented"           'present cycle'
if grep -qE 'vkBasalt err:|Vulkan Loader.*ERROR|VUID-' "$LOG"; then
    echo "  FAIL  the run reported errors"
    grep -E 'vkBasalt err:|Vulkan Loader.*ERROR|VUID-' "$LOG" | head -5 | sed 's/^/        /'
    fail=1
else
    echo "  ok    the run reported no errors"
fi

echo
echo "=== a second launch runs the same chain from the disk cache ==="
LOG2="$WORK/run2.log"
run_vkcube "$LOG2"
check2() { # check2 <description> <pattern>
    if grep -qE "$2" "$LOG2"; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1"
        fail=1
    fi
}
check2 "warm run hit the shader cache"  'shader cache hit'
check2 "warm run presented"             'present cycle'
if grep -qE 'vkBasalt err:|Vulkan Loader.*ERROR|VUID-' "$LOG2"; then
    echo "  FAIL  the warm run reported errors"
    grep -E 'vkBasalt err:|Vulkan Loader.*ERROR|VUID-' "$LOG2" | head -5 | sed 's/^/        /'
    fail=1
else
    echo "  ok    the warm run reported no errors"
fi

echo
echo "=== the shader cache was generated in the XDG cache ==="
if [ -d "$FAKE_HOME/.cache/vkBasalt-overlay" ] && [ -n "$(ls -A "$FAKE_HOME/.cache/vkBasalt-overlay" 2>/dev/null)" ]; then
    echo "  ok    $(find "$FAKE_HOME/.cache/vkBasalt-overlay" -type f | wc -l) file(s) under \$XDG_CACHE_HOME/vkBasalt-overlay"
else
    echo "  FAIL  no shader cache was written"
    fail=1
fi

echo
echo "=== every file the layer created, and where ==="
find "$FAKE_HOME" -type f 2>/dev/null | sed "s|$FAKE_HOME|~|" | sort | sed 's/^/  /'

echo
echo "=== nothing outside the XDG directories ==="
stray=$(find "$FAKE_HOME" -mindepth 1 -maxdepth 1 ! -name '.config' ! -name '.cache' ! -name '.local' 2>/dev/null)
if [ -z "$stray" ]; then
    echo "  ok    \$HOME holds only .config, .cache and .local"
else
    echo "  FAIL  files outside the XDG directories:"
    printf '%s\n' "$stray" | sed 's/^/        /'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "=== last 25 lines the layer logged (cold run) ==="
    tail -25 "$LOG" | sed 's/^/  /'
    echo
    echo "=== last 25 lines the layer logged (warm run) ==="
    tail -25 "$LOG2" 2>/dev/null | sed 's/^/  /'
fi

echo
[ "$fail" -eq 0 ] && echo "E2E PASS" || echo "E2E FAIL"
exit "$fail"
