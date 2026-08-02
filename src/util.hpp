#ifndef UTIL_HPP_INCLUDED
#define UTIL_HPP_INCLUDED

#include <string>
#include <sstream>
#include <vector>

namespace vkBasalt
{
    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString);

    // True when the unforked vkBasalt is loaded in this process too. Both layers
    // share ENABLE_VKBASALT, so installing both activates both: effects apply
    // twice and neither can be disabled without the other. Probed once.
    bool conflictingLayerLoaded();

    // Full path of the loaded unforked libvkbasalt.so, empty when none; the
    // path names which install (system, container, Proton bundle) brought it.
    const std::string& conflictingLayerPath();

    enum class Color
    {
        defaultColor,

        black,
        red,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        white
    };

    void outputInColor(std::string output, Color foreground = Color::defaultColor, Color background = Color::defaultColor);

    template<typename T>
    std::string convertToString(T object)
    {
        std::stringstream ss;
        ss << object;
        return ss.str();
    }
} // namespace vkBasalt

#endif // UTIL_HPP_INCLUDED
