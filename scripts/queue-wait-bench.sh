#!/usr/bin/env bash
# Measures what a queue drain costs over waiting for the layer's own submission.
#
#   scripts/queue-wait-bench.sh
#
# Runs against lavapipe, so it needs no GPU and gives the same answer on any machine.
# The absolute milliseconds are software-rendering numbers and mean little on their own;
# the ratio is the result, and it is why the reload waits on a fence rather than draining.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Built inside the project: the system temp directory is commonly mounted noexec.
WORK=$(mktemp -d "$ROOT/.cache/queue-wait.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/queue-wait.XXXXXX")
} || exit 1
trap 'rm -rf "$WORK"' EXIT

HEADERS=$(nix build nixpkgs#vulkan-headers --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
MESA=$(nix build nixpkgs#mesa --no-link --print-out-paths 2>/dev/null)
[ -n "$HEADERS" ] && [ -n "$LOADER" ] && [ -n "$MESA" ] || { echo "could not get vulkan headers, loader and mesa"; exit 1; }

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
[ -f "$MESA/lib/libvulkan_lvp.so" ] || { echo "mesa has no lavapipe driver at $MESA/lib/libvulkan_lvp.so"; exit 1; }

nix shell nixpkgs#gcc --command gcc -O2 -I"$HEADERS/include" \
    -o "$WORK/bench" "$ROOT/test/queue-wait/queue_wait_bench.c" \
    -L"$LOADER/lib" -lvulkan || { echo "could not build the benchmark"; exit 1; }

LD_LIBRARY_PATH="$LOADER/lib" VK_DRIVER_FILES="$WORK/lvp.json" "$WORK/bench"
