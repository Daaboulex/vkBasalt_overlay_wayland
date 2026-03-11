// LD_AUDIT library for Wine Wayland input interposition.
//
// Problem: Wine loads winewayland.so via dlopen(RTLD_LOCAL), which creates
// a private symbol scope. libwayland-client.so's wl_proxy_add_listener
// resolves within that local scope, bypassing our LD_PRELOAD / Vulkan-layer
// interposition entirely.
//
// Solution: The rtld-audit interface (rtld-audit(7)) intercepts symbol
// resolution even for RTLD_LOCAL libraries. This audit library:
//   1. Detects when libvkbasalt-overlay.so is loaded (la_objopen)
//   2. Resolves our wl_proxy_add_listener wrapper address from it
//   3. Redirects all other bindings of wl_proxy_add_listener to our wrapper
//      (la_symbind64), so Wine's Wayland code hits our interposition layer
//
// Usage: LD_AUDIT=/path/to/libvkbasalt-audit.so <game>
//
// Performance: la_objopen returns 0 for unrelated libraries, so la_symbind64
// is only called for wayland/vkbasalt/wine-related objects. Overhead for
// non-Wayland games is negligible.

#define _GNU_SOURCE
#include <link.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

// Cookie for our main library — set when la_objopen sees it
static uintptr_t vkbasalt_cookie = 0;

// Address of our wl_proxy_add_listener wrapper in libvkbasalt-overlay.so
static uintptr_t wrapper_addr = 0;

unsigned int
la_version(unsigned int version)
{
    return LAV_CURRENT;
}

unsigned int
la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie)
{
    const char *name = map->l_name;
    if (!name || name[0] == '\0')
        return LA_FLG_BINDTO | LA_FLG_BINDFROM; // main executable

    // When our Vulkan layer is loaded, grab a handle and resolve the wrapper
    if (strstr(name, "libvkbasalt-overlay.so"))
    {
        vkbasalt_cookie = *cookie;

        void *handle = dlopen(name, RTLD_NOLOAD | RTLD_NOW);
        if (handle)
        {
            void *sym = dlsym(handle, "wl_proxy_add_listener");
            if (sym)
                wrapper_addr = (uintptr_t)sym;
            dlclose(handle);
        }

        return LA_FLG_BINDTO | LA_FLG_BINDFROM;
    }

    // Only audit libraries that might bind wl_proxy_add_listener:
    // wayland-client (defines it), wine modules (call it), game code
    if (strstr(name, "wayland") ||
        strstr(name, "wine") ||
        strstr(name, "proton"))
        return LA_FLG_BINDTO | LA_FLG_BINDFROM;

    // Skip everything else — no auditing overhead for unrelated libraries
    return 0;
}

#ifdef __LP64__
uintptr_t
la_symbind64(Elf64_Sym *sym, unsigned int ndx,
             uintptr_t *refcook, uintptr_t *defcook,
             unsigned int *flags, const char *symname)
{
    if (!wrapper_addr)
        return sym->st_value;

    if (strcmp(symname, "wl_proxy_add_listener") != 0)
        return sym->st_value;

    // Don't redirect bindings originating from or defined by our library
    if (*refcook == vkbasalt_cookie || *defcook == vkbasalt_cookie)
        return sym->st_value;

    // Redirect to our interposition wrapper
    return wrapper_addr;
}
#else
uintptr_t
la_symbind32(Elf32_Sym *sym, unsigned int ndx,
             uintptr_t *refcook, uintptr_t *defcook,
             unsigned int *flags, const char *symname)
{
    if (!wrapper_addr)
        return sym->st_value;

    if (strcmp(symname, "wl_proxy_add_listener") != 0)
        return sym->st_value;

    if (*refcook == vkbasalt_cookie || *defcook == vkbasalt_cookie)
        return sym->st_value;

    return wrapper_addr;
}
#endif
