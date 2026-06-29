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

    // Called when the app is detected as an X11 (Xlib/Xcb) client -- e.g. an
    // XWayland/Wine game that enables only an X11 surface extension. Selects the
    // X11 input backend even when WAYLAND_DISPLAY is set in the environment.
    void setX11Surface();

    // Returns true for a Wayland client. Decided from the surface/extension the
    // app actually uses (authoritative); falls back to WAYLAND_DISPLAY only until
    // that is known.
    bool isWayland();
} // namespace vkBasalt
