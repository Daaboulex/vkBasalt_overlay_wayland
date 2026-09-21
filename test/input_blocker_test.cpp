#include "input_blocker.hpp"
#include "logger.hpp"
#include "wayland_display.hpp"
#include "wayland_interpose.hpp"

#include <cstdio>
#include <vector>

namespace vkBasalt
{
    Logger Logger::s_instance;

    static bool              interposeActive = true;
    static std::vector<bool> gameFocusNotices;

    bool isWayland()
    {
        return true;
    }

    bool waylandInterposeActive()
    {
        return interposeActive;
    }

    void notifyGameKeyboardFocus(bool hasFocus)
    {
        gameFocusNotices.push_back(hasFocus);
    }

    void* getKeyboardDisplay()
    {
        return nullptr;
    }
}

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    using namespace vkBasalt;

    initInputBlocker(true);
    setInputBlocked(true);
    expect(isInputBlocked(), "an open overlay blocks the game's events");
    expect(gameFocusNotices == std::vector<bool>{false}, "the game is told it lost focus when blocking starts");

    initInputBlocker(false);
    expect(!isInputBlocked(), "disabling blocking while the overlay is open releases the block it holds");
    expect(gameFocusNotices == std::vector<bool>{false, true}, "the game gets its focus back when the block is released");

    setInputBlocked(false);
    expect(!isInputBlocked(), "closing the overlay afterwards finds nothing left to release");
    expect(gameFocusNotices.size() == 2, "a release with nothing blocked sends the game no second focus notice");

    setInputBlocked(true);
    expect(!isInputBlocked(), "with blocking disabled the overlay does not block");

    initInputBlocker(true);
    setInputBlocked(true);
    setInputBlocked(false);
    expect(!isInputBlocked(), "the toggle path releases what it blocked");
    expect(gameFocusNotices == std::vector<bool>{false, true, false, true}, "every block is paired with one release notice");

    interposeActive = false;
    setInputBlocked(true);
    expect(!isInputBlocked(), "blocking is refused while no game listener is wrapped");
    expect(gameFocusNotices.size() == 4, "a refused block tells the game nothing");

    if (failures != 0)
    {
        std::printf("%d expectation(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the block is released on every path that ends it\n");
    return 0;
}
