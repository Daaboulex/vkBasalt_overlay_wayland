#include "input_blocker.hpp"
#include "wayland_display.hpp"
#include "wayland_interpose.hpp"
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
    static std::atomic<bool> blocked{false};
    static bool interposeUnavailableWarned = false;

#if VKBASALT_X11
    static bool grabbed = false;

    static bool grabInput()
    {
        if (grabbed)
            return true;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
        {
            Logger::warn("X11: no display for the overlay grab -- input blocking unavailable");
            return false;
        }

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
            Logger::warn("X11: overlay grab refused (keyboard " + std::to_string(kbResult) + ", pointer " + std::to_string(ptrResult)
                         + ") -- input blocking unavailable, the game still receives input");
        }

        XFlush(display);
        return grabbed;
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
            // Wayland has no global input grabs; blocking works by suppressing
            // events in the interposed game listeners.
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

        if (isWayland())
        {
            // The interpose is the only thing that can suppress the game's events.
            // Wine loads winewayland.so via dlopen(RTLD_LOCAL), whose private symbol
            // scope bypasses it unless libvkbasalt-audit is active via LD_AUDIT.
            // X11 grabs are not a fallback: wine Wayland games do not take input
            // through XWayland, and stale grabs cause compositor focus issues.
            if (shouldBlock && !waylandInterposeActive())
            {
                if (!interposeUnavailableWarned)
                {
                    interposeUnavailableWarned = true;
                    Logger::warn("Wayland: input blocking requested but no game input listener was wrapped, so the game "
                                 "would keep receiving events. Blocking stays OFF. Under wine this needs the audit shim: "
                                 "launch via vkbasalt-run, or set LD_AUDIT to libvkbasalt-audit.so.");
                }
                return;
            }

            blocked.store(shouldBlock, std::memory_order_release);
            Logger::debug(std::string("Wayland input blocking: ") + (shouldBlock ? "suppressing game events" : "forwarding game events"));
            // Synthetic leave/enter releases keys the game held when the overlay opened.
            notifyGameKeyboardFocus(!shouldBlock);
            return;
        }

#if VKBASALT_X11
        if (shouldBlock)
        {
            if (!grabInput())
                return;
            blocked.store(true, std::memory_order_release);
        }
        else
        {
            ungrabInput();
            blocked.store(false, std::memory_order_release);
        }
#endif
    }

    bool isInputBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }
}
