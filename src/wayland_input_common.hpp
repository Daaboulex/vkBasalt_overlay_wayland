#pragma once

#include <wayland-client.h>

namespace vkBasalt
{
    // Shared Wayland input state — single wl_seat and event queue used by both
    // keyboard and mouse input backends. Avoids binding two separate seats and
    // creating redundant event queues for the same display.
    //
    // The seat listener is managed here to avoid the wl_seat single-listener
    // constraint. Keyboard and mouse modules register their device listeners
    // via callbacks set before initialization.

    // Callbacks for device binding — set by keyboard/mouse modules before init
    using KeyboardBindCallback = void (*)(wl_seat* seat);
    using PointerBindCallback = void (*)(wl_seat* seat);

    void setKeyboardBindCallback(KeyboardBindCallback cb);
    void setPointerBindCallback(PointerBindCallback cb);

    // Initialize shared Wayland input resources (idempotent).
    // Returns true if the shared seat is ready.
    bool initWaylandInputCommon();

    // Clean up shared resources. Called from cleanupWaylandKeyboard/Mouse.
    void cleanupWaylandInputCommon();

    // Access the shared event queue (for dispatching events)
    wl_event_queue* getWaylandInputQueue();

} // namespace vkBasalt
