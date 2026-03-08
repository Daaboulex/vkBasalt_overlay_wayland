#pragma once

struct wl_display;

namespace vkBasalt
{
    // Called from vkCreateWaylandSurfaceKHR to capture the game's wl_display
    void setWaylandDisplay(wl_display* display);

    // Returns the captured wl_display, or nullptr if not on Wayland
    wl_display* getWaylandDisplay();

    // Returns true if running under Wayland (WAYLAND_DISPLAY env var is set)
    bool isWayland();
} // namespace vkBasalt
