#include "keyboard_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "wayland_interpose.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <unordered_set>

namespace vkBasalt
{
    // Keyboard-specific state (seat/queue come from wayland_input_common)
    static wl_keyboard* wlKeyboard = nullptr;

    // XKB state for key translation
    static xkb_context* xkbCtx = nullptr;
    static xkb_keymap* xkbKeymap = nullptr;
    static xkb_state* xkbState = nullptr;

    // Tracking pressed keys (Wayland keycodes)
    static std::unordered_set<uint32_t> pressedKeys;

    // Accumulated key press events (keysyms) — survives press+release within a single frame
    // so that isKeyPressedWayland can detect rapid key taps even when the key is already
    // released by the time the check runs.
    static std::unordered_set<uint32_t> keyPressEvents;

    // Accumulated state between getKeyboardState calls
    static std::string typedCharsAccumulator;
    static std::string lastKeyNameAccumulator;
    static bool backspacePressed = false;
    static bool deletePressed = false;
    static bool enterPressed = false;
    static bool leftPressed = false;
    static bool rightPressed = false;
    static bool homePressed = false;
    static bool endPressed = false;

    static bool initialized = false;

    // Process a key press event
    static void processWaylandKey(uint32_t keycode, uint32_t state)
    {
        if (!xkbState)
            return;

        // Wayland keycodes are evdev codes, XKB expects evdev + 8
        uint32_t xkbKeycode = keycode + 8;

        if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
        {
            pressedKeys.erase(keycode);
            xkb_state_update_key(xkbState, xkbKeycode, XKB_KEY_UP);
            return;
        }

        // Key press
        pressedKeys.insert(keycode);
        xkb_state_update_key(xkbState, xkbKeycode, XKB_KEY_DOWN);

        xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkbState, xkbKeycode);

        // Track press event for edge detection (survives same-frame release)
        keyPressEvents.insert((uint32_t)keysym);

        // Capture key name for keybind editor (skip modifiers)
        if (keysym != XKB_KEY_Shift_L && keysym != XKB_KEY_Shift_R &&
            keysym != XKB_KEY_Control_L && keysym != XKB_KEY_Control_R &&
            keysym != XKB_KEY_Alt_L && keysym != XKB_KEY_Alt_R &&
            keysym != XKB_KEY_Super_L && keysym != XKB_KEY_Super_R)
        {
            char nameBuf[64];
            if (xkb_keysym_get_name(keysym, nameBuf, sizeof(nameBuf)) > 0)
                lastKeyNameAccumulator = nameBuf;
        }

        // Handle special keys
        if (keysym == XKB_KEY_BackSpace)
            backspacePressed = true;
        else if (keysym == XKB_KEY_Delete)
            deletePressed = true;
        else if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter)
            enterPressed = true;
        else if (keysym == XKB_KEY_Left)
            leftPressed = true;
        else if (keysym == XKB_KEY_Right)
            rightPressed = true;
        else if (keysym == XKB_KEY_Home)
            homePressed = true;
        else if (keysym == XKB_KEY_End)
            endPressed = true;
        else
        {
            // Get UTF-8 text for the key
            char buf[8];
            int len = xkb_state_key_get_utf8(xkbState, xkbKeycode, buf, sizeof(buf));
            if (len > 0 && buf[0] >= 0x20) // Skip control characters
                typedCharsAccumulator += std::string(buf, len);
        }
    }

    // Wayland keyboard listener callbacks
    static void keyboardKeymap(void* /*data*/, wl_keyboard* /*keyboard*/,
                               uint32_t format, int32_t fd, uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
        {
            close(fd);
            return;
        }

        char* mapStr = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapStr == MAP_FAILED)
        {
            close(fd);
            Logger::err("Wayland: failed to mmap keymap");
            return;
        }

        // Clean up old keymap/state
        if (xkbState)
        {
            xkb_state_unref(xkbState);
            xkbState = nullptr;
        }
        if (xkbKeymap)
        {
            xkb_keymap_unref(xkbKeymap);
            xkbKeymap = nullptr;
        }

        xkbKeymap = xkb_keymap_new_from_string(xkbCtx, mapStr,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapStr, size);
        close(fd);

        if (!xkbKeymap)
        {
            Logger::err("Wayland: failed to compile keymap");
            return;
        }

        xkbState = xkb_state_new(xkbKeymap);
        if (!xkbState)
            Logger::err("Wayland: failed to create XKB state");

        Logger::debug("Wayland: keymap loaded");
    }

    static void keyboardEnter(void* /*data*/, wl_keyboard* /*keyboard*/,
                              uint32_t /*serial*/, wl_surface* /*surface*/,
                              wl_array* /*keys*/)
    {
    }

    static void keyboardLeave(void* /*data*/, wl_keyboard* /*keyboard*/,
                              uint32_t /*serial*/, wl_surface* /*surface*/)
    {
        pressedKeys.clear();
        keyPressEvents.clear();  // Prevent stale keysyms surviving focus loss
    }

    static void keyboardKey(void* /*data*/, wl_keyboard* /*keyboard*/,
                            uint32_t /*serial*/, uint32_t /*time*/,
                            uint32_t key, uint32_t state)
    {
        processWaylandKey(key, state);
    }

    static void keyboardModifiers(void* /*data*/, wl_keyboard* /*keyboard*/,
                                  uint32_t /*serial*/, uint32_t modsDepressed,
                                  uint32_t modsLatched, uint32_t modsLocked,
                                  uint32_t group)
    {
        if (xkbState)
            xkb_state_update_mask(xkbState, modsDepressed, modsLatched, modsLocked, 0, 0, group);
    }

    static void keyboardRepeatInfo(void* /*data*/, wl_keyboard* /*keyboard*/,
                                   int32_t /*rate*/, int32_t /*delay*/)
    {
    }

    static const wl_keyboard_listener keyboardListener = {
        .keymap = keyboardKeymap,
        .enter = keyboardEnter,
        .leave = keyboardLeave,
        .key = keyboardKey,
        .modifiers = keyboardModifiers,
        .repeat_info = keyboardRepeatInfo,
    };

    // Called by shared seat listener when keyboard capability is available
    static void bindKeyboard(wl_seat* seat)
    {
        if (wlKeyboard)
            return;

        wlKeyboard = wl_seat_get_keyboard(seat);
        // Register as overlay proxy BEFORE add_listener so the interposition
        // layer passes this through without wrapping
        registerOverlayProxy((wl_proxy*)wlKeyboard);
        wl_keyboard_add_listener(wlKeyboard, &keyboardListener, nullptr);
        Logger::debug("Wayland: keyboard bound from shared seat");
    }

    bool initWaylandKeyboard()
    {
        if (initialized)
            return wlKeyboard != nullptr;

        initialized = true;

        // Create XKB context
        xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!xkbCtx)
        {
            Logger::err("Wayland: failed to create XKB context");
            return false;
        }

        // Register our callback before initializing shared resources
        setKeyboardBindCallback(bindKeyboard);

        // Initialize shared seat/queue/registry (triggers seat capability callbacks)
        if (!initWaylandInputCommon())
            return false;

        if (wlKeyboard)
            Logger::info("Wayland keyboard input initialized");
        else
            Logger::warn("Wayland: no keyboard found on seat");

        return wlKeyboard != nullptr;
    }

    void cleanupWaylandKeyboard()
    {
        if (wlKeyboard)
        {
            unregisterOverlayProxy((wl_proxy*)wlKeyboard);
            wl_keyboard_destroy(wlKeyboard);
            wlKeyboard = nullptr;
        }
        if (xkbState)
        {
            xkb_state_unref(xkbState);
            xkbState = nullptr;
        }
        if (xkbKeymap)
        {
            xkb_keymap_unref(xkbKeymap);
            xkbKeymap = nullptr;
        }
        if (xkbCtx)
        {
            xkb_context_unref(xkbCtx);
            xkbCtx = nullptr;
        }

        pressedKeys.clear();
        keyPressEvents.clear();
        initialized = false;

        // Clean up shared resources (idempotent)
        cleanupWaylandInputCommon();
    }

    uint32_t convertToKeySymWayland(std::string key)
    {
        // XKB keysym names are the same as X11 keysym names
        xkb_keysym_t sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (sym == XKB_KEY_NoSymbol)
        {
            // Try case-insensitive
            sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        }
        if (sym == XKB_KEY_NoSymbol)
            Logger::err("Wayland: invalid key name: " + key);

        return (uint32_t)sym;
    }

    bool isKeyPressedWayland(uint32_t ks)
    {
        if (!initWaylandKeyboard())
            return false;

        dispatchWaylandInputEvents();

        // Check accumulated press events first — catches rapid taps where
        // press+release both arrive in the same dispatch cycle
        if (keyPressEvents.count(ks))
        {
            keyPressEvents.erase(ks);
            return true;
        }

        // Check if any currently-held key maps to this keysym
        if (!xkbKeymap)
            return false;

        for (uint32_t keycode : pressedKeys)
        {
            uint32_t xkbKeycode = keycode + 8;
            xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState, xkbKeycode);
            if ((uint32_t)sym == ks)
                return true;
        }
        return false;
    }

    KeyboardState getKeyboardStateWayland()
    {
        KeyboardState state;

        if (!initWaylandKeyboard())
            return state;

        dispatchWaylandInputEvents();

        state.typedChars = std::move(typedCharsAccumulator);
        state.lastKeyName = std::move(lastKeyNameAccumulator);
        state.backspace = backspacePressed;
        state.del = deletePressed;
        state.enter = enterPressed;
        state.left = leftPressed;
        state.right = rightPressed;
        state.home = homePressed;
        state.end = endPressed;

        // Reset accumulators (moved-from strings are already empty or valid-but-unspecified)
        typedCharsAccumulator.clear();
        lastKeyNameAccumulator.clear();
        backspacePressed = false;
        deletePressed = false;
        enterPressed = false;
        leftPressed = false;
        rightPressed = false;
        homePressed = false;
        endPressed = false;

        return state;
    }
} // namespace vkBasalt
