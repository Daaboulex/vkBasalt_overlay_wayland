#!/usr/bin/env bash
set -uo pipefail

# Local layer-order regression matrix on lavapipe: runs vkcube with vkBasalt
# and a mock frame-generation layer that replicates lsfg-vk's structural
# present behavior (consume the caller's semaphores in a submit, acquire an
# extra swapchain image INSIDE the present call, present generated + original).
#
# It proves BOTH orders survive, and it proves WHICH order actually ran: the
# log interleaving gives vkBasalt present-cycles per mock frame --
#   1 -> vkBasalt is ABOVE the framegen layer (the efficient order)
#   2 -> the framegen layer is above vkBasalt (vkBasalt sees generated frames)
#
# Implicit-layer order is NOT controlled by VK_INSTANCE_LAYERS (verified: a
# named-only list still loads every implicit layer). It follows manifest
# DIRECTORY precedence in XDG_DATA_DIRS: the earlier directory's layer ends up
# closer to the game.
#
# Usage: ./run-local-matrix.sh <path-to-vkbasalt-share-dir>
#   e.g.  ./run-local-matrix.sh "$(nix build .#default --no-link --print-out-paths)/share"
#
# Requires: gcc + vulkan headers, vulkan loader, vkcube, a lavapipe ICD.

BASALT_SHARE="${1:?usage: run-local-matrix.sh <vkbasalt-share-dir>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The mock is dlopen'd by the loader, so its directory must NOT be noexec
# (/tmp often is on NixOS -- dlopen then fails "failed to map segment").
mkexecdir() {
    local base d
    for base in "${TMPDIR:-/tmp}" "$HOME/.cache" "$HOME"; do
        [ -d "$base" ] || continue
        d="$(mktemp -d "$base/vkb-matrix.XXXXXX" 2>/dev/null)" || continue
        if printf '#!/bin/sh\n' >"$d/.probe" && chmod +x "$d/.probe" && "$d/.probe" 2>/dev/null; then
            rm -f "$d/.probe"
            printf '%s' "$d"
            return 0
        fi
        rm -rf "$d"
    done
    echo "run-local-matrix: no exec-capable temp dir found" >&2
    return 1
}
WORK="$(mkexecdir)" || exit 1
trap 'rm -rf "$WORK"' EXIT

# shellcheck disable=SC2046
gcc -shared -fPIC -O2 $(pkg-config --cflags vulkan 2>/dev/null || true) \
    -o "$WORK/libmock_framegen.so" "$HERE/mock_framegen.c" || exit 1

mkdir -p "$WORK/mock/vulkan/implicit_layer.d"
cat >"$WORK/mock/vulkan/implicit_layer.d/mock_framegen.json" <<JSON
{
  "file_format_version": "1.1.0",
  "layer": {
    "name": "VK_LAYER_MOCK_framegen",
    "type": "GLOBAL",
    "library_path": "$WORK/libmock_framegen.so",
    "api_version": "1.3.0",
    "implementation_version": "1",
    "description": "structural mock of a frame-generation layer",
    "disable_environment": { "DISABLE_MOCK_FRAMEGEN": "1" }
  }
}
JSON

ICD="${VK_DRIVER_FILES:-/run/opengl-driver/share/vulkan/icd.d/lvp_icd.x86_64.json}"
fail=0

run_case() { # run_case <name> <xdg_data_dirs> <expected_cycles_per_frame>
    local name="$1" layers="$2" want="$3" log="$WORK/$1.log"
    (
        export VK_DRIVER_FILES="$ICD" XDG_DATA_DIRS="$BASALT_SHARE:$WORK/mock:${XDG_DATA_DIRS:-/usr/share}"
        export ENABLE_VKBASALT=1 VK_INSTANCE_LAYERS="$layers"
        # trace: EVERY present is logged, so the counts below are real counts,
        # not a sampled rate (debug logs only a bounded sample).
        export VKBASALT_LOG_LEVEL=trace
        vkcube >/dev/null 2>"$log" &
        local pid=$!
        sleep 8
        kill -9 "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    )

    # Real counts: vkBasalt presents vs mock generated frames. 1 = vkBasalt
    # above the framegen layer (it sees only real frames), 2 = below it.
    local presents frames ratio anomalies hooked
    presents=$({ grep -c 'present cycle' "$log" || true; })
    frames=$({ grep -cE 'mock_framegen: frame [0-9]+ ok' "$log" || true; })
    hooked=$({ grep -c 'mock_framegen: device hooked' "$log" || true; })
    anomalies=$({ grep -cE 'mock_framegen: .*(failed|STALL|TIMEOUT)|down-chain present returned|Vulkan Loader.*ERROR' "$log" || true; })
    if [ "$frames" -gt 0 ]; then
        ratio=$(awk -v p="$presents" -v f="$frames" 'BEGIN { printf "%.0f", p / f }')
    else
        ratio=0
    fi

    if [ "$ratio" = "$want" ] && [ "$anomalies" -eq 0 ] && [ "$hooked" -ge 1 ] && [ "$frames" -gt 20 ]; then
        echo "$name: PASS (presents=$presents generated=$frames ratio=$ratio, no anomalies)"
    else
        echo "$name: FAIL (presents=$presents generated=$frames ratio=$ratio want=$want, hooked=$hooked, anomalies=$anomalies)"
        { grep -E 'mock_framegen: .*(failed|STALL|TIMEOUT)|down-chain present returned|ERROR' "$log" || true; } | head -4 | sed 's/^/    /'
        fail=1
    fi
}

BASALT=VK_LAYER_VKBASALT_OVERLAY_post_processing
MOCK=VK_LAYER_MOCK_framegen

echo "=== A: vkBasalt ABOVE framegen -- the efficient order (vkBasalt sees only real frames) ==="
run_case A "$BASALT:$MOCK" 1

echo "=== B: framegen ABOVE vkBasalt -- today's wasteful order (vkBasalt sees generated frames too) ==="
run_case B "$MOCK:$BASALT" 2

if [ "$fail" -eq 0 ]; then
    echo "MATRIX PASS -- both orders stable; the efficient order runs vkBasalt on real frames only"
else
    echo "MATRIX FAIL"
fi
exit "$fail"
