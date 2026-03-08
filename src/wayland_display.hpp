#pragma once

struct wl_display;

namespace vkBasalt
{
    // Called from vkCreateWaylandSurfaceKHR to capture the game's wl_display
    void setWaylandDisplay(wl_display* display);

    // Returns the captured wl_display, or nullptr if not on Wayland
    wl_display* getWaylandDisplay();

    // Returns true if running under Wayland (WAYLAND_DISPLAY is set and display was captured)
    bool isWayland();
} // namespace vkBasalt
