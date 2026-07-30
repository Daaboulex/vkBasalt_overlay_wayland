#pragma once

#include <cstdint>

namespace vkBasalt
{
    enum class KeyMode
    {
        held,
        press,
        toggle
    };

    // Shaders are written against Windows virtual-key codes; the input backends
    // work in X11/xkb keysyms, which share one numbering. 0 means unmapped.
    inline uint32_t keysymForVirtualKey(int vk)
    {
        if (vk >= 0x30 && vk <= 0x39)
            return static_cast<uint32_t>(vk);
        if (vk >= 0x41 && vk <= 0x5A)
            return static_cast<uint32_t>(vk - 0x41 + 0x61);
        if (vk >= 0x70 && vk <= 0x87)
            return static_cast<uint32_t>(0xffbe + (vk - 0x70));

        switch (vk)
        {
            case 0x08: return 0xff08;
            case 0x09: return 0xff09;
            case 0x0D: return 0xff0d;
            case 0x10: return 0xffe1;
            case 0x11: return 0xffe3;
            case 0x12: return 0xffe9;
            case 0x14: return 0xffe5;
            case 0x1B: return 0xff1b;
            case 0x20: return 0x0020;
            case 0x21: return 0xff55;
            case 0x22: return 0xff56;
            case 0x23: return 0xff57;
            case 0x24: return 0xff50;
            case 0x25: return 0xff51;
            case 0x26: return 0xff52;
            case 0x27: return 0xff53;
            case 0x28: return 0xff54;
            case 0x2D: return 0xff63;
            case 0x2E: return 0xffff;
            default: return 0;
        }
    }

    // press fires only on the frame the key goes down; toggle flips on each such
    // frame and holds between them; held mirrors the key.
    inline bool applyKeyMode(KeyMode mode, bool isDown, bool& wasDown, bool& toggled)
    {
        bool result = false;
        switch (mode)
        {
            case KeyMode::press: result = isDown && !wasDown; break;
            case KeyMode::toggle:
                if (isDown && !wasDown)
                    toggled = !toggled;
                result = toggled;
                break;
            case KeyMode::held: result = isDown; break;
        }
        wasDown = isDown;
        return result;
    }
} // namespace vkBasalt
