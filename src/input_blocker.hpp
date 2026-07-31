#pragma once

namespace vkBasalt
{
    void initInputBlocker(bool enabled);

    void setInputBlocked(bool blocked);
    bool isInputBlocked();

    // True once the application no longer holds the input focus it held when the
    // overlay took it. An X11 grab outlives a focus change, so without this the
    // cursor stays captured by a window the user has already left.
    bool inputFocusLost();
}
