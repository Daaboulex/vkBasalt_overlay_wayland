#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "wayland_interpose.hpp"
#include "wayland_pointer_state.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <mutex>

namespace vkBasalt
{
    static wl_pointer*              wlPointer       = nullptr;
    static zwp_relative_pointer_v1* relativePointer = nullptr;

    static std::mutex          stateMutex;
    static WaylandPointerState state;

    static bool bindCallbackSet = false;
    static bool mouseLogged     = false;
    static bool noPointerWarned = false;

    static void pointerEnter(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));

        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer enter at " + std::to_string(wl_fixed_to_int(sx)) + "," + std::to_string(wl_fixed_to_int(sy)));
    }

    static void pointerLeave(void* /*data*/, wl_pointer* /*pointer*/,
                             uint32_t /*serial*/, wl_surface* /*surface*/)
    {
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer leave");
    }

    static void pointerMotion(void* /*data*/, wl_pointer* /*pointer*/,
                              uint32_t /*time*/, wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }

#ifdef WL_POINTER_WARP_SINCE_VERSION
    static void pointerWarp(void* /*data*/, wl_pointer* /*pointer*/,
                            wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }
#endif

    static void pointerButton(void* /*data*/, wl_pointer* /*pointer*/,
                              uint32_t /*serial*/, uint32_t /*time*/,
                              uint32_t button, uint32_t buttonState)
    {
        const bool pressed = (buttonState == WL_POINTER_BUTTON_STATE_PRESSED);
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer button " + std::to_string(button) + " " + (pressed ? "pressed" : "released"));

        std::lock_guard<std::mutex> lock(stateMutex);
        state.button(button, pressed, WaylandPointerState::Clock::now());
    }

    static void pointerAxis(void* /*data*/, wl_pointer* /*pointer*/,
                            uint32_t /*time*/, uint32_t axis, wl_fixed_t value)
    {
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollContinuous(wl_fixed_to_double(value));
    }

    static void pointerFrame(void* /*data*/, wl_pointer* /*pointer*/)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.frame();
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
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollDiscrete(discrete);
    }

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
    static void pointerAxisValue120(void* /*data*/, wl_pointer* /*pointer*/,
                                    uint32_t axis, int32_t value120)
    {
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollDiscrete(value120 / 120.0);
    }
#endif

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
    static void pointerAxisRelativeDirection(void* /*data*/, wl_pointer* /*pointer*/,
                                             uint32_t /*axis*/, uint32_t /*direction*/)
    {
    }
#endif

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
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
        .axis_value120 = pointerAxisValue120,
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
        .axis_relative_direction = pointerAxisRelativeDirection,
#endif
#ifdef WL_POINTER_WARP_SINCE_VERSION
        .warp = pointerWarp,
#endif
    };

    static void relativeMotion(void* /*data*/, zwp_relative_pointer_v1* /*relative*/,
                               uint32_t /*utimeHi*/, uint32_t /*utimeLo*/,
                               wl_fixed_t dx, wl_fixed_t dy,
                               wl_fixed_t /*dxUnaccel*/, wl_fixed_t /*dyUnaccel*/)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.relativeMotion(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    }

    static const zwp_relative_pointer_v1_listener relativePointerListener = {
        .relative_motion = relativeMotion,
    };

    static void bindPointer(wl_seat* seat)
    {
        if (wlPointer)
            return;

        wlPointer = wl_seat_get_pointer(seat);
        registerOverlayProxy((wl_proxy*)wlPointer);
        wl_pointer_add_listener(wlPointer, &pointerListener, nullptr);

        zwp_relative_pointer_manager_v1* manager = getWaylandRelativePointerManager();
        if (!manager)
        {
            Logger::warn("Wayland: the compositor offers no zwp_relative_pointer_manager_v1, so the overlay cursor "
                         "cannot move while a game holds the pointer locked");
            return;
        }

        relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(manager, wlPointer);
        zwp_relative_pointer_v1_add_listener(relativePointer, &relativePointerListener, nullptr);
        Logger::debug("Wayland: pointer and relative pointer bound from shared seat");
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
        if (relativePointer)
        {
            zwp_relative_pointer_v1_destroy(relativePointer);
            relativePointer = nullptr;
        }
        if (wlPointer)
        {
            unregisterOverlayProxy((wl_proxy*)wlPointer);
            wl_pointer_destroy(wlPointer);
            wlPointer = nullptr;
        }

        bindCallbackSet = false;
        mouseLogged     = false;
        noPointerWarned = false;
    }

    void setWaylandPointerBounds(int width, int height)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.setBounds(width, height);
    }

    void mirrorButtonState(uint32_t button, bool pressed)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.button(button, pressed, WaylandPointerState::Clock::now());
    }

    MouseState getMouseStateWayland()
    {
        MouseState result;

        const bool ready = initWaylandMouse();
        dispatchWaylandInputEvents();
        if (!ready)
            return result;

        std::lock_guard<std::mutex> lock(stateMutex);
        const PointerSample sample = state.sample(WaylandPointerState::Clock::now());

        result.x            = sample.x;
        result.y            = sample.y;
        result.leftButton   = sample.left;
        result.rightButton  = sample.right;
        result.middleButton = sample.middle;
        result.scrollDelta  = sample.scroll;
        return result;
    }
} // namespace vkBasalt
