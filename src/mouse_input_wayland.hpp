#pragma once

#include "mouse_input.hpp"

namespace vkBasalt
{
    MouseState getMouseStateWayland();

    // Initialize Wayland mouse from a captured wl_display
    bool initWaylandMouse();

    // Cleanup
    void cleanupWaylandMouse();
} // namespace vkBasalt
