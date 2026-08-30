#include "live_uniform_policy.hpp"

int main()
{
    using namespace vkBasalt;

    if (effectValueApplyMode(false, false) != EffectValueApplyMode::Rebuild)
        return 1;
    if (effectValueApplyMode(false, true) != EffectValueApplyMode::Rebuild)
        return 1;
    if (effectValueApplyMode(true, true) != EffectValueApplyMode::Rebuild)
        return 1;
    if (effectValueApplyMode(true, false) != EffectValueApplyMode::LiveUniform)
        return 1;
    return 0;
}
