#include "mouse_input_wayland.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <wayland-client.h>

#include <cstring>

namespace vkBasalt
{
    // Shared Wayland objects (reuse seat from keyboard init)
    static wl_display* mouseDisplayWrapper = nullptr;
    static wl_event_queue* mouseQueue = nullptr;
    static wl_registry* mouseRegistry = nullptr;
    static wl_seat* mouseSeat = nullptr;
    static wl_pointer* wlPointer = nullptr;

    // Mouse state
    static int pointerX = 0;
    static int pointerY = 0;
    static bool leftButton = false;
    static bool rightButton = false;
    static bool middleButton = false;
    static float scrollAccumulator = 0.0f;

    static bool mouseInitialized = false;

    // Pointer listener callbacks
    static void pointerEnter(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        pointerX = wl_fixed_to_int(sx);
        pointerY = wl_fixed_to_int(sy);
        Logger::debug("Wayland: pointer enter at " + std::to_string(pointerX) + "," + std::to_string(pointerY));
    }

    static void pointerLeave(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/)
    {
        // Release all buttons on leave — Wayland's implicit grab can cause
        // leave/enter cycles that swallow release events
        leftButton = false;
        rightButton = false;
        middleButton = false;
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
            case 0x110: leftButton = pressed; break;   // BTN_LEFT
            case 0x111: rightButton = pressed; break;   // BTN_RIGHT
            case 0x112: middleButton = pressed; break;  // BTN_MIDDLE
        }
    }

    static void pointerAxis(void* /*data*/, wl_pointer* /*pointer*/,
                            uint32_t /*time*/, uint32_t axis, wl_fixed_t value)
    {
        // WL_POINTER_AXIS_VERTICAL_SCROLL = 0
        if (axis == 0)
        {
            // Negative value = scroll up, positive = scroll down
            // Normalize: typical step is 10.0 fixed-point
            float scroll = wl_fixed_to_double(value);
            scrollAccumulator -= scroll / 10.0f;
        }
    }

    static void pointerFrame(void* /*data*/, wl_pointer* /*pointer*/)
    {
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
        // Discrete scroll events (wheel clicks) — more precise than axis
        if (axis == 0)
            scrollAccumulator += (float)discrete; // Positive = up
    }

    static void pointerAxisValue120(void* /*data*/, wl_pointer* /*pointer*/,
                                    uint32_t /*axis*/, int32_t /*value120*/)
    {
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

    // Seat listener for mouse
    static void mouseSeatCapabilities(void* /*data*/, wl_seat* seat, uint32_t caps)
    {
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wlPointer)
        {
            wlPointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(wlPointer, &pointerListener, nullptr);
            Logger::debug("Wayland: pointer bound from seat");
        }
    }

    static void mouseSeatName(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/)
    {
    }

    static const wl_seat_listener mouseSeatListener = {
        .capabilities = mouseSeatCapabilities,
        .name = mouseSeatName,
    };

    // Registry listener for mouse
    static void mouseRegistryGlobal(void* /*data*/, wl_registry* registry,
                                    uint32_t name, const char* interface, uint32_t version)
    {
        if (std::strcmp(interface, wl_seat_interface.name) == 0 && !mouseSeat)
        {
            mouseSeat = (wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface,
                                                     version < 5 ? version : 5);
            wl_seat_add_listener(mouseSeat, &mouseSeatListener, nullptr);
        }
    }

    static void mouseRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/)
    {
    }

    static const wl_registry_listener mouseRegistryListener = {
        .global = mouseRegistryGlobal,
        .global_remove = mouseRegistryGlobalRemove,
    };

    bool initWaylandMouse()
    {
        if (mouseInitialized)
            return wlPointer != nullptr;

        mouseInitialized = true;

        wl_display* display = getWaylandDisplay();
        if (!display)
            return false;

        mouseDisplayWrapper = (wl_display*)wl_proxy_create_wrapper(display);
        if (!mouseDisplayWrapper)
            return false;

        mouseQueue = wl_display_create_queue(display);
        if (!mouseQueue)
        {
            wl_proxy_wrapper_destroy(mouseDisplayWrapper);
            mouseDisplayWrapper = nullptr;
            return false;
        }

        wl_proxy_set_queue((wl_proxy*)mouseDisplayWrapper, mouseQueue);

        mouseRegistry = wl_display_get_registry(mouseDisplayWrapper);
        wl_registry_add_listener(mouseRegistry, &mouseRegistryListener, nullptr);

        wl_display_roundtrip_queue(display, mouseQueue);
        wl_display_roundtrip_queue(display, mouseQueue);

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
            wl_pointer_destroy(wlPointer);
            wlPointer = nullptr;
        }
        if (mouseSeat)
        {
            wl_seat_destroy(mouseSeat);
            mouseSeat = nullptr;
        }
        if (mouseRegistry)
        {
            wl_registry_destroy(mouseRegistry);
            mouseRegistry = nullptr;
        }
        if (mouseQueue)
        {
            wl_event_queue_destroy(mouseQueue);
            mouseQueue = nullptr;
        }
        if (mouseDisplayWrapper)
        {
            wl_proxy_wrapper_destroy(mouseDisplayWrapper);
            mouseDisplayWrapper = nullptr;
        }

        mouseInitialized = false;
    }

    MouseState getMouseStateWayland()
    {
        MouseState state;

        if (!initWaylandMouse())
            return state;

        // Dispatch pending events on our private queue
        wl_display* display = getWaylandDisplay();
        if (display)
            wl_display_dispatch_queue_pending(display, mouseQueue);

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
