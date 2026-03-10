#include "input_blocker.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <atomic>

#ifndef VKBASALT_X11
#define VKBASALT_X11 1
#endif

#if VKBASALT_X11
#include "keyboard_input_x11.hpp"
#include <X11/Xlib.h>
#endif

namespace vkBasalt
{
    static bool blockingEnabled = false;
    // Atomic: written by overlay thread (setInputBlocked), read by game thread
    // (isInputBlocked via Wayland interpose wrapper callbacks)
    static std::atomic<bool> blocked{false};

#if VKBASALT_X11
    static bool grabbed = false;

    static void grabInput()
    {
        if (grabbed)
            return;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
            return;

        Window root = DefaultRootWindow(display);

        int kbResult = XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        int ptrResult = XGrabPointer(display, root, False,
                                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

        if (kbResult == GrabSuccess && ptrResult == GrabSuccess)
        {
            grabbed = true;
            Logger::debug("Input grabbed for overlay");
        }
        else
        {
            if (kbResult == GrabSuccess)
                XUngrabKeyboard(display, CurrentTime);
            if (ptrResult == GrabSuccess)
                XUngrabPointer(display, CurrentTime);
            Logger::debug("Failed to grab input");
        }

        XFlush(display);
    }

    static void ungrabInput()
    {
        if (!grabbed)
            return;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
            return;

        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
        XFlush(display);

        grabbed = false;
        Logger::debug("Input released from overlay");
    }
#endif

    void initInputBlocker(bool enabled)
    {
        blockingEnabled = enabled;

        if (isWayland())
        {
            // Wayland doesn't support global input grabs
            // Input events are delivered to our private event queue
            // and consumed by the overlay when visible
            Logger::debug(std::string("Input blocking ") + (enabled ? "enabled (Wayland: event consumption mode)" : "disabled"));
            return;
        }

#if VKBASALT_X11
        if (!enabled && grabbed)
        {
            ungrabInput();
            blocked.store(false, std::memory_order_release);
        }
#endif

        Logger::debug(std::string("Input blocking ") + (enabled ? "enabled" : "disabled"));
    }

    void setInputBlocked(bool shouldBlock)
    {
        if (!blockingEnabled)
            return;

        if (shouldBlock == blocked.load(std::memory_order_acquire))
            return;

        blocked.store(shouldBlock, std::memory_order_release);

        if (isWayland())
        {
            // On Wayland, interposed wl_proxy_add_listener wrapper callbacks
            // check isInputBlocked() and suppress events to the game
            Logger::debug(std::string("Wayland input blocking: ") + (shouldBlock ? "suppressing game events" : "forwarding game events"));
            return;
        }

#if VKBASALT_X11
        if (blocked)
            grabInput();
        else
            ungrabInput();
#endif
    }

    bool isInputBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }
}
