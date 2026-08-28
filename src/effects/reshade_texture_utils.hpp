#ifndef RESHADE_TEXTURE_UTILS_HPP_INCLUDED
#define RESHADE_TEXTURE_UTILS_HPP_INCLUDED

#include <algorithm>
#include <string_view>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    inline bool hasReshadeTextureAnnotation(const reshadefx::texture& texture,
                                            std::string_view name)
    {
        return std::any_of(texture.annotations.begin(), texture.annotations.end(),
                           [name](const reshadefx::annotation& annotation) {
                               return annotation.name == name;
                           });
    }

    // ReShade shares generated textures with the same unique name across the
    // effects in one runtime. Source-backed textures require additional source
    // validation and remain on their existing upload path.
    inline bool isGeneratedSharedReshadeTexture(const reshadefx::texture& texture)
    {
        return texture.semantic.empty() && !hasReshadeTextureAnnotation(texture, "source");
    }
}

#endif
