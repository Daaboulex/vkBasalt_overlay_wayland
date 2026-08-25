#include "effects/reshade_pass_utils.hpp"

#include <cstdio>

using namespace vkBasalt;

int main()
{
    reshadefx::pass compute;
    compute.cs_entry_point = "Compute";

    reshadefx::pass backbuffer;
    backbuffer.vs_entry_point = "VS";
    backbuffer.ps_entry_point = "PS";

    reshadefx::pass auxiliary = backbuffer;
    auxiliary.render_target_names[0] = "AuxiliaryTexture";

    reshadefx::technique technique;
    technique.passes = {compute, compute, compute, compute, compute, backbuffer, auxiliary};

    if (writesReshadeBackBuffer(compute))
    {
        std::puts("compute pass was incorrectly counted as a backbuffer write");
        return 1;
    }
    if (!writesReshadeBackBuffer(backbuffer))
    {
        std::puts("graphics backbuffer pass was not counted");
        return 1;
    }
    if (writesReshadeBackBuffer(auxiliary))
    {
        std::puts("auxiliary render-target pass was incorrectly counted as a backbuffer write");
        return 1;
    }
    if (countReshadeBackBufferWrites(technique) != 1)
    {
        std::puts("mixed technique did not report exactly one backbuffer write");
        return 1;
    }

    std::puts("ReShade pass accounting: all assertions passed");
    return 0;
}
