#pragma once

#include "mouse_input.hpp"

namespace vkBasalt
{
    MouseState getMouseStateWayland();

    bool initWaylandMouse();

    void cleanupWaylandMouse();

    // Mirrors button events from the game's pointer: releases consumed by the
    // implicit grab reach the game's pointer but not the overlay's.
    void mirrorButtonState(uint32_t button, bool pressed);
} // namespace vkBasalt
