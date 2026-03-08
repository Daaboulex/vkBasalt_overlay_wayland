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

    bool isWayland()
    {
        if (waylandChecked >= 0)
            return waylandChecked == 1;

        // Check environment before surface creation — if WAYLAND_DISPLAY is set,
        // we're on Wayland even if we haven't captured the display yet. The input
        // backends will gracefully no-op until initWayland*() succeeds.
        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        if (wlDisplay && *wlDisplay)
        {
            waylandChecked = 1;
            return true;
        }

        waylandChecked = 0;
        return false;
    }
} // namespace vkBasalt
