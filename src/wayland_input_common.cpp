#include "wayland_input_common.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <cstring>

namespace vkBasalt
{
    static wl_display* displayWrapper = nullptr;
    static wl_event_queue* queue = nullptr;
    static wl_registry* registry = nullptr;
    static wl_seat* seat = nullptr;
    static bool commonInitialized = false;

    // Device bind callbacks — set by keyboard/mouse modules before init
    static KeyboardBindCallback keyboardBind = nullptr;
    static PointerBindCallback pointerBind = nullptr;

    void setKeyboardBindCallback(KeyboardBindCallback cb)
    {
        keyboardBind = cb;
        // If seat already bound, invoke callback immediately for late registration
        if (seat && cb)
            cb(seat);
    }

    void setPointerBindCallback(PointerBindCallback cb)
    {
        pointerBind = cb;
        // If seat already bound, invoke callback immediately for late registration
        if (seat && cb)
            cb(seat);
    }

    // Single seat listener that handles both keyboard and pointer capabilities
    static void seatCapabilities(void* /*data*/, wl_seat* s, uint32_t caps)
    {
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboardBind)
            keyboardBind(s);
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && pointerBind)
            pointerBind(s);
    }

    static void seatName(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/)
    {
    }

    static const wl_seat_listener sharedSeatListener = {
        .capabilities = seatCapabilities,
        .name = seatName,
    };

    static void registryGlobal(void* /*data*/, wl_registry* reg,
                               uint32_t name, const char* interface, uint32_t version)
    {
        if (std::strcmp(interface, wl_seat_interface.name) != 0)
            return;
        if (seat)
            return;

        seat = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface,
                                            version < 5 ? version : 5);
        wl_seat_add_listener(seat, &sharedSeatListener, nullptr);
        Logger::debug("Wayland: shared seat bound");
    }

    static void registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/)
    {
    }

    static const wl_registry_listener registryListener = {
        .global = registryGlobal,
        .global_remove = registryGlobalRemove,
    };

    wl_event_queue* getWaylandInputQueue()
    {
        return queue;
    }

    wl_seat* getWaylandSeat()
    {
        return seat;
    }

    bool initWaylandInputCommon()
    {
        if (commonInitialized)
            return seat != nullptr;

        commonInitialized = true;

        wl_display* display = getWaylandDisplay();
        if (!display)
            return false;

        displayWrapper = (wl_display*)wl_proxy_create_wrapper(display);
        if (!displayWrapper)
            return false;

        queue = wl_display_create_queue(display);
        if (!queue)
        {
            wl_proxy_wrapper_destroy(displayWrapper);
            displayWrapper = nullptr;
            return false;
        }

        wl_proxy_set_queue((wl_proxy*)displayWrapper, queue);

        registry = wl_display_get_registry(displayWrapper);
        wl_registry_add_listener(registry, &registryListener, nullptr);

        // Roundtrip to discover globals (seat)
        wl_display_roundtrip_queue(display, queue);
        // Second roundtrip to get seat capabilities (keyboard + pointer)
        wl_display_roundtrip_queue(display, queue);

        if (seat)
            Logger::info("Wayland: shared input resources initialized");
        else
            Logger::warn("Wayland: no seat found");

        return seat != nullptr;
    }

    void cleanupWaylandInputCommon()
    {
        if (seat)
        {
            wl_seat_destroy(seat);
            seat = nullptr;
        }
        if (registry)
        {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        if (queue)
        {
            wl_event_queue_destroy(queue);
            queue = nullptr;
        }
        if (displayWrapper)
        {
            wl_proxy_wrapper_destroy(displayWrapper);
            displayWrapper = nullptr;
        }

        keyboardBind = nullptr;
        pointerBind = nullptr;
        commonInitialized = false;
    }

} // namespace vkBasalt
