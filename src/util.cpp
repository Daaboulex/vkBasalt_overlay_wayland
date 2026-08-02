#include "util.hpp"

#include <cstring>
#include <iostream>
#include <unistd.h>
#include <link.h>

namespace vkBasalt
{
    const std::string& conflictingLayerPath()
    {
        // A negative is never cached: the other layer can be dlopened after this
        // one, so the answer stays live until it turns positive.
        static std::string cached;
        if (cached.empty())
        {
            dl_iterate_phdr(
                [](struct dl_phdr_info* info, size_t, void* out) {
                    if (!info->dlpi_name || !*info->dlpi_name)
                        return 0;
                    const char* base = std::strrchr(info->dlpi_name, '/');
                    base = base ? base + 1 : info->dlpi_name;
                    if (std::strcmp(base, "libvkbasalt.so") == 0 || std::strncmp(base, "libvkbasalt.so.", 15) == 0)
                    {
                        *static_cast<std::string*>(out) = info->dlpi_name;
                        return 1;
                    }
                    return 0;
                },
                &cached);
        }
        return cached;
    }

    bool conflictingLayerLoaded()
    {
        return !conflictingLayerPath().empty();
    }

    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString)
    {
        for (const char* other : stringVector)
        {
            if (other == std::string(addString))
            {
                return;
            }
        }
        stringVector.push_back(addString);
    }

    void outputInColor(std::string output, Color foreground, Color background)
    {
        std::vector<std::string> magicNumbers;
        switch (foreground)
        {
            case Color::black: magicNumbers.push_back("30"); break;
            case Color::red: magicNumbers.push_back("31"); break;
            case Color::green: magicNumbers.push_back("32"); break;
            case Color::yellow: magicNumbers.push_back("33"); break;
            case Color::blue: magicNumbers.push_back("34"); break;
            case Color::magenta: magicNumbers.push_back("35"); break;
            case Color::cyan: magicNumbers.push_back("36"); break;
            case Color::white: magicNumbers.push_back("37"); break;
            default: break;
        }
        switch (background)
        {
            case Color::black: magicNumbers.push_back("40"); break;
            case Color::red: magicNumbers.push_back("41"); break;
            case Color::green: magicNumbers.push_back("42"); break;
            case Color::yellow: magicNumbers.push_back("43"); break;
            case Color::blue: magicNumbers.push_back("44"); break;
            case Color::magenta: magicNumbers.push_back("45"); break;
            case Color::cyan: magicNumbers.push_back("46"); break;
            case Color::white: magicNumbers.push_back("47"); break;
            default: break;
        }
        std::string magicString = "";
        for (bool first = true; auto& magicNumber : magicNumbers)
        {
            if (!first)
            {
                magicString += ";";
            }
            magicString += magicNumber;
            first = false;
        }
        if (magicString.size() == 0 || !isatty(fileno(stdout)))
        {
            std::cout << output << std::endl;
        }
        else
        {
            std::cout << "\033[" << magicString << "m" << output << "\033[0m" << std::endl;
        }
    }
} // namespace vkBasalt
