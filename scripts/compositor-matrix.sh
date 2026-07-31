#!/usr/bin/env bash
# Runs the layer under each presentation environment a game actually meets:
# a plain Wayland surface, a nested gamescope, and X11.
#
#   scripts/compositor-matrix.sh
#
# Needs a live Wayland session, so this is a script rather than a flake check.
# gamescope is how most Proton games are presented, and X11 is what a Wine game
# gets when it takes the Xlib surface, so neither is covered by the plain
# lavapipe smoke test.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

[ -n "${WAYLAND_DISPLAY:-}" ] || { echo "no Wayland session: set WAYLAND_DISPLAY and XDG_RUNTIME_DIR"; exit 1; }

OUT=$(nix build "$ROOT#default" --no-link --print-out-paths 2>/dev/null)
[ -d "$OUT/share/vulkan/implicit_layer.d" ] || { echo "could not build the layer"; exit 1; }

TOOLS=$(nix build nixpkgs#vulkan-tools --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
GAMESCOPE=$(nix build nixpkgs#gamescope --no-link --print-out-paths 2>/dev/null)
XSERVER=$(nix build nixpkgs#xorg.xorgserver --no-link --print-out-paths 2>/dev/null)
[ -n "$TOOLS" ] && [ -n "$LOADER" ] || { echo "could not get vulkan tools and loader"; exit 1; }

WORK=$(mktemp -d "$ROOT/.cache/compositor.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/compositor.XXXXXX")
} || exit 1
trap 'rm -rf "$WORK"' EXIT

FAKE_HOME="$WORK/home"
mkdir -p "$FAKE_HOME/.config/vkBasalt-overlay"
cp "$ROOT/test/language/macro_spacing.fx" "$WORK/macro_spacing.fx"
cat > "$FAKE_HOME/.config/vkBasalt-overlay/vkBasalt.conf" <<EOF
effects = macro_spacing
macro_spacing = $WORK/macro_spacing.fx
reshadeIncludePath = $WORK
enableOnLaunch = True
EOF

fail=0

# Runs one case and reports what the layer managed. $1 names it, the rest is the
# command that must end up presenting frames.
run_case() {
    local name="$1"; shift
    local log="$WORK/$name.log"

    (
        export HOME="$FAKE_HOME"
        export XDG_CONFIG_HOME="$FAKE_HOME/.config"
        export XDG_CACHE_HOME="$FAKE_HOME/.cache"
        export XDG_DATA_HOME="$FAKE_HOME/.local/share"
        export XDG_DATA_DIRS="$OUT/share:/usr/share"
        export LD_LIBRARY_PATH="$LOADER/lib"
        export PATH="$TOOLS/bin:${GAMESCOPE:+$GAMESCOPE/bin:}${XSERVER:+$XSERVER/bin:}$PATH"
        export ENABLE_VKBASALT=1
        export VKBASALT_LOG_LEVEL=debug
        "$@" >/dev/null 2>"$log" &
        local pid=$!
        sleep 12
        kill -9 "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    )

    local presented="no" errors="none"
    grep -qE 'present cycle|created fake swapchain images' "$log" && presented="yes"
    if grep -qE 'vkBasalt err:|VUID-' "$log"; then
        errors=$(grep -cE 'vkBasalt err:|VUID-' "$log")
    fi

    if [ "$presented" = "yes" ] && [ "$errors" = "none" ]; then
        echo "  ok    $name"
    else
        echo "  FAIL  $name (presented=$presented, errors=$errors)"
        grep -E 'vkBasalt err:|VUID-' "$log" | head -3 | sed 's/^/          /'
        fail=1
    fi
}

echo "=== the layer under each way a game reaches the screen ==="

run_case wayland vkcube --c 400

if [ -n "$GAMESCOPE" ]; then
    run_case gamescope "$GAMESCOPE/bin/gamescope" -W 1280 -H 720 -- vkcube --c 400
else
    echo "  skip  gamescope (not available)"
fi

if [ -n "$XSERVER" ]; then
    "$XSERVER/bin/Xvfb" :77 -screen 0 1280x720x24 >/dev/null 2>&1 &
    xvfb_pid=$!
    sleep 2
    DISPLAY=:77 run_case x11-xlib vkcube --c 400 --wsi xlib
    kill -9 "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
else
    echo "  skip  x11 (no X server available)"
fi

echo
[ "$fail" -eq 0 ] && echo "COMPOSITOR MATRIX PASS" || echo "COMPOSITOR MATRIX FAIL"
exit "$fail"
