#ifndef LIVE_UNIFORM_POLICY_HPP_INCLUDED
#define LIVE_UNIFORM_POLICY_HPP_INCLUDED

namespace vkBasalt
{
    enum class EffectValueApplyMode
    {
        Rebuild,
        LiveUniform,
    };

    inline EffectValueApplyMode effectValueApplyMode(
        bool liveReshadeUniformsEnabled, bool builtInEffect)
    {
        return liveReshadeUniformsEnabled && !builtInEffect
            ? EffectValueApplyMode::LiveUniform
            : EffectValueApplyMode::Rebuild;
    }
} // namespace vkBasalt

#endif // LIVE_UNIFORM_POLICY_HPP_INCLUDED
