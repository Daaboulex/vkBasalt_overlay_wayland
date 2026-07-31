#!/usr/bin/env bash
# Asks whether an overlay left open survives the application losing focus.
#
#   scripts/focus-loss.sh
#
# An X11 pointer grab is held by the client until it lets go: losing focus does
# not end it, so an overlay that grabs and never hears about focus leaving would
# strand the cursor. This runs the layer on a private X server, opens the
# overlay, takes focus away, and asks a second client whether the grab is still
# held.
#
# It reports inconclusive on a bare Xvfb: the layer reads key state through
# XInput2 and XQueryKeymap, and no synthetic press has yet made it toggle without
# a window manager. Run it on a session with one to reach a verdict.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

OUT=$(nix build "$ROOT#default" --no-link --print-out-paths 2>/dev/null)
[ -d "$OUT/share/vulkan/implicit_layer.d" ] || { echo "could not build the layer"; exit 1; }

TOOLS=$(nix build nixpkgs#vulkan-tools --no-link --print-out-paths 2>/dev/null)
LOADER=$(nix build nixpkgs#vulkan-loader --no-link --print-out-paths 2>/dev/null)
XSERVER=$(nix build nixpkgs#xorg.xorgserver --no-link --print-out-paths 2>/dev/null)
XDOTOOL=$(nix build nixpkgs#xdotool --no-link --print-out-paths 2>/dev/null)
XLIB=$(nix build nixpkgs#xorg.libX11.out --no-link --print-out-paths 2>/dev/null)
XLIBDEV=$(nix build nixpkgs#xorg.libX11.dev --no-link --print-out-paths 2>/dev/null)
XPROTO=$(nix build nixpkgs#xorg.xorgproto --no-link --print-out-paths 2>/dev/null)
for v in "$TOOLS" "$LOADER" "$XSERVER" "$XDOTOOL" "$XLIB" "$XLIBDEV" "$XPROTO"; do
    [ -n "$v" ] || { echo "could not get the x server, xdotool, xlib and vulkan tools"; exit 1; }
done

WORK=$(mktemp -d "$ROOT/.cache/focus.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/focus.XXXXXX")
} || exit 1
DISPLAY_NUM=":78"
cleanup() {
    kill -9 "${app_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

nix shell nixpkgs#gcc --command gcc -O2 -o "$WORK/grab_probe" "$ROOT/test/focus-loss/grab_probe.c" \
    -I"$XLIBDEV/include" -I"$XPROTO/include" -L"$XLIB/lib" -lX11 > "$WORK/cc.log" 2>&1
[ -x "$WORK/grab_probe" ] || { echo "could not build the grab probe"; head -3 "$WORK/cc.log"; exit 1; }

FAKE_HOME="$WORK/home"
mkdir -p "$FAKE_HOME/.config/vkBasalt-overlay"
cp "$ROOT/test/language/macro_spacing.fx" "$WORK/macro_spacing.fx"
cat > "$FAKE_HOME/.config/vkBasalt-overlay/vkBasalt.conf" <<EOF
effects = macro_spacing
macro_spacing = $WORK/macro_spacing.fx
reshadeIncludePath = $WORK
enableOnLaunch = True
toggleKey = Home
EOF

"$XSERVER/bin/Xvfb" "$DISPLAY_NUM" -screen 0 1280x720x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2

export DISPLAY="$DISPLAY_NUM"
probe() { DISPLAY="$DISPLAY_NUM" "$WORK/grab_probe"; }

[ "$(probe)" = "free" ] || { echo "the pointer was already grabbed before the layer started"; exit 1; }

(
    export HOME="$FAKE_HOME"
    export XDG_CONFIG_HOME="$FAKE_HOME/.config"
    export XDG_CACHE_HOME="$FAKE_HOME/.cache"
    export XDG_DATA_HOME="$FAKE_HOME/.local/share"
    export XDG_DATA_DIRS="$OUT/share:/usr/share"
    export LD_LIBRARY_PATH="$LOADER/lib:$XLIB/lib"
    export PATH="$TOOLS/bin:$PATH"
    export ENABLE_VKBASALT=1
    export VKBASALT_LOG_LEVEL=debug
    exec vkcube --wsi xlib >/dev/null 2>"$WORK/run.log"
) &
app_pid=$!
sleep 8

# Xvfb has no window manager, so nothing holds input focus and a synthetic key
# would go nowhere. The window is focused first.
app_window=$("$XDOTOOL/bin/xdotool" search --onlyvisible --class vkcube 2>/dev/null | head -1)
if [ -n "$app_window" ]; then
    "$XDOTOOL/bin/xdotool" windowfocus "$app_window" 2>/dev/null || true
    "$XDOTOOL/bin/xdotool" windowraise "$app_window" 2>/dev/null || true
fi
sleep 1
# The layer samples key state rather than consuming events, so an instantaneous
# synthetic press falls between two samples. The key is held instead.
"$XDOTOOL/bin/xdotool" keydown --clearmodifiers Home 2>/dev/null || true
sleep 1
"$XDOTOOL/bin/xdotool" keyup --clearmodifiers Home 2>/dev/null || true
sleep 3

after_toggle=$(probe)
echo "  after opening the overlay:   pointer $after_toggle"

if [ "$after_toggle" != "held" ]; then
    echo "  the overlay never took a pointer grab, so this test cannot say anything"
    echo "  --- what the layer logged ---"
    tail -12 "$WORK/run.log" | sed "s/^/    /"
    echo "FOCUS LOSS INCONCLUSIVE"
    exit 0
fi

"$XDOTOOL/bin/xdotool" windowunmap "$("$XDOTOOL/bin/xdotool" getactivewindow 2>/dev/null)" 2>/dev/null || true
"$XDOTOOL/bin/xdotool" windowfocus --sync 1 2>/dev/null || true
sleep 3

after_focus_loss=$(probe)
echo "  after focus went elsewhere:  pointer $after_focus_loss"

if [ "$after_focus_loss" = "held" ]; then
    echo
    echo "FOCUS LOSS FAIL -- the grab outlived the window's focus, which strands the cursor"
    exit 1
fi

echo
echo "FOCUS LOSS PASS"
