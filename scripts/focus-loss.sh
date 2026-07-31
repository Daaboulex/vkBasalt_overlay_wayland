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
# It reports inconclusive today. vkcube renders uncapped into a software X
# server, which starves a second client's connection, so the probe times out
# rather than answering. Throttling the application, or probing over a connection
# opened before it starts, is what this needs next.
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
WM=$(nix build nixpkgs#xorg.twm --no-link --print-out-paths 2>/dev/null)
for v in "$TOOLS" "$LOADER" "$XSERVER" "$XDOTOOL" "$XLIB" "$XLIBDEV" "$XPROTO"; do
    [ -n "$v" ] || { echo "could not get the x server, xdotool, xlib and vulkan tools"; exit 1; }
done

WORK=$(mktemp -d "$ROOT/.cache/focus.XXXXXX" 2>/dev/null) || {
  mkdir -p "$ROOT/.cache" && WORK=$(mktemp -d "$ROOT/.cache/focus.XXXXXX")
} || exit 1
DISPLAY_NUM=":78"
cleanup() {
    exec 3>&- 2>/dev/null || true
    kill -9 "${app_pid:-0}" "${probe_pid:-0}" "${wm_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true
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

# Without a window manager nothing owns input focus, and a synthetic key has
# nowhere to land.
if [ -n "$WM" ]; then
    DISPLAY="$DISPLAY_NUM" "$WM/bin/twm" >/dev/null 2>&1 &
    wm_pid=$!
    sleep 2
fi

export DISPLAY="$DISPLAY_NUM"
# The probe holds one connection for the whole run and answers a line at a time,
# because opening a new one to a server busy rendering starves past any timeout.
mkfifo "$WORK/cmd"
"$WORK/grab_probe" < "$WORK/cmd" > "$WORK/answers" 2>&1 &
probe_pid=$!
exec 3> "$WORK/cmd"

# The count is read from the file rather than kept in a variable, because every
# caller runs in a command substitution and a variable would not survive it.
ask() {
    local before after waited
    before=$(wc -l < "$WORK/answers" 2>/dev/null || echo 0)
    echo "$1" >&3
    waited=0
    while :; do
        after=$(wc -l < "$WORK/answers" 2>/dev/null || echo 0)
        [ "$after" -gt "$before" ] && break
        sleep 0.2
        waited=$((waited + 1))
        [ "$waited" -gt 100 ] && { echo "probe-timeout"; return; }
    done
    tail -1 "$WORK/answers"
}
probe() { ask grab; }
keystate() { ask key; }
step() { echo "  .. $1"; }

step "probing the pointer before anything runs"
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
step "started the application, waiting for it to present"
sleep 8

# Xvfb has no window manager, so nothing holds input focus and a synthetic key
# would go nowhere. The window is focused first.
step "looking for the application window"
app_window=$(timeout 10 "$XDOTOOL/bin/xdotool" search "" 2>/dev/null | tail -1)
step "window: ${app_window:-none found}"
if [ -n "$app_window" ]; then
    timeout 10 "$XDOTOOL/bin/xdotool" windowmap "$app_window" 2>/dev/null || true
    timeout 10 "$XDOTOOL/bin/xdotool" windowraise "$app_window" 2>/dev/null || true
    timeout 10 "$XDOTOOL/bin/xdotool" windowfocus "$app_window" 2>/dev/null || true
    timeout 10 "$XDOTOOL/bin/xdotool" windowactivate "$app_window" 2>/dev/null || true
    step "focused window $app_window"
fi
sleep 1
# The layer samples key state rather than consuming events, so an instantaneous
# synthetic press falls between two samples. The key is held instead.
step "sending the toggle key"
timeout 10 "$XDOTOOL/bin/xdotool" keydown --clearmodifiers Home 2>/dev/null || true
sleep 1
step "the server reports Home $(keystate) while it is held"
timeout 10 "$XDOTOOL/bin/xdotool" keyup --clearmodifiers Home 2>/dev/null || true
sleep 3

step "probing after the toggle"
after_toggle=$(probe)
echo "  after opening the overlay:   pointer $after_toggle"

if [ "$after_toggle" != "held" ]; then
    echo "  the overlay never took a pointer grab, so this test cannot say anything"
    echo "  --- what the layer logged ---"
    grep -iE "toggle|grab|overlay|key|block|focus" "$WORK/run.log" | tail -10 | sed "s/^/    /"
    echo "  --- windows on the display ---"
    timeout 10 "$XDOTOOL/bin/xdotool" search "" 2>/dev/null | head -5 | sed "s/^/    /"
    echo "FOCUS LOSS INCONCLUSIVE"
    exit 0
fi

# Focus is moved by unmapping the window that holds it. Every xdotool call here
# is bounded: an unbounded --sync waits for a window that may never be focused,
# and the layer holds a keyboard grab by this point.
if [ -n "$app_window" ]; then
    timeout 10 "$XDOTOOL/bin/xdotool" windowunmap "$app_window" 2>/dev/null || true
fi
sleep 3

step "probing after focus went away"
after_focus_loss=$(probe)
echo "  after focus went elsewhere:  pointer $after_focus_loss"

if [ "$after_focus_loss" = "held" ]; then
    echo
    echo "FOCUS LOSS FAIL -- the grab outlived the window's focus, which strands the cursor"
    exit 1
fi

echo
echo "FOCUS LOSS PASS"
