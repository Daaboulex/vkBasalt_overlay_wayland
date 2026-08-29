#include "reshade_uniform_storage.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using namespace vkBasalt;

    std::array<uint32_t, 32> storage{};
    reshadefx::uniform value = {};
    value.offset = 8;
    value.size = 12;
    value.type.base = reshadefx::type::t_float;
    value.type.rows = 3;
    value.type.cols = 1;
    const uint32_t vectorWords[3] = {11, 22, 33};
    assert(writeReshadeUniformWords(
        storage.data(), sizeof(storage), value, vectorWords, 3));
    assert(storage[2] == 11 && storage[3] == 22 && storage[4] == 33);

    storage.fill(0);
    value.offset = 0;
    value.size = 32;
    value.type.rows = 2;
    value.type.cols = 1;
    value.type.array_length = 2;
    const uint32_t arrayWords[2] = {7, 9};
    assert(writeReshadeUniformWords(
        storage.data(), sizeof(storage), value, arrayWords, 2, 1));
    assert(storage[4] == 7 && storage[5] == 9);

    storage.fill(0);
    value.offset = 0;
    value.size = 32;
    value.type.rows = 2;
    value.type.cols = 2;
    value.type.array_length = 0;
    const uint32_t matrixWords[4] = {1, 2, 3, 4};
    assert(writeReshadeUniformWords(
        storage.data(), sizeof(storage), value, matrixWords, 4));
    assert(storage[0] == 1 && storage[1] == 2);
    assert(storage[4] == 3 && storage[5] == 4);

    assert(!writeReshadeUniformWords(
        storage.data(), 4, value, matrixWords, 4));
    value.size = 4;
    assert(!writeReshadeUniformWords(
        storage.data(), sizeof(storage), value, matrixWords, 4));
    return 0;
}
