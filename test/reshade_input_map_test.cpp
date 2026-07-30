#include "../src/reshade_input_map.hpp"

#include <cstdio>

using namespace vkBasalt;

static int failures = 0;

static void expectKey(int vk, uint32_t want, const char* what)
{
    uint32_t got = keysymForVirtualKey(vk);
    if (got != want)
    {
        std::printf("FAIL %s: virtual-key 0x%02X gave keysym 0x%04X, wanted 0x%04X\n", what, vk, got, want);
        failures++;
    }
}

static void expectMode(bool got, bool want, const char* what)
{
    if (got != want)
    {
        std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false", want ? "true" : "false");
        failures++;
    }
}

int main()
{
    expectKey(0x41, 0x061, "A maps to lowercase a, not uppercase");
    expectKey(0x5A, 0x07a, "Z is the end of the letter range");
    expectKey(0x30, 0x030, "digit 0 is its own ASCII value");
    expectKey(0x39, 0x039, "digit 9 is its own ASCII value");
    expectKey(0x70, 0xffbe, "F1 is the base of the function-key range");
    expectKey(0x7B, 0xffc9, "F12 is eleven above F1");
    expectKey(0x20, 0x020, "space");
    expectKey(0x1B, 0xff1b, "escape");
    expectKey(0x25, 0xff51, "left arrow");
    expectKey(0x28, 0xff54, "down arrow");
    expectKey(0x2E, 0xffff, "delete");
    expectKey(0x00, 0, "an unmapped code reports unmapped");
    expectKey(0xFE, 0, "an out-of-range code reports unmapped");

    // A held key reads down for as long as it is down.
    {
        bool was = false, tog = false;
        expectMode(applyKeyMode(KeyMode::held, true, was, tog), true, "held down");
        expectMode(applyKeyMode(KeyMode::held, true, was, tog), true, "held stays down");
        expectMode(applyKeyMode(KeyMode::held, false, was, tog), false, "held released");
    }

    // press fires once, on the transition only.
    {
        bool was = false, tog = false;
        expectMode(applyKeyMode(KeyMode::press, true, was, tog), true, "press fires on the down edge");
        expectMode(applyKeyMode(KeyMode::press, true, was, tog), false, "press does not repeat while held");
        expectMode(applyKeyMode(KeyMode::press, false, was, tog), false, "press is silent on release");
        expectMode(applyKeyMode(KeyMode::press, true, was, tog), true, "press fires again on the next down edge");
    }

    // toggle flips on each down edge and holds its value between them.
    {
        bool was = false, tog = false;
        expectMode(applyKeyMode(KeyMode::toggle, true, was, tog), true, "toggle turns on");
        expectMode(applyKeyMode(KeyMode::toggle, true, was, tog), true, "toggle holds while still held");
        expectMode(applyKeyMode(KeyMode::toggle, false, was, tog), true, "toggle survives release");
        expectMode(applyKeyMode(KeyMode::toggle, true, was, tog), false, "toggle turns off on the next press");
        expectMode(applyKeyMode(KeyMode::toggle, false, was, tog), false, "toggle stays off after release");
    }

    if (failures)
    {
        std::printf("%d assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("reshade input map: all assertions passed\n");
    return 0;
}
