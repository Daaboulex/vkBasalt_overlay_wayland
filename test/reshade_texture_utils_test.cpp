#include <cstdio>

#include "effects/reshade_texture_utils.hpp"

using namespace vkBasalt;

int main()
{
    reshadefx::texture frameworkTexture;
    frameworkTexture.unique_name = "V__SharedFramework__Albedo";
    if (!isGeneratedSharedReshadeTexture(frameworkTexture))
        return 1;

    reshadefx::texture colorReference;
    colorReference.semantic = "COLOR";
    if (isGeneratedSharedReshadeTexture(colorReference))
        return 1;

    reshadefx::texture depthReference;
    depthReference.semantic = "DEPTH";
    if (isGeneratedSharedReshadeTexture(depthReference))
        return 1;

    reshadefx::texture sourceTexture;
    reshadefx::annotation source;
    source.name = "source";
    sourceTexture.annotations.push_back(source);
    if (isGeneratedSharedReshadeTexture(sourceTexture))
        return 1;

    std::puts("ReShade generated texture sharing: all assertions passed");
    return 0;
}
