#include "mouse_input.hpp"

#include <cstdio>
#include <cstdlib>

namespace vkBasalt
{
    static int   backendSamples = 0;
    static float pendingScroll  = 0.0f;

    bool isWayland()
    {
        return true;
    }

    MouseState getMouseStateWayland()
    {
        backendSamples++;
        MouseState state;
        state.scrollDelta = pendingScroll;
        pendingScroll     = 0.0f;
        return state;
    }
}

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    using namespace vkBasalt;

    pendingScroll = 3.0f;
    beginMouseInputFrame();

    const MouseState atPresent = getMouseState();
    const MouseState atRecord  = getMouseState();

    expect(atPresent.scrollDelta == 3.0f, "the first reader in a frame sees the scrolling");
    expect(atRecord.scrollDelta == 3.0f, "the second reader in the same frame sees the same scrolling");
    expect(backendSamples == 1, "the backend is sampled once a frame, not once a reader");

    pendingScroll = 5.0f;
    beginMouseInputFrame();

    const MouseState nextFrame = getMouseState();
    expect(nextFrame.scrollDelta == 5.0f, "a new frame samples again instead of repeating the snapshot");
    expect(backendSamples == 2, "marking a frame is what refreshes the snapshot");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: one mouse sample a frame, shared by every reader\n");
    return 0;
}
