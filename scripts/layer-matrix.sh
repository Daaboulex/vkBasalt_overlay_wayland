#!/usr/bin/env bash
# Runs the layer-order regression matrix: vkcube under this layer plus a mock
# frame-generation layer, in both orders, on lavapipe.
#
#   scripts/layer-matrix.sh
#
# Needs no GPU and no lsfg-vk. The mock replicates lsfg-vk's structural present
# behaviour, so this exercises the swapchain, present, fence and image-pool paths
# against a layer that acquires an extra image inside present and consumes the
# caller's semaphores.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SHARE=$(nix build "$ROOT#default" --no-link --print-out-paths 2>/dev/null)/share
[ -d "$SHARE/vulkan/implicit_layer.d" ] || { echo "could not build the layer"; exit 1; }

HEADERS=$(nix build nixpkgs#vulkan-headers --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
TOOLS=$(nix build nixpkgs#vulkan-tools --no-link --print-out-paths 2>/dev/null)
MESA=$(nix build nixpkgs#mesa --no-link --print-out-paths 2>/dev/null)
[ -n "$HEADERS" ] && [ -n "$LOADER" ] && [ -n "$TOOLS" ] && [ -n "$MESA" ] \
    || { echo "could not get vulkan headers, loader, tools and mesa"; exit 1; }
[ -f "$MESA/lib/libvulkan_lvp.so" ] || { echo "mesa has no lavapipe driver"; exit 1; }

ICD_DIR=$(mktemp -d "$ROOT/.cache/layer-matrix.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && ICD_DIR=$(mktemp -d "$ROOT/.cache/layer-matrix.XXXXXX")
} || exit 1
trap 'rm -rf "$ICD_DIR"' EXIT

cat > "$ICD_DIR/lvp.json" <<EOF
{
    "ICD": {
        "api_version": "1.4.354",
        "library_arch": "64",
        "library_path": "$MESA/lib/libvulkan_lvp.so"
    },
    "file_format_version": "1.0.1"
}
EOF

nix shell nixpkgs#gcc --command env \
    PATH="$TOOLS/bin:$PATH" \
    CPATH="$HEADERS/include" \
    LIBRARY_PATH="$LOADER/lib" \
    LD_LIBRARY_PATH="$LOADER/lib" \
    VK_DRIVER_FILES="$ICD_DIR/lvp.json" \
    "$ROOT/test/mock-framegen/run-local-matrix.sh" "$SHARE"
