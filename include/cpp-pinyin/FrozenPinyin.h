#ifndef CPP_PINYIN_FROZEN_PINYIN_H
#define CPP_PINYIN_FROZEN_PINYIN_H

#include <cstddef>
#include <cstdint>
#include <string>

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/ManTone.h>

namespace Pinyin
{
    using FrozenPinyin16 = std::uint16_t;

    // 8-bit-friendly packed layout:
    // low byte : tone[2:0] in bits 7..5 + shuangpin initial bits 4..0
    // high byte: EXT in bit7 + original final bit6 + ALT in bit5 + final bits4..0
    //
    // Reconstructing the two ASCII shuangpin bytes as one uint16_t requires only
    // an AND and an OR, with no shifts:
    //     asciiPair = (packed & 0x5F1F) | 0x2040
    // low byte = initial ASCII, high byte = final ASCII.
    constexpr FrozenPinyin16 FROZEN_PINYIN_ASCII_MASK = 0x5F1Fu;
    constexpr FrozenPinyin16 FROZEN_PINYIN_ASCII_RESTORE = 0x2040u;
    constexpr FrozenPinyin16 FROZEN_PINYIN_EXT_MASK = 0x8000u;
    constexpr FrozenPinyin16 FROZEN_PINYIN_ALT_MASK = 0x2000u;
    constexpr FrozenPinyin16 FROZEN_PINYIN_TONE_MASK = 0x00E0u;

    /*
    Deprecated second style enum retained only as source history.  The public Frozen API now
    uses ManTone::Style directly so callers only need one style enum.

    enum class FrozenPinyinStyle : std::uint8_t {
        DirectShuangpin = 0,
        ToneMark = 1,
        ToneNumber = 2,
        NoTone = 3
    };
    */

    constexpr FrozenPinyin16 frozenPinyinAsciiPair(FrozenPinyin16 packed) {
        return static_cast<FrozenPinyin16>((packed & FROZEN_PINYIN_ASCII_MASK) |
                                           FROZEN_PINYIN_ASCII_RESTORE);
    }
    constexpr bool frozenPinyinIsExtended(FrozenPinyin16 packed) {
        return (packed & FROZEN_PINYIN_EXT_MASK) != 0;
    }
    constexpr bool frozenPinyinIsAlternate(FrozenPinyin16 packed) {
        return (packed & FROZEN_PINYIN_ALT_MASK) != 0;
    }
    constexpr std::uint8_t frozenPinyinTone(FrozenPinyin16 packed) {
        return static_cast<std::uint8_t>((packed & FROZEN_PINYIN_TONE_MASK) >> 5);
    }

    // Dictionary-size-independent conversion.  Full pinyin is rebuilt from a
    // 64-entry initial-fragment LUT plus a 128-entry final-fragment LUT; ALT is
    // part of those indices, so ambiguous finals do not require a branch.
    std::size_t CPP_PINYIN_EXPORT frozenPinyinToBuffer(
        FrozenPinyin16 packed, ManTone::Style style, char *out, std::size_t capacity,
        bool useUmlaut = true, bool neutralToneWithFive = false);

    std::string CPP_PINYIN_EXPORT frozenPinyinToString(
        FrozenPinyin16 packed, ManTone::Style style,
        bool useUmlaut = true, bool neutralToneWithFive = false);
}

#endif
