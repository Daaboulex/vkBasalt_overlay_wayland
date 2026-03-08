#pragma once

struct wl_display;
struct wl_surface;

namespace vkBasalt
{
    // Called from vkCreateWaylandSurfaceKHR to capture the game's wl_display
    void setWaylandDisplay(wl_display* display);

    // Returns the captured wl_display, or nullptr if not on Wayland
    wl_display* getWaylandDisplay();

    // Called from vkCreateWaylandSurfaceKHR to capture the game's wl_surface
    void setWaylandSurface(wl_surface* surface);

    // Returns the captured wl_surface, or nullptr if not on Wayland
    wl_surface* getWaylandSurface();

    // Returns true if running under Wayland (WAYLAND_DISPLAY env var is set)
    bool isWayland();
} // namespace vkBasalt
