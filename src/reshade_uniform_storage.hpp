#ifndef RESHADE_UNIFORM_STORAGE_HPP_INCLUDED
#define RESHADE_UNIFORM_STORAGE_HPP_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "reshade/effect_module.hpp"

namespace vkBasalt
{
    // Writes tightly packed 32-bit scalar words into ReShade's std140-like
    // uniform layout (16-byte array elements and matrix rows).
    inline bool writeReshadeUniformWords(
        void* mappedBuffer, size_t bufferSize,
        const reshadefx::uniform& variable,
        const uint32_t* words, size_t wordCount,
        size_t arrayIndex = 0)
    {
        if (mappedBuffer == nullptr || words == nullptr
            || variable.offset > bufferSize
            || variable.size > bufferSize - variable.offset)
            return false;

        uint8_t* const destination =
            static_cast<uint8_t*>(mappedBuffer) + variable.offset;
        const auto writeWord = [&](size_t destinationWord,
                                   const uint32_t* source) {
            const size_t destinationByte = destinationWord * 4;
            if (destinationByte > variable.size
                || sizeof(uint32_t) > variable.size - destinationByte)
                return false;
            std::memcpy(destination + destinationByte, source,
                        sizeof(uint32_t));
            return true;
        };
        const size_t arrayLength = variable.type.is_array()
            ? variable.type.array_length : 1u;
        if (arrayIndex >= arrayLength)
            return false;

        const size_t componentCount = variable.type.components();
        wordCount = std::min(wordCount, componentCount);
        if (variable.type.is_matrix())
        {
            size_t source = 0;
            for (size_t row = 0;
                 row < variable.type.rows && source < wordCount; ++row)
            {
                for (size_t column = 0;
                     column < variable.type.cols && source < wordCount;
                     ++column, ++source)
                {
                    const size_t destinationWord =
                        arrayIndex * variable.type.rows * 4
                        + row * 4 + column;
                    if (!writeWord(destinationWord, words + source))
                        return false;
                }
            }
            return true;
        }

        if (arrayLength > 1)
        {
            for (size_t row = 0; row < wordCount; ++row)
            {
                const size_t destinationWord = arrayIndex * 4 + row;
                if (!writeWord(destinationWord, words + row))
                    return false;
            }
            return true;
        }

        if (wordCount > variable.size / 4)
            return false;
        std::memcpy(destination, words, wordCount * 4);
        return true;
    }

    inline bool writeReshadeUniformInitializer(
        void* mappedBuffer, size_t bufferSize,
        const reshadefx::uniform& variable)
    {
        if (variable.type.is_unbounded_array())
            return false;
        const reshadefx::constant zero = {};
        const size_t arrayLength = variable.type.is_array()
            ? variable.type.array_length : 1u;
        bool success = true;
        for (size_t i = 0; i < arrayLength; ++i)
        {
            const reshadefx::constant* value = &zero;
            if (variable.has_initializer_value)
            {
                if (!variable.type.is_array())
                    value = &variable.initializer_value;
                else if (i < variable.initializer_value.array_data.size())
                    value = &variable.initializer_value.array_data[i];
            }
            success = writeReshadeUniformWords(
                mappedBuffer, bufferSize, variable, value->as_uint,
                variable.type.components(), i) && success;
        }
        return success;
    }
}

#endif
