#pragma once

struct wl_proxy;

namespace vkBasalt
{
    // Register overlay-owned proxies before their add_listener call so the
    // interpose layer passes them through unwrapped.
    void registerOverlayProxy(wl_proxy* proxy);
    void unregisterOverlayProxy(wl_proxy* proxy);

    // Synthetic keyboard leave/enter to wrapped game keyboards, so the game
    // drops held keys when the overlay opens.
    void notifyGameKeyboardFocus(bool hasFocus);

    // True once a game pointer or keyboard listener has been wrapped. Blocking
    // cannot suppress anything until then, so callers must not claim it works.
    bool waylandInterposeActive();
}
