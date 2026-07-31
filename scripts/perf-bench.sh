#!/usr/bin/env bash
# Measures what the layer costs per frame, by rendering a fixed number of frames
# with it and without it.
#
#   scripts/perf-bench.sh [frames]
#
# Set VKBASALT_PERF_ICD to run against a real driver instead of the bundled
# lavapipe. On lavapipe the absolute numbers are software rendering and mean
# little on their own; the ratio between the two runs is the result.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRAMES="${1:-600}"

OUT=$(nix build "$ROOT#default" --no-link --print-out-paths 2>/dev/null)
[ -d "$OUT/share/vulkan/implicit_layer.d" ] || { echo "could not build the layer"; exit 1; }

TOOLS=$(nix build nixpkgs#vulkan-tools --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
MESA=$(nix build nixpkgs#mesa --no-link --print-out-paths 2>/dev/null)
[ -n "$TOOLS" ] && [ -n "$LOADER" ] && [ -n "$MESA" ] || { echo "could not get vulkan tools, loader and mesa"; exit 1; }

WORK=$(mktemp -d "$ROOT/.cache/perf.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/perf.XXXXXX")
} || exit 1
trap 'rm -rf "$WORK"' EXIT

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

FAKE_HOME="$WORK/home"
mkdir -p "$FAKE_HOME/.config/vkBasalt-overlay"
cp "$ROOT/test/language/macro_spacing.fx" "$WORK/macro_spacing.fx"
cat > "$FAKE_HOME/.config/vkBasalt-overlay/vkBasalt.conf" <<EOF
effects = macro_spacing
macro_spacing = $WORK/macro_spacing.fx
reshadeIncludePath = $WORK
enableOnLaunch = True
EOF

# One run of the same workload. $1 is 1 to load the layer, 0 to leave it out.
run() {
    local enable="$1" start end
    start=$(date +%s%N)
    (
        export HOME="$FAKE_HOME"
        export XDG_CONFIG_HOME="$FAKE_HOME/.config"
        export XDG_CACHE_HOME="$FAKE_HOME/.cache"
        export XDG_DATA_HOME="$FAKE_HOME/.local/share"
        export XDG_DATA_DIRS="$OUT/share:/usr/share"
        export VK_DRIVER_FILES="${VKBASALT_PERF_ICD:-$WORK/lvp.json}"
        export LD_LIBRARY_PATH="$LOADER/lib"
        export PATH="$TOOLS/bin:$PATH"
        export ENABLE_VKBASALT="$enable"
        vkcube --c "$FRAMES" --present_mode 0 >/dev/null 2>&1
    )
    end=$(date +%s%N)
    echo $(( (end - start) / 1000000 ))
}

echo "rendering $FRAMES frames per run, uncapped"

# The first run pays for shader compilation and warms the driver, so it is
# discarded rather than reported.
run 1 > /dev/null

without=$(run 0)
with=$(run 1)

[ "$without" -gt 0 ] || { echo "the baseline run took no measurable time"; exit 1; }

per_without=$(awk -v t="$without" -v f="$FRAMES" 'BEGIN { printf "%.3f", t / f }')
per_with=$(awk -v t="$with" -v f="$FRAMES" 'BEGIN { printf "%.3f", t / f }')
overhead=$(awk -v a="$without" -v b="$with" 'BEGIN { printf "%.1f", (b - a) * 100.0 / a }')
per_frame=$(awk -v a="$without" -v b="$with" -v f="$FRAMES" 'BEGIN { printf "%.3f", (b - a) / f }')

printf '\n  without the layer  %6s ms total  %8s ms/frame\n' "$without" "$per_without"
printf '  with one effect    %6s ms total  %8s ms/frame\n' "$with" "$per_with"
printf '\n  the layer costs    %8s ms/frame, %s%% more than not running it\n' "$per_frame" "$overhead"
