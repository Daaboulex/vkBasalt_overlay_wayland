#include "wayland_display.hpp"

#include "logger.hpp"

#include <cstdlib>

namespace vkBasalt
{
    static wl_display* waylandDisplay = nullptr;
    static wl_surface* waylandSurface = nullptr;
    static int waylandChecked = -1; // -1 = unchecked, 0 = no, 1 = yes

    void setWaylandDisplay(wl_display* display)
    {
        if (!display)
            return;

        waylandDisplay = display;
        waylandChecked = 1;
        Logger::info("captured Wayland display from vkCreateWaylandSurfaceKHR");
    }

    wl_display* getWaylandDisplay()
    {
        return waylandDisplay;
    }

    void setWaylandSurface(wl_surface* surface)
    {
        if (!surface)
            return;

        waylandSurface = surface;
        Logger::info("captured Wayland surface from vkCreateWaylandSurfaceKHR");
    }

    wl_surface* getWaylandSurface()
    {
        return waylandSurface;
    }

    void setX11Surface()
    {
        if (waylandChecked == 1)
            return; // a Wayland surface/display already won the tie

        waylandChecked = 0;
        Logger::info("detected X11 (Xlib/Xcb) client -> X11 input backend");
    }

    bool isWayland()
    {
        // Authoritative: the windowing backend the app actually uses, set once by
        // setWaylandDisplay (from vkCreateWaylandSurfaceKHR) or setX11Surface
        // (from the enabled instance surface extension). Never from the env.
        if (waylandChecked >= 0)
            return waylandChecked == 1;

        // Not yet determined: fall back to the environment, but do NOT cache it,
        // so a later setX11Surface()/setWaylandDisplay() still sets the real
        // backend. Under XWayland both WAYLAND_DISPLAY and DISPLAY are set, so
        // WAYLAND_DISPLAY alone is not proof of a Wayland client -- that case is
        // resolved by setX11Surface() at instance creation before input is routed.
        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        return wlDisplay && *wlDisplay;
    }
} // namespace vkBasalt
