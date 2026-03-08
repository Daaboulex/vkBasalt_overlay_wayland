#include "wayland_display.hpp"

#include "logger.hpp"

#include <cstdlib>

namespace vkBasalt
{
    static wl_display* waylandDisplay = nullptr;
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

    bool isWayland()
    {
        if (waylandChecked >= 0)
            return waylandChecked == 1;

        // Check environment before surface creation
        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        if (wlDisplay && *wlDisplay)
        {
            // Wayland is likely but we haven't captured the display yet
            // Return false until vkCreateWaylandSurfaceKHR is called
            return false;
        }

        waylandChecked = 0;
        return false;
    }
} // namespace vkBasalt
