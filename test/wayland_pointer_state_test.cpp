#include "wayland_pointer_state.hpp"

#include <chrono>
#include <cstdio>

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
    using Clock = WaylandPointerState::Clock;
    using namespace std::chrono_literals;

    const Clock::time_point t0 = Clock::now();

    {
        WaylandPointerState state;
        state.setBounds(200, 100);

        state.absoluteMotion(10.0, 10.0);
        state.relativeMotion(5.0, 5.0);
        state.frame();
        const PointerSample unlocked = state.sample(t0);
        expect(unlocked.x == 10 && unlocked.y == 10, "absolute motion wins over the relative delta of the same frame");

        state.relativeMotion(3.0, -2.0);
        state.frame();
        const PointerSample locked = state.sample(t0);
        expect(locked.x == 13 && locked.y == 8, "a frame without absolute motion moves the cursor by the relative delta");

        state.relativeMotion(500.0, 500.0);
        state.frame();
        const PointerSample edge = state.sample(t0);
        expect(edge.x == 199 && edge.y == 99, "a relative-driven cursor stops at the last pixel of the bounds");

        state.relativeMotion(-1.0, 0.0);
        state.frame();
        const PointerSample back = state.sample(t0);
        expect(back.x == 198 && back.y == 99, "moving back from the edge takes effect at once, with no invisible overshoot to unwind");

        state.absoluteMotion(50.0, 40.0);
        state.frame();
        const PointerSample resync = state.sample(t0);
        expect(resync.x == 50 && resync.y == 40, "absolute motion resynchronises the cursor after relative movement");
    }

    {
        WaylandPointerState state;
        state.relativeMotion(4.0, 6.0);
        const PointerSample noFrame = state.sample(t0);
        expect(noFrame.x == 4 && noFrame.y == 6, "a compositor that sends no frame events still moves the cursor by the next poll");
    }

    {
        WaylandPointerState state;
        state.absoluteMotion(0.0, 0.0);
        state.button(WaylandPointerState::BTN_LEFT, true, t0);
        state.button(WaylandPointerState::BTN_LEFT, false, t0 + 5ms);
        const PointerSample first = state.sample(t0 + 10ms);
        expect(first.left, "a press and release inside one frame interval is seen as down once");
        const PointerSample second = state.sample(t0 + 20ms);
        expect(!second.left, "the latched press reads as up on the following frame, completing the click");

        state.button(WaylandPointerState::BTN_RIGHT, true, t0 + 30ms);
        const PointerSample held = state.sample(t0 + 40ms);
        expect(held.right, "a held button reads as down");
        state.button(WaylandPointerState::BTN_RIGHT, false, t0 + 50ms);
        const PointerSample released = state.sample(t0 + 60ms);
        expect(!released.right, "a released button reads as up");
        expect(!released.left && !released.middle, "other buttons are untouched");
    }

    {
        WaylandPointerState state;
        state.absoluteMotion(0.0, 0.0);
        state.sample(t0);
        state.button(WaylandPointerState::BTN_LEFT, true, t0 + 10ms);
        expect(state.sample(t0 + 100ms).left, "a consumed release keeps the button down while the hold is short");
        expect(!state.sample(t0 + 400ms).left, "a button whose release the compositor consumed is let go once the pointer has rested");

        state.button(WaylandPointerState::BTN_LEFT, true, t0 + 500ms);
        state.absoluteMotion(1.0, 1.0);
        state.sample(t0 + 700ms);
        state.relativeMotion(1.0, 0.0);
        state.frame();
        expect(state.sample(t0 + 900ms).left, "a drag, whether absolute or relative, keeps the button held");
    }

    {
        WaylandPointerState state;
        state.scrollDiscrete(1.0);
        state.scrollContinuous(10.0);
        state.frame();
        state.scrollContinuous(10.0);
        state.frame();
        const PointerSample scrolled = state.sample(t0);
        expect(scrolled.scroll == -2.0f, "a discrete step silences the continuous axis of its own frame only");
        expect(state.sample(t0).scroll == 0.0f, "sampling drains the scroll accumulator");
    }

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the overlay cursor follows absolute motion, relative motion under a lock, and no click is lost\n");
    return 0;
}
