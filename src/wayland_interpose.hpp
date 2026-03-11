#pragma once

struct wl_proxy;

namespace vkBasalt
{
    // Register overlay-owned proxies so the interposition layer skips them.
    // Call BEFORE wl_pointer_add_listener / wl_keyboard_add_listener.
    void registerOverlayProxy(wl_proxy* proxy);
    void unregisterOverlayProxy(wl_proxy* proxy);

    // Send synthetic keyboard leave/enter to wrapped game keyboards.
    // Called when overlay blocking state changes — makes the game release
    // all held keys (leave) or re-acquire focus (enter).
    // Only works when wl_proxy_add_listener interposition is active.
    void notifyGameKeyboardFocus(bool hasFocus);
}
