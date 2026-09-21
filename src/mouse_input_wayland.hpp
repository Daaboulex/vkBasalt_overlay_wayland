#pragma once

#include "mouse_input.hpp"

namespace vkBasalt
{
    MouseState getMouseStateWayland();

    bool initWaylandMouse();

    void cleanupWaylandMouse();

    void setWaylandPointerBounds(int width, int height);

    void mirrorButtonState(uint32_t button, bool pressed);
} // namespace vkBasalt
