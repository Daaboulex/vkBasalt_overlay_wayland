#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "wayland_interpose.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <wayland-client.h>

#include <cstring>

namespace vkBasalt
{
    // Mouse-specific state (seat/queue come from wayland_input_common)
    static wl_pointer* wlPointer = nullptr;

    // Mouse state
    static int pointerX = 0;
    static int pointerY = 0;
    static bool leftButton = false;
    static bool rightButton = false;
    static bool middleButton = false;
    static float scrollAccumulator = 0.0f;

    // Per-frame flag: true when axis_discrete or axis_value120 fired for this
    // pointer frame, so we skip the continuous axis event to avoid double-counting.
    static bool discreteScrollReceived = false;

    // Frame counters for auto-release. On Wayland, compositors (KWin/tiling)
    // can intercept left-click drags as window moves, consuming the button
    // release so it never reaches our pointer. We auto-release after a few
    // frames to synthesize the missing release. Normal releases arrive within
    // 1-2 frames and clear the counter before auto-release triggers.
    static int leftPressFrames = 0;
    static int rightPressFrames = 0;
    static int middlePressFrames = 0;
    static constexpr int AUTO_RELEASE_FRAMES = 3;

    static bool mouseInitialized = false;

    // Pointer listener callbacks
    static void pointerEnter(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        pointerX = wl_fixed_to_int(sx);
        pointerY = wl_fixed_to_int(sy);

        // Do NOT clear button state here. Surface reconfigurations (swapchain
        // resize) cause rapid leave/enter cycles while the user is dragging.
        // Clearing buttons on enter breaks ImGui drag operations. Button state
        // is tracked purely from wl_pointer.button events. If a compositor grab
        // (Alt+drag) consumes a release, the next user click naturally clears it.

        Logger::debug("Wayland: pointer enter at " + std::to_string(pointerX) + "," + std::to_string(pointerY));
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
    }

    static void pointerButton(void* /*data*/, wl_pointer* /*pointer*/,
                              uint32_t /*serial*/, uint32_t /*time*/,
                              uint32_t button, uint32_t state)
    {
        bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
        Logger::debug("Wayland: pointer button " + std::to_string(button) + " " + (pressed ? "pressed" : "released"));

        // Linux evdev button codes: BTN_LEFT=0x110, BTN_RIGHT=0x111, BTN_MIDDLE=0x112
        switch (button)
        {
            case 0x110:
                leftButton = pressed;
                leftPressFrames = pressed ? 1 : 0;
                break;
            case 0x111:
                rightButton = pressed;
                rightPressFrames = pressed ? 1 : 0;
                break;
            case 0x112:
                middleButton = pressed;
                middlePressFrames = pressed ? 1 : 0;
                break;
        }
    }

    static void pointerAxis(void* /*data*/, wl_pointer* /*pointer*/,
                            uint32_t /*time*/, uint32_t axis, wl_fixed_t value)
    {
        // Only use continuous axis as fallback when discrete events are not sent.
        // When both fire for the same pointer frame, discrete/value120 takes priority.
        if (axis != 0)
            return;
        if (discreteScrollReceived)
            return;

        // Negative value = scroll up, positive = scroll down
        // Normalize: typical step is 10.0 fixed-point
        float scroll = wl_fixed_to_double(value);
        scrollAccumulator -= scroll / 10.0f;
    }

    static void pointerFrame(void* /*data*/, wl_pointer* /*pointer*/)
    {
        // Reset per-frame discrete flag at the end of each pointer frame
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
        // Discrete scroll events (wheel clicks) — preferred over continuous axis
        if (axis != 0)
            return;

        discreteScrollReceived = true;
        scrollAccumulator -= (float)discrete; // Wayland: positive = scroll down, ImGui: positive = scroll up
    }

    static void pointerAxisValue120(void* /*data*/, wl_pointer* /*pointer*/,
                                    uint32_t axis, int32_t value120)
    {
        // High-resolution scroll (wl_pointer v8+). 120 units = one wheel click.
        // Preferred over both axis and axis_discrete when available.
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

    // Called by shared seat listener when pointer capability is available
    static void bindPointer(wl_seat* seat)
    {
        if (wlPointer)
            return;

        wlPointer = wl_seat_get_pointer(seat);
        // Register as overlay proxy BEFORE add_listener so the interposition
        // layer passes this through without wrapping
        registerOverlayProxy((wl_proxy*)wlPointer);
        wl_pointer_add_listener(wlPointer, &pointerListener, nullptr);
        Logger::debug("Wayland: pointer bound from shared seat");
    }

    bool initWaylandMouse()
    {
        if (mouseInitialized)
            return wlPointer != nullptr;

        mouseInitialized = true;

        // Register our callback before initializing shared resources
        setPointerBindCallback(bindPointer);

        // Initialize shared seat/queue/registry (triggers seat capability callbacks)
        if (!initWaylandInputCommon())
            return false;

        if (wlPointer)
            Logger::info("Wayland mouse input initialized");
        else
            Logger::warn("Wayland: no pointer found on seat");

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

        mouseInitialized = false;

        // Clean up shared resources (idempotent)
        cleanupWaylandInputCommon();
    }

    void mirrorButtonState(uint32_t button, bool pressed)
    {
        switch (button)
        {
            case 0x110:
                leftButton = pressed;
                leftPressFrames = pressed ? 1 : 0;
                break;
            case 0x111:
                rightButton = pressed;
                rightPressFrames = pressed ? 1 : 0;
                break;
            case 0x112:
                middleButton = pressed;
                middlePressFrames = pressed ? 1 : 0;
                break;
        }
    }

    MouseState getMouseStateWayland()
    {
        MouseState state;

        if (!initWaylandMouse())
            return state;

        dispatchWaylandInputEvents();

        // Auto-release buttons whose release was consumed by the compositor
        // (e.g., KWin intercepting left-click drag as a window move).
        // Normal releases arrive within 1-2 frames and reset the counter.
        if (leftButton && ++leftPressFrames > AUTO_RELEASE_FRAMES)
        {
            leftButton = false;
            leftPressFrames = 0;
        }
        if (rightButton && ++rightPressFrames > AUTO_RELEASE_FRAMES)
        {
            rightButton = false;
            rightPressFrames = 0;
        }
        if (middleButton && ++middlePressFrames > AUTO_RELEASE_FRAMES)
        {
            middleButton = false;
            middlePressFrames = 0;
        }

        state.x = pointerX;
        state.y = pointerY;
        state.leftButton = leftButton;
        state.rightButton = rightButton;
        state.middleButton = middleButton;
        state.scrollDelta = scrollAccumulator;
        scrollAccumulator = 0.0f;

        // Trace-level per-frame state dump (use VKBASALT_LOG_LEVEL=trace)
        if (state.leftButton || state.rightButton || state.middleButton || state.scrollDelta != 0.0f)
        {
            Logger::trace("mouse state: pos=(" + std::to_string(state.x) + "," + std::to_string(state.y)
                + ") L=" + std::to_string(state.leftButton)
                + " R=" + std::to_string(state.rightButton)
                + " M=" + std::to_string(state.middleButton)
                + " scroll=" + std::to_string(state.scrollDelta));
        }

        return state;
    }
} // namespace vkBasalt
