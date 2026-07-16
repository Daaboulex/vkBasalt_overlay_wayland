// rtld-audit(7) library: Wine loads winewayland.so via dlopen(RTLD_LOCAL),
// whose private symbol scope bypasses the wl_proxy_add_listener interpose in
// libvkbasalt-overlay.so. Audit symbol binding and redirect other objects'
// wl_proxy_add_listener bindings to that wrapper.
// Usage: LD_AUDIT=/path/to/libvkbasalt-audit.so <game>

#define _GNU_SOURCE
#include <link.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

static uintptr_t vkbasalt_cookie = 0;

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

    // Audit only objects that could bind wl_proxy_add_listener; returning 0
    // keeps la_symbind out of every other library's bindings.
    if (strstr(name, "wayland") ||
        strstr(name, "wine") ||
        strstr(name, "proton"))
        return LA_FLG_BINDTO | LA_FLG_BINDFROM;

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

    if (*refcook == vkbasalt_cookie || *defcook == vkbasalt_cookie)
        return sym->st_value;

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
