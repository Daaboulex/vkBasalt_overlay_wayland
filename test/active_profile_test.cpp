#include "config_serializer.hpp"
#include "logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace vkBasalt
{
    Logger Logger::s_instance;
}

using namespace vkBasalt;
namespace fs = std::filesystem;

namespace
{
    std::string readFile(const fs::path& path)
    {
        std::ifstream      file(path);
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;

    const fs::path configHome = argv[1];
    fs::remove_all(configHome);
    const fs::path configs = configHome / "vkBasalt-overlay" / "configs";
    fs::create_directories(configs);
    setenv("XDG_CONFIG_HOME", configHome.c_str(), 1);

    std::ofstream(configs / ".active_profiles") << "alacritty=none\n"
                                                   "wine-preloader=default\n"
                                                   "vkcube=None\n"
                                                   "mygame=perf\n";
    std::ofstream(configs / "alacritty@none.conf") << "effects = cas\n";
    std::ofstream(configs / "alacritty.conf") << "";
    std::ofstream(configs / "wine-preloader.conf") << "";

    const ActiveProfile bypassed = ConfigSerializer::getActiveProfile("alacritty");
    if (bypassed.kind != ProfileKind::Bypass || bypassed.name != "none")
        return 1;

    const ActiveProfile defaulted = ConfigSerializer::getActiveProfile("wine-preloader");
    if (defaulted.kind != ProfileKind::Default || defaulted.name != "default")
        return 1;

    const ActiveProfile absent = ConfigSerializer::getActiveProfile("gamescope");
    if (absent.kind != ProfileKind::Default || absent.name != "default")
        return 1;

    const ActiveProfile capitalised = ConfigSerializer::getActiveProfile("vkcube");
    if (capitalised.kind != ProfileKind::Named || capitalised.name != "None")
        return 1;

    const ActiveProfile named = ConfigSerializer::getActiveProfile("mygame");
    if (named.kind != ProfileKind::Named || named.name != "perf")
        return 1;

    const std::vector<std::string> defaultAndBypass{"default", "none"};
    if (ConfigSerializer::listProfilesForGame("alacritty") != defaultAndBypass)
        return 1;
    if (ConfigSerializer::listProfilesForGame("wine-preloader") != defaultAndBypass)
        return 1;

    if (ConfigSerializer::createProfile("alacritty", "none"))
        return 1;
    if (ConfigSerializer::deleteProfile("alacritty", "none"))
        return 1;
    if (readFile(configs / "alacritty@none.conf") != "effects = cas\n")
        return 1;

    if (!ConfigSerializer::createProfile("alacritty", "fresh", "none"))
        return 1;
    if (readFile(configs / "alacritty@fresh.conf").find("cas") != std::string::npos)
        return 1;

    ConfigSerializer::setActiveProfile("alacritty", "fresh");
    if (ConfigSerializer::getActiveProfile("alacritty").kind != ProfileKind::Named)
        return 1;
    if (ConfigSerializer::getActiveProfile("wine-preloader").kind != ProfileKind::Default)
        return 1;

    return 0;
}
