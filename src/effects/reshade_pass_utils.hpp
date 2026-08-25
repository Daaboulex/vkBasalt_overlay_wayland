#ifndef RESHADE_PASS_UTILS_HPP_INCLUDED
#define RESHADE_PASS_UTILS_HPP_INCLUDED

#include <cstddef>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    inline bool writesReshadeBackBuffer(const reshadefx::pass& pass)
    {
        return pass.cs_entry_point.empty() && pass.render_target_names[0].empty();
    }

    inline std::size_t countReshadeBackBufferWrites(const reshadefx::technique& technique)
    {
        std::size_t count = 0;
        for (const auto& pass : technique.passes)
        {
            if (writesReshadeBackBuffer(pass))
                ++count;
        }
        return count;
    }
}

#endif
