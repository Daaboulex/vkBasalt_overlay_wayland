#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "wayland_interpose.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <wayland-client.h>

#include <chrono>
#include <cstring>

namespace vkBasalt
{
    static wl_pointer* wlPointer = nullptr;

    static int pointerX = 0;
    static int pointerY = 0;
    static bool leftButton = false;
    static bool rightButton = false;
    static bool middleButton = false;
    static float scrollAccumulator = 0.0f;

    // Set when axis_discrete/axis_value120 fired this pointer frame, so the
    // continuous axis event is not double-counted.
    static bool discreteScrollReceived = false;

    // A compositor grab (e.g. Alt+drag) can consume a button release; synthesize
    // one after the pointer has been idle with the button held.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point leftPressTime{};
    static Clock::time_point rightPressTime{};
    static Clock::time_point middlePressTime{};
    static constexpr int AUTO_RELEASE_MS = 200;

    static bool motionSinceLastPoll = false;
    static Clock::time_point lastMotionTime{};

    static bool bindCallbackSet = false;
    static bool mouseLogged = false;
    static bool noPointerWarned = false;

    static void pointerEnter(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        pointerX = wl_fixed_to_int(sx);
        pointerY = wl_fixed_to_int(sy);

        // Button state is not cleared here: swapchain resizes cause rapid
        // leave/enter cycles mid-drag, and clearing would break the drag.

        Logger::trace("Wayland: pointer enter at " + std::to_string(pointerX) + "," + std::to_string(pointerY));
    }

    static void pointerLeave(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/)
    {
        Logger::trace("Wayland: pointer leave");
    }

    static void pointerMotion(void* /*data*/, wl_pointer* /*pointer*/,
                              uint32_t /*time*/, wl_fixed_t sx, wl_fixed_t sy)
    {
        pointerX = wl_fixed_to_int(sx);
        pointerY = wl_fixed_to_int(sy);
        motionSinceLastPoll = true;
    }

    static void pointerButton(void* /*data*/, wl_pointer* /*pointer*/,
                              uint32_t /*serial*/, uint32_t /*time*/,
                              uint32_t button, uint32_t state)
    {
        bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
        Logger::trace("Wayland: pointer button " + std::to_string(button) + " " + (pressed ? "pressed" : "released"));

        // evdev button codes: BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112.
        auto now = Clock::now();
        switch (button)
        {
            case 0x110:
                leftButton = pressed;
                if (pressed) leftPressTime = now;
                break;
            case 0x111:
                rightButton = pressed;
                if (pressed) rightPressTime = now;
                break;
            case 0x112:
                middleButton = pressed;
                if (pressed) middlePressTime = now;
                break;
        }
    }

    static void pointerAxis(void* /*data*/, wl_pointer* /*pointer*/,
                            uint32_t /*time*/, uint32_t axis, wl_fixed_t value)
    {
        if (axis != 0)
            return;
        if (discreteScrollReceived)
            return;

        float scroll = wl_fixed_to_double(value);
        scrollAccumulator -= scroll / 10.0f;
    }

    static void pointerFrame(void* /*data*/, wl_pointer* /*pointer*/)
    {
        discreteScrollReceived = false;
    }

    static void pointerAxisSource(void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*source*/)
    {
    }

    static void pointerAxisStop(void* /*data*/, wl_pointer* /*pointer*/,
                                uint32_t /*time*/, uint32_t /*axis*/)
    {
    }

    static void pointerAxisDiscrete(void* /*data*/, wl_pointer* /*pointer*/,
                                    uint32_t axis, int32_t discrete)
    {
        if (axis != 0)
            return;

        discreteScrollReceived = true;
        scrollAccumulator -= (float)discrete;
    }

    static void pointerAxisValue120(void* /*data*/, wl_pointer* /*pointer*/,
                                    uint32_t axis, int32_t value120)
    {
        // wl_pointer v8+ high-resolution scroll: 120 units = one wheel click.
        if (axis != 0)
            return;

        discreteScrollReceived = true;
        scrollAccumulator -= (float)value120 / 120.0f;
    }

    static void pointerAxisRelativeDirection(void* /*data*/, wl_pointer* /*pointer*/,
                                             uint32_t /*axis*/, uint32_t /*direction*/)
    {
    }

    static const wl_pointer_listener pointerListener = {
        .enter = pointerEnter,
        .leave = pointerLeave,
        .motion = pointerMotion,
        .button = pointerButton,
        .axis = pointerAxis,
        .frame = pointerFrame,
        .axis_source = pointerAxisSource,
        .axis_stop = pointerAxisStop,
        .axis_discrete = pointerAxisDiscrete,
        .axis_value120 = pointerAxisValue120,
        .axis_relative_direction = pointerAxisRelativeDirection,
    };

    static void bindPointer(wl_seat* seat)
    {
        if (wlPointer)
            return;

        wlPointer = wl_seat_get_pointer(seat);
        // Register before add_listener so the interpose layer passes it through unwrapped.
        registerOverlayProxy((wl_proxy*)wlPointer);
        wl_pointer_add_listener(wlPointer, &pointerListener, nullptr);
        Logger::debug("Wayland: pointer bound from shared seat");
    }

    bool initWaylandMouse()
    {
        if (!bindCallbackSet)
        {
            setPointerBindCallback(bindPointer);
            bindCallbackSet = true;
        }

        if (!initWaylandInputCommon())
            return false;

        if (wlPointer && !mouseLogged)
        {
            mouseLogged = true;
            Logger::info("Wayland mouse input initialized");
        }
        else if (!wlPointer && !noPointerWarned)
        {
            noPointerWarned = true;
            Logger::warn("Wayland: no pointer found on seat");
        }

        return wlPointer != nullptr;
    }

    void cleanupWaylandMouse()
    {
        if (wlPointer)
        {
            unregisterOverlayProxy((wl_proxy*)wlPointer);
            wl_pointer_destroy(wlPointer);
            wlPointer = nullptr;
        }

        bindCallbackSet = false;
        mouseLogged = false;
        noPointerWarned = false;
    }

    void mirrorButtonState(uint32_t button, bool pressed)
    {
        auto now = Clock::now();
        switch (button)
        {
            case 0x110:
                leftButton = pressed;
                if (pressed) leftPressTime = now;
                break;
            case 0x111:
                rightButton = pressed;
                if (pressed) rightPressTime = now;
                break;
            case 0x112:
                middleButton = pressed;
                if (pressed) middlePressTime = now;
                break;
        }
    }

    MouseState getMouseStateWayland()
    {
        MouseState state;

        bool ready = initWaylandMouse();
        dispatchWaylandInputEvents();
        if (!ready)
            return state;

        // While the pointer moves, buttons stay held (a drag); once motion stops
        // and no release arrives within AUTO_RELEASE_MS, release them.
        auto now = Clock::now();
        if (motionSinceLastPoll)
        {
            lastMotionTime = now;
            motionSinceLastPoll = false;
        }

        auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMotionTime).count();
        if (idleMs > AUTO_RELEASE_MS)
        {
            auto leftMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - leftPressTime).count();
            auto rightMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - rightPressTime).count();
            auto middleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - middlePressTime).count();

            if (leftButton && leftMs > AUTO_RELEASE_MS)
                leftButton = false;
            if (rightButton && rightMs > AUTO_RELEASE_MS)
                rightButton = false;
            if (middleButton && middleMs > AUTO_RELEASE_MS)
                middleButton = false;
        }

        state.x = pointerX;
        state.y = pointerY;
        state.leftButton = leftButton;
        state.rightButton = rightButton;
        state.middleButton = middleButton;
        state.scrollDelta = scrollAccumulator;
        scrollAccumulator = 0.0f;

        return state;
    }
} // namespace vkBasalt
