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
    static bool   grabbed     = false;
    static Window focusAtGrab = None;

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

        int focusRevert = 0;
        if (XGetInputFocus(display, &focusAtGrab, &focusRevert) == 0)
            focusAtGrab = None;

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

    static void releaseBlock()
    {
        if (!blocked.load(std::memory_order_acquire))
            return;

        blocked.store(false, std::memory_order_release);

        if (isWayland())
        {
            Logger::debug("Wayland input blocking: forwarding game events");
            notifyGameKeyboardFocus(true);
            return;
        }

#if VKBASALT_X11
        ungrabInput();
#endif
    }

    static void engageBlock()
    {
        if (blocked.load(std::memory_order_acquire))
            return;

        if (isWayland())
        {
            if (!waylandInterposeActive())
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
        }
#if VKBASALT_X11
        else if (!grabInput())
            return;
#endif

        blocked.store(true, std::memory_order_release);

        if (isWayland())
        {
            Logger::debug("Wayland input blocking: suppressing game events");
            notifyGameKeyboardFocus(false);
        }
    }

    void initInputBlocker(bool enabled)
    {
        blockingEnabled = enabled;

        if (!enabled)
            releaseBlock();

        Logger::debug(std::string("Input blocking ") + (enabled ? "enabled" : "disabled")
                      + (isWayland() ? " (Wayland: event consumption mode)" : ""));
    }

    void setInputBlocked(bool shouldBlock)
    {
        if (!shouldBlock)
        {
            releaseBlock();
            return;
        }

        if (!blockingEnabled)
            return;

        engageBlock();
    }

    bool isInputBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }

    bool inputFocusLost()
    {
#if VKBASALT_X11
        if (!grabbed || focusAtGrab == None)
            return false;

        Display* display = (Display*) getKeyboardDisplay();
        if (!display)
            return false;

        Window current = None;
        int    revert  = 0;
        if (XGetInputFocus(display, &current, &revert) == 0)
            return false;

        return current != focusAtGrab;
#else
        return false;
#endif
    }
}
