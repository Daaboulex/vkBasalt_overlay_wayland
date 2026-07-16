#pragma once

struct wl_display;
struct wl_surface;

namespace vkBasalt
{
    void setWaylandDisplay(wl_display* display);

    wl_display* getWaylandDisplay();

    void setWaylandSurface(wl_surface* surface);

    wl_surface* getWaylandSurface();

    // Selects the X11 input backend for clients that enable only an X11 surface
    // extension (XWayland/Wine), even when WAYLAND_DISPLAY is set.
    void setX11Surface();

    bool isWayland();
} // namespace vkBasalt
