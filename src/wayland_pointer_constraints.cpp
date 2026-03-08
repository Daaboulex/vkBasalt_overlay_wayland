#include "wayland_pointer_constraints.hpp"
#include "wayland_display.hpp"
#include "wayland_input_common.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include "pointer-constraints-unstable-v1-client-protocol.h"

#include <cstring>

namespace vkBasalt
{
    static zwp_pointer_constraints_v1* constraints = nullptr;
    static zwp_confined_pointer_v1* confinedPointer = nullptr;
    static wl_pointer* pointer = nullptr;
    static bool constraintsInitialized = false;

    // Confined pointer listener
    static void confinedPointerConfined(void* /*data*/, zwp_confined_pointer_v1* /*cp*/)
    {
        Logger::debug("Wayland: pointer confined to surface");
    }

    static void confinedPointerUnconfined(void* /*data*/, zwp_confined_pointer_v1* /*cp*/)
    {
        Logger::debug("Wayland: pointer unconfined");
    }

    static const zwp_confined_pointer_v1_listener confinedPointerListener = {
        .confined = confinedPointerConfined,
        .unconfined = confinedPointerUnconfined,
    };

    // Registry listener to bind pointer constraints global
    static void constraintsRegistryGlobal(void* /*data*/, wl_registry* reg,
                                          uint32_t name, const char* interface, uint32_t version)
    {
        if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) != 0)
            return;
        if (constraints)
            return;

        constraints = (zwp_pointer_constraints_v1*)wl_registry_bind(
            reg, name, &zwp_pointer_constraints_v1_interface,
            version < 1 ? version : 1);
        Logger::debug("Wayland: pointer constraints bound");
    }

    static void constraintsRegistryGlobalRemove(void* /*data*/, wl_registry* /*reg*/, uint32_t /*name*/)
    {
    }

    static const wl_registry_listener constraintsRegistryListener = {
        .global = constraintsRegistryGlobal,
        .global_remove = constraintsRegistryGlobalRemove,
    };

    bool initPointerConstraints()
    {
        if (constraintsInitialized)
            return constraints != nullptr;

        wl_display* display = getWaylandDisplay();
        if (!display)
            return false;

        // Ensure shared input is initialized first (creates the event queue)
        if (!initWaylandInputCommon())
            return false;

        constraintsInitialized = true;

        // We need our own registry query on the shared queue to find the
        // pointer constraints global. The shared input common registry
        // only looks for wl_seat.
        wl_display* wrapper = (wl_display*)wl_proxy_create_wrapper(display);
        if (!wrapper)
            return false;

        wl_event_queue* q = getWaylandInputQueue();
        if (!q)
        {
            wl_proxy_wrapper_destroy(wrapper);
            return false;
        }

        wl_proxy_set_queue((wl_proxy*)wrapper, q);

        wl_registry* reg = wl_display_get_registry(wrapper);
        wl_registry_add_listener(reg, &constraintsRegistryListener, nullptr);

        wl_display_roundtrip_queue(display, q);

        wl_registry_destroy(reg);
        wl_proxy_wrapper_destroy(wrapper);

        if (constraints)
            Logger::info("Wayland: pointer constraints initialized");
        else
            Logger::info("Wayland: pointer constraints not available (compositor may not support zwp_pointer_constraints_v1)");

        return constraints != nullptr;
    }

    void confinePointer()
    {
        if (!constraints)
        {
            if (!initPointerConstraints())
                return;
        }

        if (confinedPointer)
        {
            Logger::debug("Wayland: pointer already confined, skipping");
            return;
        }

        wl_surface* surface = getWaylandSurface();
        if (!surface)
        {
            Logger::debug("Wayland: no surface available for pointer confinement");
            return;
        }

        // Get a pointer from the seat for confinement. We get our own
        // because the mouse_input module's pointer is private.
        if (!pointer)
        {
            if (!initWaylandInputCommon())
            {
                Logger::debug("Wayland: cannot confine pointer — no seat available");
                return;
            }

            // The pointer bind callback is already claimed by mouse_input,
            // so we get our own wl_pointer from the seat directly.
            // This is safe because wl_seat_get_pointer returns a new proxy
            // each time — we just won't add a listener (we don't need events).
            wl_seat* seat = getWaylandSeat();
            if (!seat)
            {
                Logger::debug("Wayland: cannot confine pointer — seat not available");
                return;
            }
            pointer = wl_seat_get_pointer(seat);
            if (!pointer)
            {
                Logger::debug("Wayland: cannot confine pointer — seat has no pointer capability");
                return;
            }
            Logger::debug("Wayland: obtained pointer from seat for confinement");
        }

        confinedPointer = zwp_pointer_constraints_v1_confine_pointer(
            constraints, surface, pointer, nullptr,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);

        if (!confinedPointer)
        {
            // The compositor may return NULL if the surface already has a
            // constraint (e.g., the game locked the pointer). This is not fatal.
            Logger::debug("Wayland: confine_pointer returned NULL — surface may already be constrained");
            return;
        }

        zwp_confined_pointer_v1_add_listener(confinedPointer, &confinedPointerListener, nullptr);

        // Flush to apply immediately
        wl_display* display = getWaylandDisplay();
        if (display)
            wl_display_flush(display);

        Logger::debug("Wayland: pointer confinement requested");
    }

    void releasePointer()
    {
        if (!confinedPointer)
            return;

        zwp_confined_pointer_v1_destroy(confinedPointer);
        confinedPointer = nullptr;

        // Flush to apply immediately
        wl_display* display = getWaylandDisplay();
        if (display)
            wl_display_flush(display);

        Logger::debug("Wayland: pointer confinement released");
    }

    void cleanupPointerConstraints()
    {
        releasePointer();

        if (pointer)
        {
            wl_pointer_destroy(pointer);
            pointer = nullptr;
        }

        if (constraints)
        {
            zwp_pointer_constraints_v1_destroy(constraints);
            constraints = nullptr;
        }

        constraintsInitialized = false;
    }

} // namespace vkBasalt
