#pragma once

#include <cstdint>
#include <string>
#include "keyboard_input.hpp"

namespace vkBasalt
{
    uint32_t convertToKeySymWayland(std::string key);
    bool     isKeyPressedWayland(uint32_t ks);
    KeyboardState getKeyboardStateWayland();

    // Initialize Wayland keyboard from a captured wl_display
    bool initWaylandKeyboard();

    // Cleanup
    void cleanupWaylandKeyboard();
} // namespace vkBasalt
