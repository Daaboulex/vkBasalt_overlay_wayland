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
            return;

        waylandChecked = 0;
        Logger::info("detected X11 (Xlib/Xcb) client -> X11 input backend");
    }

    bool isWayland()
    {
        if (waylandChecked >= 0)
            return waylandChecked == 1;

        // Undetermined: fall back to the environment without caching it, so a later
        // setX11Surface()/setWaylandDisplay() still decides. XWayland clients have
        // both WAYLAND_DISPLAY and DISPLAY set, so the env var alone is not proof.
        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        return wlDisplay && *wlDisplay;
    }
} // namespace vkBasalt
