#pragma once

#include <wayland-client.h>

struct zwp_relative_pointer_manager_v1;

namespace vkBasalt
{
    // One wl_seat and one event queue shared by the keyboard and mouse backends;
    // wl_seat allows only a single listener, so device binding goes through
    // callbacks registered here.

    using KeyboardBindCallback = void (*)(wl_seat* seat);
    using PointerBindCallback = void (*)(wl_seat* seat);

    void setKeyboardBindCallback(KeyboardBindCallback cb);
    void setPointerBindCallback(PointerBindCallback cb);

    bool initWaylandInputCommon();

    void cleanupWaylandInputCommon();

    wl_event_queue* getWaylandInputQueue();

    wl_seat* getWaylandSeat();

    zwp_relative_pointer_manager_v1* getWaylandRelativePointerManager();

    // Resets the per-frame dispatch deduplication.
    void beginWaylandInputFrame();

    // Non-blocking read + dispatch; only the first call per frame does real work.
    void dispatchWaylandInputEvents();

} // namespace vkBasalt
