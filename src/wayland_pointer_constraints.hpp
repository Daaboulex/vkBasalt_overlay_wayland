#pragma once

namespace vkBasalt
{
    // Initialize pointer constraints from the Wayland registry.
    // Requires initWaylandInputCommon() to have been called first.
    bool initPointerConstraints();

    // Confine the pointer to the game's wl_surface (entire surface).
    // No-op if already confined or if constraints aren't available.
    void confinePointer();

    // Release the pointer confinement.
    // No-op if not currently confined.
    void releasePointer();

    // Clean up pointer constraints resources.
    void cleanupPointerConstraints();

} // namespace vkBasalt
