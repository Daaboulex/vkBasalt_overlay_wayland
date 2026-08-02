#ifndef MOUSE_INPUT_HPP_INCLUDED
#define MOUSE_INPUT_HPP_INCLUDED

#include <cstdint>

namespace vkBasalt
{
    struct MouseState
    {
        int x = 0;
        int y = 0;
        bool leftButton = false;
        bool rightButton = false;
        bool middleButton = false;
        float scrollDelta = 0.0f; // positive = up
    };

    // Reading the state drains the scroll accumulator, so the overlay and the
    // ReShade uniforms would each take part of a frame's scrolling. Marking the
    // frame makes every reader in it see one snapshot.
    void beginMouseInputFrame();

    MouseState getMouseState();

} // namespace vkBasalt

#endif // MOUSE_INPUT_HPP_INCLUDED
