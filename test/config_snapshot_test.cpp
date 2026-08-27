#include "config.hpp"
#include "logger.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace vkBasalt
{
    Logger Logger::s_instance;
}

int main()
{
    using namespace vkBasalt;

    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("vkbasalt-config-snapshot-" + std::to_string(getpid()));
    const std::filesystem::path basePath = root.string() + "-base.conf";
    const std::filesystem::path profilePath = root.string() + "-profile.conf";
    {
        std::ofstream base(basePath);
        base << "baseValue = base\n";
        std::ofstream profile(profilePath);
        profile << "profileValue = profile\n";
    }

    Config base(basePath.string());
    Config live(profilePath.string());
    live.setFallback(&base);
    live.setOverride("stagedValue", "old");

    const uint64_t capturedRevision = live.getBuildStateRevision();
    const std::string capturedSignature = live.getBuildStateSignature();
    Config snapshot(live);
    live.setOverride("stagedValue", "new");
    live.clearOverrides();
    base.setOverride("baseValue", "changed");

    const bool valid = snapshot.getBuildStateRevision() == capturedRevision
        && snapshot.getBuildStateSignature() == capturedSignature
        && snapshot.getOption<std::string>("profileValue") == "profile"
        && snapshot.getOption<std::string>("baseValue") == "base"
        && snapshot.getOption<std::string>("stagedValue") == "old"
        && live.getOption<std::string>("stagedValue", "missing") == "missing"
        && live.getOption<std::string>("baseValue") == "changed"
        && live.getBuildStateRevision() > capturedRevision
        && live.getBuildStateSignature() != capturedSignature;

    std::remove(basePath.c_str());
    std::remove(profilePath.c_str());
    if (!valid)
        return 1;

    std::puts("Config snapshot and fallback ownership: all assertions passed");
    return 0;
}
