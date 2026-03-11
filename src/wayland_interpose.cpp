// Wayland event interposition for input blocking.
//
// On Wayland, there is no XGrabPointer equivalent. The game and our overlay
// both receive pointer/keyboard events from the compositor for the same
// surface. This module interposes on wl_proxy_add_listener (the underlying
// C function that wl_pointer_add_listener inlines to) and wraps the game's
// pointer/keyboard listeners with callbacks that suppress events when the
// overlay has input blocked.
//
// Overlay-owned proxies are registered via registerOverlayProxy() and are
// always passed through unwrapped.

#include "wayland_interpose.hpp"
#include "input_blocker.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

// Forward-declare to avoid pulling in mouse_input_wayland.hpp (which
// transitively includes mouse_input.hpp → X11 headers in some targets).
namespace vkBasalt { void mirrorButtonState(uint32_t button, bool pressed); }

#include <wayland-client.h>
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace vkBasalt
{
    static std::mutex proxyMutex;
    static std::unordered_set<wl_proxy*> overlayProxies;

    void registerOverlayProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(proxyMutex);
        overlayProxies.insert(proxy);
    }

    void unregisterOverlayProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(proxyMutex);
        overlayProxies.erase(proxy);
    }

    static bool isOverlayProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(proxyMutex);
        return overlayProxies.count(proxy) > 0;
    }
} // namespace vkBasalt

// ── Game listener storage ────────────────────────────────────────────────────

// Per-proxy data for wrapped game listeners. We store the game's original
// listener and user_data so we can forward events when not blocking.
struct GamePointerData
{
    wl_pointer_listener original;
    void* userData;
};

struct GameKeyboardData
{
    wl_keyboard_listener original;
    void* userData;
};

static std::mutex gameDataMutex;
static std::unordered_map<wl_pointer*, GamePointerData> gamePointers;
static std::unordered_map<wl_keyboard*, GameKeyboardData> gameKeyboards;

// ── Wrapper pointer listener ─────────────────────────────────────────────────

static void wp_enter(void* data, wl_pointer* p, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.enter)
        it->second.original.enter(it->second.userData, p, serial, s, x, y);
}

static void wp_leave(void* data, wl_pointer* p, uint32_t serial, wl_surface* s)
{
    // Always forward leave — the game needs to know it lost focus
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.leave)
        it->second.original.leave(it->second.userData, p, serial, s);
}

static void wp_motion(void* data, wl_pointer* p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.motion)
        it->second.original.motion(it->second.userData, p, time, x, y);
}

static void wp_button(void* data, wl_pointer* p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    // Always mirror button state to overlay — the game's pointer receives
    // releases via Wayland's implicit grab that our overlay pointer never sees.
    if (vkBasalt::isWayland())
        vkBasalt::mirrorButtonState(button, state == WL_POINTER_BUTTON_STATE_PRESSED);

    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.button)
        it->second.original.button(it->second.userData, p, serial, time, button, state);
}

static void wp_axis(void* data, wl_pointer* p, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis)
        it->second.original.axis(it->second.userData, p, time, axis, value);
}

static void wp_frame(void* data, wl_pointer* p)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.frame)
        it->second.original.frame(it->second.userData, p);
}

static void wp_axis_source(void* data, wl_pointer* p, uint32_t source)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis_source)
        it->second.original.axis_source(it->second.userData, p, source);
}

static void wp_axis_stop(void* data, wl_pointer* p, uint32_t time, uint32_t axis)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis_stop)
        it->second.original.axis_stop(it->second.userData, p, time, axis);
}

static void wp_axis_discrete(void* data, wl_pointer* p, uint32_t axis, int32_t discrete)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis_discrete)
        it->second.original.axis_discrete(it->second.userData, p, axis, discrete);
}

static void wp_axis_value120(void* data, wl_pointer* p, uint32_t axis, int32_t value120)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis_value120)
        it->second.original.axis_value120(it->second.userData, p, axis, value120);
}

static void wp_axis_relative_direction(void* data, wl_pointer* p, uint32_t axis, uint32_t direction)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gamePointers.find(p);
    if (it != gamePointers.end() && it->second.original.axis_relative_direction)
        it->second.original.axis_relative_direction(it->second.userData, p, axis, direction);
}

static const wl_pointer_listener wrapperPointerListener = {
    .enter = wp_enter,
    .leave = wp_leave,
    .motion = wp_motion,
    .button = wp_button,
    .axis = wp_axis,
    .frame = wp_frame,
    .axis_source = wp_axis_source,
    .axis_stop = wp_axis_stop,
    .axis_discrete = wp_axis_discrete,
    .axis_value120 = wp_axis_value120,
    .axis_relative_direction = wp_axis_relative_direction,
};

// ── Wrapper keyboard listener ────────────────────────────────────────────────

static void wk_keymap(void* data, wl_keyboard* kb, uint32_t format, int32_t fd, uint32_t size)
{
    // Always forward keymap — needed for initialization
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.keymap)
        it->second.original.keymap(it->second.userData, kb, format, fd, size);
}

static void wk_enter(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* s, wl_array* keys)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.enter)
        it->second.original.enter(it->second.userData, kb, serial, s, keys);
}

static void wk_leave(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* s)
{
    // Always forward leave
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.leave)
        it->second.original.leave(it->second.userData, kb, serial, s);
}

static void wk_key(void* data, wl_keyboard* kb, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    if (vkBasalt::isInputBlocked())
        return;
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.key)
        it->second.original.key(it->second.userData, kb, serial, time, key, state);
}

static void wk_modifiers(void* data, wl_keyboard* kb, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    // Always forward modifiers — games need modifier state for keybinds
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.modifiers)
        it->second.original.modifiers(it->second.userData, kb, serial, depressed, latched, locked, group);
}

static void wk_repeat_info(void* data, wl_keyboard* kb, int32_t rate, int32_t delay)
{
    // Always forward repeat info
    std::lock_guard<std::mutex> lock(gameDataMutex);
    auto it = gameKeyboards.find(kb);
    if (it != gameKeyboards.end() && it->second.original.repeat_info)
        it->second.original.repeat_info(it->second.userData, kb, rate, delay);
}

static const wl_keyboard_listener wrapperKeyboardListener = {
    .keymap = wk_keymap,
    .enter = wk_enter,
    .leave = wk_leave,
    .key = wk_key,
    .modifiers = wk_modifiers,
    .repeat_info = wk_repeat_info,
};

// ── Symbol interposition ─────────────────────────────────────────────────────
//
// wl_pointer_add_listener is a static inline in <wayland-client-protocol.h>
// that calls wl_proxy_add_listener. We interpose on the underlying C function
// so that even inlined calls are caught.

using AddListenerFn = int (*)(struct wl_proxy*, void (**)(void), void*);

static AddListenerFn getRealAddListener()
{
    static AddListenerFn fn = (AddListenerFn)dlsym(RTLD_NEXT, "wl_proxy_add_listener");
    return fn;
}

extern "C" __attribute__((visibility("default")))
int wl_proxy_add_listener(struct wl_proxy* proxy,
                          void (**implementation)(void),
                          void* data)
{
    AddListenerFn real = getRealAddListener();
    if (!real)
        return -1;

    const char* cls = wl_proxy_get_class(proxy);
    if (!cls)
        return real(proxy, implementation, data);

    // Skip overlay-owned proxies — they use their own listeners
    if (vkBasalt::isOverlayProxy(proxy))
        return real(proxy, implementation, data);

    if (std::strcmp(cls, "wl_pointer") == 0)
    {
        auto* gameListener = (const wl_pointer_listener*)implementation;
        auto* ptr = (wl_pointer*)proxy;

        {
            std::lock_guard<std::mutex> lock(gameDataMutex);
            GamePointerData gpd;
            gpd.original = *gameListener;
            gpd.userData = data;
            gamePointers[ptr] = gpd;
        }

        vkBasalt::Logger::debug("Wayland interpose: wrapped game pointer listener");
        return real(proxy, (void (**)(void))&wrapperPointerListener, nullptr);
    }

    if (std::strcmp(cls, "wl_keyboard") == 0)
    {
        auto* gameListener = (const wl_keyboard_listener*)implementation;
        auto* kb = (wl_keyboard*)proxy;

        {
            std::lock_guard<std::mutex> lock(gameDataMutex);
            GameKeyboardData gkd;
            gkd.original = *gameListener;
            gkd.userData = data;
            gameKeyboards[kb] = gkd;
        }

        vkBasalt::Logger::debug("Wayland interpose: wrapped game keyboard listener");
        return real(proxy, (void (**)(void))&wrapperKeyboardListener, nullptr);
    }

    return real(proxy, implementation, data);
}

namespace vkBasalt
{
    void notifyGameKeyboardFocus(bool hasFocus)
    {
        std::lock_guard<std::mutex> lock(gameDataMutex);
        if (gameKeyboards.empty())
            return;

        for (auto& [kb, data] : gameKeyboards)
        {
            if (hasFocus)
            {
                // Synthetic enter — game regains keyboard focus with no pressed keys
                if (data.original.enter)
                {
                    wl_array emptyKeys;
                    wl_array_init(&emptyKeys);
                    data.original.enter(data.userData, kb, 0, nullptr, &emptyKeys);
                    wl_array_release(&emptyKeys);
                }
            }
            else
            {
                // Synthetic leave — game drops all held keys
                if (data.original.leave)
                    data.original.leave(data.userData, kb, 0, nullptr);
            }
        }

        Logger::debug(std::string("Wayland interpose: synthetic keyboard ") +
                       (hasFocus ? "enter" : "leave") + " sent to " +
                       std::to_string(gameKeyboards.size()) + " game keyboard(s)");
    }
} // namespace vkBasalt
