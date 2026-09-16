#include <cpp-pinyin/FrozenPinyin.h>

#include "FrozenMandarinData.h"
#include "FrozenPinyinInternal.h"

namespace Pinyin
{
    namespace
    {
        struct Fragments {
            const char *initial;
            const char *finalBase;
            std::uint8_t finalMeta;
        };

        bool fragments(FrozenPinyin16 packed, Fragments &out) {
            // kInitialId/kFinalId are direct byte offsets into NUL-separated
            // one-dimensional pools. ALT is already part of the selector.
            const std::uint8_t initialIndex = static_cast<std::uint8_t>(
                (packed & 0x001Fu) | ((packed >> 8) & 0x0020u));
            const std::uint8_t finalIndex = static_cast<std::uint8_t>((packed >> 8) & 0x007Fu);
            const std::uint8_t initialOffset = FrozenData::kInitialId[initialIndex];
            const std::uint8_t finalOffset = FrozenData::kFinalId[finalIndex];
            if (initialOffset == FrozenData::kInvalidFragment ||
                finalOffset == FrozenData::kInvalidFragment)
                return false;
            out.initial = FrozenData::kInitialText + initialOffset;
            out.finalBase = FrozenData::kFinalBase + finalOffset;
            const char *meta = out.finalBase;
            while (*meta) ++meta;
            out.finalMeta = static_cast<std::uint8_t>(static_cast<unsigned char>(meta[1]));
            return true;
        }

        /* Previous full-tone-string lookup, retained for reference only:
        const char *toneMarkedFinal(std::uint8_t offset, std::uint8_t tone) {
            switch (tone) {
            case 1: return FrozenData::kFinalToneMarked1 + offset;
            case 2: return FrozenData::kFinalToneMarked2 + offset;
            case 3: return FrozenData::kFinalToneMarked3 + offset;
            case 4: return FrozenData::kFinalToneMarked4 + offset;
            default: return FrozenData::kFinalBase + offset;
            }
        }
        */

        std::uint8_t legacyTone2Position(std::uint8_t finalMeta) {
            // bit0 stores the zero-based marked-vowel position. TONE2 inserts
            // the digit after that ASCII vowel, so the legacy position is +1.
            return static_cast<std::uint8_t>((finalMeta & 0x01u) + 1u);
        }

        /* Previous fixed-stride lookup, retained for reference only:
        const std::uint8_t iid = FrozenData::kInitialId[initialIndex];
        const std::uint8_t fid = FrozenData::kFinalId[finalIndex];
        out.initial = FrozenData::kInitialText[iid];
        out.finalBase = FrozenData::kFinalBase[fid];
        */

        std::size_t appendChar(char ch, char *out, std::size_t cap, std::size_t pos, bool useUmlaut) {
            if (useUmlaut && ch == 'v') {
                if (pos + 2 >= cap) return static_cast<std::size_t>(-1);
                out[pos++] = static_cast<char>(0xC3);
                out[pos++] = static_cast<char>(0xBC);
                return pos;
            }
            if (pos + 1 >= cap) return static_cast<std::size_t>(-1);
            out[pos++] = ch;
            return pos;
        }

        std::size_t appendText(const char *src, char *out, std::size_t cap, std::size_t pos,
                               bool useUmlaut) {
            while (*src) {
                // Keep non-ASCII bytes verbatim. Literal ASCII 'v' alone is
                // subject to optional v->ü expansion.
                if (static_cast<unsigned char>(*src) >= 0x80) {
                    if (pos + 1 >= cap) return static_cast<std::size_t>(-1);
                    out[pos++] = *src++;
                    continue;
                }
                pos = appendChar(*src++, out, cap, pos, useUmlaut);
                if (pos == static_cast<std::size_t>(-1)) return pos;
            }
            return pos;
        }

        std::size_t appendToneMarkedFinal(const Fragments &f, std::uint8_t tone,
                                                char *out, std::size_t cap, std::size_t pos,
                                                bool useUmlaut) {
            if (tone < 1 || tone > 4)
                return appendText(f.finalBase, out, cap, pos, useUmlaut);

            const std::uint8_t markPos = static_cast<std::uint8_t>(f.finalMeta & 0x01u);
            const std::uint8_t vowelId = static_cast<std::uint8_t>((f.finalMeta >> 1) & 0x07u);
            if (vowelId >= 6u)
                return static_cast<std::size_t>(-1);

            const std::size_t toneOffset =
                static_cast<std::size_t>((vowelId * 4u + (tone - 1u)) * 2u);
            std::uint8_t srcPos = 0;
            for (const char *p = f.finalBase; *p; ++p, ++srcPos) {
                if (srcPos == markPos) {
                    if (pos + 2 >= cap) return static_cast<std::size_t>(-1);
                    out[pos++] = static_cast<char>(FrozenData::kToneVowelUtf8[toneOffset]);
                    out[pos++] = static_cast<char>(FrozenData::kToneVowelUtf8[toneOffset + 1]);
                } else {
                    pos = appendChar(*p, out, cap, pos, useUmlaut);
                    if (pos == static_cast<std::size_t>(-1)) return pos;
                }
            }
            return pos;
        }

        std::size_t finish(char *out, std::size_t cap, std::size_t pos) {
            if (pos == static_cast<std::size_t>(-1) || pos >= cap) return 0;
            out[pos] = '\0';
            return pos;
        }

        std::size_t legacyTone2(FrozenPinyin16 packed, char *out, std::size_t cap,
                                bool useUmlaut) {
            Fragments f{};
            if (!fragments(packed, f)) return 0;
            std::size_t pos = appendText(f.initial, out, cap, 0, useUmlaut);
            if (pos == static_cast<std::size_t>(-1)) return 0;
            const auto tone = frozenPinyinTone(packed);
            const auto tonePos = legacyTone2Position(f.finalMeta);
            std::uint8_t srcPos = 0;
            for (const char *p = f.finalBase; *p; ++p) {
                pos = appendChar(*p, out, cap, pos, useUmlaut);
                if (pos == static_cast<std::size_t>(-1)) return 0;
                ++srcPos;
                if (srcPos == tonePos && tone >= 1 && tone <= 4) {
                    if (pos + 1 >= cap) return 0;
                    out[pos++] = static_cast<char>('0' + tone);
                }
            }
            return finish(out, cap, pos);
        }
    }

    std::size_t frozenPinyinToBuffer(FrozenPinyin16 packed, ManTone::Style style,
                                     char *out, std::size_t capacity, bool useUmlaut,
                                     bool neutralToneWithFive) {
        if (!out || capacity == 0 || packed == 0) return 0;
        if (style == ManTone::Style::SHUANGPIN) {
            if (capacity < 3) return 0;
            const FrozenPinyin16 pair = frozenPinyinAsciiPair(packed);
            out[0] = static_cast<char>(pair & 0xFFu);
            out[1] = static_cast<char>((pair >> 8) & 0xFFu);
            out[2] = '\0';
            return 2;
        }

        Fragments f{};
        if (!fragments(packed, f)) return 0;
        std::size_t pos = appendText(f.initial, out, capacity, 0, useUmlaut);
        if (pos == static_cast<std::size_t>(-1)) return 0;
        const auto tone = frozenPinyinTone(packed);

        if (style == ManTone::Style::TONE && tone >= 1 && tone <= 4)
            pos = appendToneMarkedFinal(f, tone, out, capacity, pos, useUmlaut);
        else
            pos = appendText(f.finalBase, out, capacity, pos, useUmlaut);
        if (pos == static_cast<std::size_t>(-1)) return 0;

        if (style == ManTone::Style::TONE3 &&
            ((tone >= 1 && tone <= 4) || (tone == 5 && neutralToneWithFive))) {
            if (pos + 1 >= capacity) return 0;
            out[pos++] = static_cast<char>('0' + tone);
        }
        return finish(out, capacity, pos);
    }

    std::string frozenPinyinToString(FrozenPinyin16 packed, ManTone::Style style,
                                     bool useUmlaut, bool neutralToneWithFive) {
        char buffer[16] = {};
        const auto n = frozenPinyinToBuffer(packed, style, buffer, sizeof(buffer),
                                            useUmlaut, neutralToneWithFive);
        return n ? std::string(buffer, n) : std::string{};
    }

    std::string frozenPinyinToLegacyStyle(FrozenPinyin16 packed, int style,
                                          bool v_to_u, bool neutral_tone_with_five) {
        if (style == static_cast<int>(ManTone::Style::SHUANGPIN))
            return frozenPinyinToString(packed, ManTone::Style::SHUANGPIN, v_to_u,
                                        neutral_tone_with_five);
        if (style == static_cast<int>(ManTone::Style::NORMAL))
            return frozenPinyinToString(packed, ManTone::Style::NORMAL, v_to_u,
                                        neutral_tone_with_five);
        if (style == static_cast<int>(ManTone::Style::TONE))
            return frozenPinyinToString(packed, ManTone::Style::TONE, v_to_u,
                                        neutral_tone_with_five);
        if (style == static_cast<int>(ManTone::Style::TONE3))
            return frozenPinyinToString(packed, ManTone::Style::TONE3, v_to_u,
                                        neutral_tone_with_five);
        if (style == static_cast<int>(ManTone::Style::TONE2)) {
            char buffer[16] = {};
            const auto n = legacyTone2(packed, buffer, sizeof(buffer), v_to_u);
            return n ? std::string(buffer, n) : std::string{};
        }
        return frozenPinyinToString(packed, ManTone::Style::TONE, v_to_u,
                                    neutral_tone_with_five);
    }
#if CPP_PINYIN_ENABLE_FILE_IO
    std::u16string frozenFullPinyinToShuangpin(const std::u16string &pinyin) {
        const std::u16string base = ManTone::toneToNormal(pinyin, false, false);

        for (std::uint8_t ii = 0; ii < 64; ++ii) {
            const std::uint8_t initialOffset = FrozenData::kInitialId[ii];
            if (initialOffset == FrozenData::kInvalidFragment)
                continue;

            const char *initial = FrozenData::kInitialText + initialOffset;
            std::size_t pos = 0;
            bool prefix = true;
            for (const char *p = initial; *p; ++p, ++pos) {
                if (pos >= base.size() || base[pos] != static_cast<unsigned char>(*p)) {
                    prefix = false;
                    break;
                }
            }
            if (!prefix)
                continue;

            const std::uint8_t alt = static_cast<std::uint8_t>((ii >> 5) & 1u);
            for (std::uint8_t fi = 0; fi < 128; ++fi) {
                if (((fi >> 5) & 1u) != alt)
                    continue;
                const std::uint8_t finalOffset = FrozenData::kFinalId[fi];
                if (finalOffset == FrozenData::kInvalidFragment)
                    continue;

                const char *finalText = FrozenData::kFinalBase + finalOffset;
                std::size_t tail = pos;
                bool equal = true;
                for (const char *p = finalText; *p; ++p, ++tail) {
                    if (tail >= base.size() || base[tail] != static_cast<unsigned char>(*p)) {
                        equal = false;
                        break;
                    }
                }
                if (!equal || tail != base.size())
                    continue;

                const char16_t bank = static_cast<char16_t>(0x40u | (ii & 0x1Fu));
                const char16_t finalChar = static_cast<char16_t>(0x20u | (fi & 0x5Fu));
                return {bank, finalChar};
            }
        }

        // Unsupported legacy-only syllables (for example hm/m/n) remain untouched.
        return pinyin;
    }
#endif

}
