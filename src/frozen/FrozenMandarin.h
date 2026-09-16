#ifndef CPP_PINYIN_FROZEN_MANDARIN_H
#define CPP_PINYIN_FROZEN_MANDARIN_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cpp-pinyin/FrozenPinyin.h>

namespace Pinyin
{
    namespace FrozenMandarin
    {
        bool contains(std::uint32_t codepoint);
        FrozenPinyin16 defaultPinyin(std::uint32_t codepoint);
        void candidates(std::uint32_t codepoint, std::vector<FrozenPinyin16> &out);
        bool isPolyphonic(std::uint32_t codepoint);
        std::uint32_t tradToSim(std::uint32_t codepoint);

        // Reconstructs a phrase into out[0..length-1] by taking each character's
        // default reading and applying only the generated override positions.
        // length must be 2..4.  Returns false when the phrase is absent.
        bool phrase(const std::uint32_t *codepoints, std::size_t length, FrozenPinyin16 out[4]);
    }
}

#endif
