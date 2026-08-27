#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "depth_identity.hpp"

using namespace vkBasalt;

template<typename Handle>
Handle fakeHandle(uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(value);
    else
        return static_cast<Handle>(value);
}

int main()
{
    const VkImage reusedBits = fakeHandle<VkImage>(0x1234);
    const DepthIdentity firstLifetime{reusedBits, 41};
    const DepthIdentity secondLifetime{reusedBits, 42};

    if (!firstLifetime.valid()
        || firstLifetime == secondLifetime
        || !destroyingDepthIdentityInvalidatesBinding(
            firstLifetime, firstLifetime)
        || destroyingDepthIdentityInvalidatesBinding(
            firstLifetime, secondLifetime)
        || destroyingDepthIdentityInvalidatesBinding(firstLifetime, {}))
        return 1;

    DepthImage image;
    image.image = reusedBits;
    image.creationSerial = 41;
    if (image.identity() != firstLifetime)
        return 1;

    std::puts("Depth allocation identity distinguishes raw-handle reuse");
    return 0;
}
