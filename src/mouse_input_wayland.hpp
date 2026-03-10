#pragma once

#include "mouse_input.hpp"

namespace vkBasalt
{
    MouseState getMouseStateWayland();

    // Initialize Wayland mouse from a captured wl_display
    bool initWaylandMouse();

    // Cleanup
    void cleanupWaylandMouse();

    // Mirror a button event from the game's pointer into overlay state.
    // The game's wl_pointer receives releases via implicit grab that our
    // overlay's private pointer never sees. Called from wayland_interpose.cpp.
    void mirrorButtonState(uint32_t button, bool pressed);
} // namespace vkBasalt
