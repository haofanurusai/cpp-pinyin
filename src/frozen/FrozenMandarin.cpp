#include "FrozenMandarin.h"
#include "FrozenMandarinData.h"

namespace Pinyin
{
    namespace FrozenMandarin
    {
        namespace
        {
            template <typename T, typename KeyT, typename Getter>
            const T *binaryFind(const T *data, std::size_t count, KeyT key, Getter getter) {
                std::size_t lo = 0, hi = count;
                while (lo < hi) {
                    const std::size_t mid = lo + ((hi - lo) >> 1);
                    const auto cur = getter(data[mid]);
                    if (cur < key) lo = mid + 1; else hi = mid;
                }
                return (lo < count && getter(data[lo]) == key) ? data + lo : nullptr;
            }

            const FrozenData::WordPair16 *findWordPair(const FrozenData::WordPair16 *data,
                                                       std::size_t count, std::uint16_t key) {
                return binaryFind(data, count, key,
                                  [](const FrozenData::WordPair16 &v) { return v.key; });
            }

            const FrozenData::CandidateIndex16 *findCandidate(std::uint16_t cp) {
                return binaryFind(FrozenData::kCandidateIndex, FrozenData::kCandidateIndexCount, cp,
                                  [](const FrozenData::CandidateIndex16 &v) { return v.codepoint; });
            }

#if CPP_PINYIN_ENABLE_TRADITIONAL
            const FrozenData::Pair16 *findPair(const FrozenData::Pair16 *data, std::size_t count,
                                               std::uint16_t key) {
                return binaryFind(data, count, key,
                                  [](const FrozenData::Pair16 &v) { return v.from; });
            }
#endif

            FrozenPinyin16 findBmpWord(std::uint16_t cp) {
                const std::uint8_t page = static_cast<std::uint8_t>(cp >> 8);
                if (page < FrozenData::kBmpPageBase ||
                    page >= static_cast<std::uint8_t>(FrozenData::kBmpPageBase + FrozenData::kBmpPageCount))
                    return 0;
                const std::uint8_t pageIndex = static_cast<std::uint8_t>(page - FrozenData::kBmpPageBase);
                const std::uint16_t descriptor = FrozenData::kWordPageOffset[pageIndex];
                const std::uint8_t low = static_cast<std::uint8_t>(cp);
                if (descriptor & FrozenData::kDensePageMask) {
                    const std::uint16_t off = descriptor & static_cast<std::uint16_t>(~FrozenData::kDensePageMask);
                    return FrozenData::kWordDense[off + low];
                }

                const std::uint16_t off = descriptor;
                const std::uint8_t count = FrozenData::kWordPageCount[pageIndex];
                std::uint8_t lo = 0, hi = count;
                while (lo < hi) {
                    const std::uint8_t mid = static_cast<std::uint8_t>(lo + ((hi - lo) >> 1));
                    if (FrozenData::kWordSparseLow[off + mid] < low) lo = static_cast<std::uint8_t>(mid + 1);
                    else hi = mid;
                }
                if (lo < count && FrozenData::kWordSparseLow[off + lo] == low)
                    return FrozenData::kWordSparsePinyin[off + lo];
                return 0;
            }

            std::uint32_t phrase2Key(const std::uint32_t *c) {
                if (c[0] > 0xFFFFu || c[1] > 0xFFFFu) return 0;
                return (c[0] << 16) | c[1];
            }
            std::uint64_t phrase3Key(const std::uint32_t *c) {
                if (c[0] > 0xFFFFu || c[1] > 0xFFFFu || c[2] > 0xFFFFu) return 0;
                return (static_cast<std::uint64_t>(c[0]) << 32) |
                       (static_cast<std::uint64_t>(c[1]) << 16) | c[2];
            }
            std::uint64_t phrase4Key(const std::uint32_t *c) {
                if (c[0] > 0xFFFFu || c[1] > 0xFFFFu || c[2] > 0xFFFFu || c[3] > 0xFFFFu) return 0;
                return (static_cast<std::uint64_t>(c[0]) << 48) |
                       (static_cast<std::uint64_t>(c[1]) << 32) |
                       (static_cast<std::uint64_t>(c[2]) << 16) | c[3];
            }

            template <typename KeyT>
            bool findPhraseDesc(const KeyT *keys, const std::uint16_t *desc, std::size_t count,
                                KeyT key, std::uint16_t &out) {
                std::size_t lo = 0, hi = count;
                while (lo < hi) {
                    const std::size_t mid = lo + ((hi - lo) >> 1);
                    if (keys[mid] < key) lo = mid + 1; else hi = mid;
                }
                if (lo >= count || keys[lo] != key) return false;
                out = desc[lo];
                return true;
            }

            void applyPhraseOverride(std::uint16_t desc, std::size_t length,
                                     const FrozenPinyin16 *pool, FrozenPinyin16 out[4]) {
                unsigned shift = length == 2 ? 13u : 11u;
                unsigned maskBits = static_cast<unsigned>(length);
                std::uint16_t offsetMask = static_cast<std::uint16_t>((1u << shift) - 1u);
                std::uint16_t mask = static_cast<std::uint16_t>((desc >> shift) & ((1u << maskBits) - 1u));
                std::uint16_t off = desc & offsetMask;
                for (std::size_t i = 0; i < length; ++i) {
                    if (mask & (1u << i)) out[i] = pool[off++];
                }
            }
        }

        FrozenPinyin16 defaultPinyin(std::uint32_t codepoint) {
            if (codepoint <= 0xFFFFu) {
                const auto cp = static_cast<std::uint16_t>(codepoint);
                const auto py = findBmpWord(cp);
                if (py) return py;
                const auto *extra = findWordPair(FrozenData::kWordExtra, FrozenData::kWordExtraCount, cp);
                return extra ? extra->pinyin : 0;
            }

            if ((codepoint >> 16) == 2u) {
                const auto *entry = findWordPair(FrozenData::kWordExt, FrozenData::kWordExtCount,
                                                 static_cast<std::uint16_t>(codepoint));
                return entry ? entry->pinyin : 0;
            }
#if CPP_PINYIN_ENABLE_BIANG
            if (codepoint == 0x30EDDu)
                return FrozenData::kBiangPinyin;
#endif
            return 0;
        }

        bool contains(std::uint32_t codepoint) {
            return defaultPinyin(codepoint) != 0;
        }

        void candidates(std::uint32_t codepoint, std::vector<FrozenPinyin16> &out) {
            out.clear();
            const auto def = defaultPinyin(codepoint);
            if (!def) return;
            out.push_back(def);
            if (codepoint > 0xFFFFu) return;
            const auto *entry = findCandidate(static_cast<std::uint16_t>(codepoint));
            if (!entry) return;
            const std::uint16_t off = entry->desc & 0x0FFFu;
            const std::uint8_t count = static_cast<std::uint8_t>((entry->desc >> 12) & 0x07u);
            out.reserve(static_cast<std::size_t>(count) + 1u);
            for (std::uint8_t i = 0; i < count; ++i)
                out.push_back(FrozenData::kCandidatePool[off + i]);
        }

        bool isPolyphonic(std::uint32_t codepoint) {
            if (codepoint < 0x3000u || codepoint >= 0xA000u) return false;
            const std::uint8_t page = static_cast<std::uint8_t>((codepoint >> 8) - FrozenData::kBmpPageBase);
            const std::uint8_t block = FrozenData::kPolyPage[page];
            if (!block) return false;
            const std::uint8_t low = static_cast<std::uint8_t>(codepoint);
            const std::size_t byteIndex = (static_cast<std::size_t>(block) - 1u) * 32u + (low >> 3);
            return (FrozenData::kPolyBitmap[byteIndex] & (1u << (low & 7u))) != 0;
        }

        std::uint32_t tradToSim(std::uint32_t codepoint) {
#if CPP_PINYIN_ENABLE_TRADITIONAL
#if CPP_PINYIN_ENABLE_BIANG
            if (codepoint == 0x30EDEu) return 0x30EDDu;
#endif
            if (codepoint <= 0xFFFFu) {
                const auto key = static_cast<std::uint16_t>(codepoint);
                if (const auto *p = findPair(FrozenData::kTransBmpBmp, FrozenData::kTransBmpBmpCount, key))
                    return p->to;
                if (const auto *p = findPair(FrozenData::kTransBmpExt, FrozenData::kTransBmpExtCount, key))
                    return 0x20000u | p->to;
                return codepoint;
            }
            if ((codepoint >> 16) == 2u) {
                const auto key = static_cast<std::uint16_t>(codepoint);
                if (const auto *p = findPair(FrozenData::kTransExtBmp, FrozenData::kTransExtBmpCount, key))
                    return p->to;
                if (const auto *p = findPair(FrozenData::kTransExtExt, FrozenData::kTransExtExtCount, key))
                    return 0x20000u | p->to;
            }
#endif
            return codepoint;
        }

        bool phrase(const std::uint32_t *codepoints, std::size_t length, FrozenPinyin16 out[4]) {
            if (!codepoints || !out || length < 2 || length > 4) return false;
            std::uint16_t desc = 0;
            bool found = false;
            switch (length) {
            case 2: {
                const auto key = phrase2Key(codepoints);
                if (!key) return false;
                found = findPhraseDesc(FrozenData::kPhrase2Keys, FrozenData::kPhrase2Desc,
                                       FrozenData::kPhrase2Count, key, desc);
                break;
            }
            case 3: {
                const auto key = phrase3Key(codepoints);
                if (!key) return false;
                found = findPhraseDesc(FrozenData::kPhrase3Keys, FrozenData::kPhrase3Desc,
                                       FrozenData::kPhrase3Count, key, desc);
                break;
            }
            case 4: {
                const auto key = phrase4Key(codepoints);
                if (!key) return false;
                found = findPhraseDesc(FrozenData::kPhrase4Keys, FrozenData::kPhrase4Desc,
                                       FrozenData::kPhrase4Count, key, desc);
                break;
            }
            }
            if (!found) return false;
            for (std::size_t i = 0; i < length; ++i) {
                out[i] = defaultPinyin(codepoints[i]);
                if (!out[i]) return false;
            }
            if (length == 2) applyPhraseOverride(desc, length, FrozenData::kPhrase2Override, out);
            else if (length == 3) applyPhraseOverride(desc, length, FrozenData::kPhrase3Override, out);
            else applyPhraseOverride(desc, length, FrozenData::kPhrase4Override, out);
            return true;
        }
    }
}
