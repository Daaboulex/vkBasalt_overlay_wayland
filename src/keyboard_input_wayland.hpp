#pragma once

#include <cstdint>
#include <string>
#include "keyboard_input.hpp"

namespace vkBasalt
{
    uint32_t convertToKeySymWayland(std::string key);
    bool     isKeyPressedWayland(uint32_t ks);
    KeyboardState getKeyboardStateWayland();

    bool initWaylandKeyboard();

    void cleanupWaylandKeyboard();
} // namespace vkBasalt
