#include <cpp-pinyin/ChineseG2p.h>
#include <cpp-pinyin/G2pglobal.h>

#include "ChineseG2p_p.h"
#include "DictUtil.h"
#include "frozen/FrozenMandarin.h"
#include "frozen/FrozenPinyinInternal.h"

#include <algorithm>
#include <cstdint>

#if CPP_PINYIN_ENABLE_FILE_IO
#include <filesystem>
#endif

#include "cpp-pinyin/U16Str.h"

namespace Pinyin
{
    static bool isHighSurrogate(const char16_t c) {
        return c >= 0xD800 && c <= 0xDBFF;
    }

    static bool isLowSurrogate(const char16_t c) {
        return c >= 0xDC00 && c <= 0xDFFF;
    }

    static std::uint32_t firstCodepoint(const std::u16string &s) {
        if (s.empty())
            return 0;
        const auto hi = static_cast<std::uint16_t>(s[0]);
        if (isHighSurrogate(s[0]) && s.size() >= 2 && isLowSurrogate(s[1])) {
            const auto lo = static_cast<std::uint16_t>(s[1]);
            return 0x10000u + ((static_cast<std::uint32_t>(hi - 0xD800u) << 10) |
                               static_cast<std::uint32_t>(lo - 0xDC00u));
        }
        return hi;
    }

    static std::string codepointToUtf8(const std::uint32_t cp) {
        std::string out;
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        return out;
    }

    static std::vector<std::u16string> splitString(const std::u16string &input) {
        std::vector<std::u16string> res;
        res.reserve(input.size());
        auto start = input.begin();
        const auto end = input.end();

        while (start != end) {
            // Frozen Mandarin accepts full Unicode scalar values.  Keep a UTF-16 surrogate
            // pair together so later code can recover one scalar without changing the public API.
            if (isHighSurrogate(*start) && start + 1 != end && isLowSurrogate(*(start + 1))) {
                res.emplace_back(start, start + 2);
                start += 2;
                continue;
            }

            const auto &currentChar = *start;
            if (Pinyin::isLetter(currentChar)) {
                auto letterStart = start;
                while (start != end && Pinyin::isLetter(*start)) {
                    ++start;
                }
                res.emplace_back(letterStart, start);
            } else if (Pinyin::isHanzi(currentChar) || Pinyin::isDigit(currentChar) || !Pinyin::isSpace(currentChar)) {
                res.emplace_back(1, currentChar);
                ++start;
            } else if (Pinyin::isKana(currentChar)) {
                const int length = (start + 1 != end && Pinyin::isSpecialKana(*(start + 1))) ? 2 : 1;
                res.emplace_back(start, start + length);
                std::advance(start, length);
            } else {
                ++start;
            }
        }
        return res;
    }

    static std::u16string mid(const std::vector<char16_t> &inputList, const size_t cursor, const size_t length) {
        const size_t end = std::min(cursor + length, inputList.size());
        return {inputList.begin() + cursor, inputList.begin() + end};
    }

    // reset pinyin to raw string
    static PinyinResVector resetZH(const std::vector<std::u16string> &input, const PinyinResVector &res,
                                   const std::vector<bool> &positions) {
        PinyinResVector result;
        result.reserve(input.size());
        int offset = 0;
        for (int i = 0; i < static_cast<int>(input.size()); ++i) {
            const auto &encodeStr = u16strToUtf8str(input[i]);
            if (positions[i])
                result.emplace_back(PinyinRes{encodeStr, res[i - offset].pinyin,
                                              res[i - offset].candidates, false});
            else {
                result.emplace_back(PinyinRes{encodeStr, encodeStr, {encodeStr},
                                              true});
                offset++;
            }
        }
        return result;
    }

    // delete elements from the list
    template <class T>
    static inline void removeElements(std::vector<T> &vector, int start, int n) {
        vector.erase(vector.begin() + start, vector.begin() + start + n);
    }

#if !CPP_PINYIN_ENABLE_FILE_IO
    static std::vector<std::string> getFrozenDefaultPinyin(const std::uint32_t codepoint, const int style,
                                                           const bool v_to_u,
                                                           const bool neutral_tone_with_five) {
        std::vector<FrozenPinyin16> packed;
        FrozenMandarin::candidates(codepoint, packed);
        if (packed.empty())
            return {codepointToUtf8(codepoint)};

        std::vector<std::string> result;
        result.reserve(packed.size());
        for (const auto value : packed) {
            const auto rendered = frozenPinyinToLegacyStyle(value, style, v_to_u, neutral_tone_with_five);
            if (!rendered.empty() && std::find(result.begin(), result.end(), rendered) == result.end())
                result.push_back(rendered);
        }
        if (result.empty())
            result.push_back(codepointToUtf8(codepoint));
        return result;
    }

    static PinyinResVector frozenHanziToPinyin(const std::vector<std::uint32_t> &hansList, const int style,
                                               const Error error, const bool candidates,
                                               const bool v_to_u, const bool neutral_tone_with_five) {
        PinyinResVector result;
        result.reserve(hansList.size());
        int cursor = 0;

        auto appendPhrase = [&](const std::uint32_t *phraseChars, const FrozenPinyin16 *phrasePinyin,
                                const int length) {
            for (int i = 0; i < length; ++i) {
                const auto lyric = phraseChars[i];
                result.emplace_back(PinyinRes{
                    codepointToUtf8(lyric),
                    frozenPinyinToLegacyStyle(phrasePinyin[i], style, v_to_u, neutral_tone_with_five),
                    candidates ? getFrozenDefaultPinyin(lyric, style, v_to_u, neutral_tone_with_five)
                               : std::vector<std::string>{},
                    false
                });
            }
        };

        while (cursor < static_cast<int>(hansList.size())) {
            const std::uint32_t current_char = hansList[cursor];
            if (!FrozenMandarin::contains(current_char)) {
                result.emplace_back(PinyinRes{codepointToUtf8(current_char), {}, {}, true});
                cursor++;
                continue;
            }

            if (!FrozenMandarin::isPolyphonic(current_char)) {
                const auto pinyin = getFrozenDefaultPinyin(current_char, style, v_to_u,
                                                           neutral_tone_with_five);
                result.emplace_back(PinyinRes{
                    codepointToUtf8(current_char), pinyin[0],
                    candidates ? pinyin : std::vector<std::string>{}, false
                });
                cursor++;
            } else {
                bool found = false;
                for (int length = 4; length >= 2 && !found; --length) {
                    if (cursor + length <= static_cast<int>(hansList.size())) {
                        FrozenPinyin16 phrasePinyin[4] = {};
                        if (FrozenMandarin::phrase(hansList.data() + cursor, length, phrasePinyin)) {
                            appendPhrase(hansList.data() + cursor, phrasePinyin, length);
                            cursor += length;
                            found = true;
                        }

                        if (cursor >= 1 && cursor - 1 + length <= static_cast<int>(hansList.size())) {
                            FrozenPinyin16 phrasePinyin1[4] = {};
                            if (FrozenMandarin::phrase(hansList.data() + cursor - 1, length, phrasePinyin1)) {
                                if (!result.empty())
                                    result.pop_back();
                                appendPhrase(hansList.data() + cursor - 1, phrasePinyin1, length);
                                cursor += length - 1;
                                found = true;
                            }
                        }
                    }

                    if (cursor + 1 >= length && cursor + 1 <= static_cast<int>(hansList.size())) {
                        const int start = cursor + 1 - length;
                        FrozenPinyin16 phrasePinyinBack[4] = {};
                        if (FrozenMandarin::phrase(hansList.data() + start, length, phrasePinyinBack)) {
                            removeElements(result, start, length - 1);
                            appendPhrase(hansList.data() + start, phrasePinyinBack, length);
                            cursor += 1;
                            found = true;
                        }
                    }

                    if (cursor + 2 >= length && cursor + 2 <= static_cast<int>(hansList.size())) {
                        const int start = cursor + 2 - length;
                        FrozenPinyin16 phrasePinyinBack1[4] = {};
                        if (FrozenMandarin::phrase(hansList.data() + start, length, phrasePinyinBack1)) {
                            removeElements(result, start, length - 2);
                            appendPhrase(hansList.data() + start, phrasePinyinBack1, length);
                            cursor += 2;
                            found = true;
                        }
                    }
                }

                if (!found) {
                    const auto pinyin = getFrozenDefaultPinyin(current_char, style, v_to_u,
                                                               neutral_tone_with_five);
                    result.emplace_back(PinyinRes{
                        codepointToUtf8(current_char), pinyin[0],
                        candidates ? pinyin : std::vector<std::string>{}, false
                    });
                    cursor++;
                }
            }
        }

        // Kept compatible with the original implementation: Error::Ignore currently does not
        // alter already-recognized entries in this inner path.
        if (error == Error::Ignore)
            return result;
        return result;
    }
#endif

    ChineseG2pPrivate::ChineseG2pPrivate(std::string language) :
        m_language(std::move(language)) {}

    ChineseG2pPrivate::~ChineseG2pPrivate() = default;

    // load zh convert dict
    void ChineseG2pPrivate::init() {
#if CPP_PINYIN_ENABLE_FILE_IO
        const std::filesystem::path dict_dir = dictionaryPath() / m_language;

        initialized = loadDict(dict_dir / "phrases_map.txt", phrases_map) &&
            loadDict(dict_dir / "phrases_dict.txt", phrases_dict) &&
            loadAdditionalDict(dict_dir / "user_dict.txt", phrases_dict) &&
            loadDict(dict_dir / "word.txt", word_dict);
#if CPP_PINYIN_ENABLE_TRADITIONAL
        initialized = initialized && loadDict(dict_dir / "trans_word.txt", trans_dict);
#endif
#else
        initialized = (m_language == "mandarin");

        /*
        Legacy runtime dictionary loader retained for reference.  It is compiled above when
        CPP_PINYIN_ENABLE_FILE_IO=1 and intentionally not used by the default frozen build:

        const std::filesystem::path dict_dir = dictionaryPath() / m_language;
        initialized = loadDict(dict_dir / "phrases_map.txt", phrases_map) &&
            loadDict(dict_dir / "phrases_dict.txt", phrases_dict) &&
            loadAdditionalDict(dict_dir / "user_dict.txt", phrases_dict) &&
            loadDict(dict_dir / "word.txt", word_dict) &&
            loadDict(dict_dir / "trans_word.txt", trans_dict);
        */
#endif

        toneSeen.reserve(4);
        toneCandidates.reserve(4);
    }

#if CPP_PINYIN_ENABLE_FILE_IO
    // get all chinese characters and positions in the list
    void ChineseG2pPrivate::zhPosition(const std::vector<std::u16string> &input, std::vector<char16_t> &res,
                                       std::vector<bool> &positions) {
        res.reserve(input.size());
        for (int i = 0; i < static_cast<int>(input.size()); ++i) {
            const auto &item = input[i][0];
            if (!item)
                continue;

#if CPP_PINYIN_ENABLE_TRADITIONAL
            const auto &simItem = trans_dict.find(item) != trans_dict.end() ? trans_dict[item] : item;
#else
            const auto &simItem = item;
#endif

            if (word_dict.find(simItem) != word_dict.end()) {
                res.emplace_back(simItem);
                positions[i] = true;
            }
        }
    }
#endif

    ChineseG2p::ChineseG2p(const std::string &language) {
        d_ptr = std::make_unique<ChineseG2pPrivate>(language);
        d_ptr->init();
    }

    ChineseG2p::~ChineseG2p() = default;

    bool ChineseG2p::initialized() const {
        return d_ptr->initialized;
    }

#if CPP_PINYIN_ENABLE_FILE_IO
    /*
    Style:
        陟罚臧否:zhi4 fa2 zang2 pi3
        汤汤:shang1 shang1
        到了:dao4 le1
    */
    bool ChineseG2p::loadCustomUserDict(const std::filesystem::path &filePath, const bool clearRawData) const {
        if (clearRawData)
            d_ptr->phrases_dict.clear();
        return loadAdditionalDict(filePath, d_ptr->phrases_dict);
    }

#if CPP_PINYIN_ENABLE_TRADITIONAL
    /*
    Style:
        䰾:鲃
        魚:鱼
        䴉:鹮
    */
    bool ChineseG2p::loadCustomFanJianDictionary(const std::filesystem::path &filePath, const bool clearRawData) const {
        if (clearRawData)
            d_ptr->trans_dict.clear();
        return loadDict(filePath, d_ptr->trans_dict);
    }
#endif // CPP_PINYIN_ENABLE_TRADITIONAL
#endif // CPP_PINYIN_ENABLE_FILE_IO

    void ChineseG2p::setToneConverter(const ToneConverter &toneConverter) const {
        d_ptr->m_toneConverter = toneConverter;
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::string &hans, int style, Error error, bool candidates,
                                              bool v_to_u, bool neutral_tone_with_five) const {
        return hanziToPinyin(splitString(utf8strToU16str(hans)), style, error, candidates, v_to_u,
                             neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::string> &hans, int style, Error error,
                                              bool candidates, bool v_to_u, bool neutral_tone_with_five) const {
        std::vector<std::u16string> hansList;
        hansList.reserve(hans.size());
        for (const auto &item : hans) {
            hansList.emplace_back(utf8strToU16str(item));
        }
        return hanziToPinyin(hansList, style, error, candidates, v_to_u, neutral_tone_with_five);
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<std::u16string> &hans, int style, Error error,
                                              bool candidates, bool v_to_u, bool neutral_tone_with_five) const {
#if CPP_PINYIN_ENABLE_FILE_IO
        std::vector<char16_t> hansList;
        std::vector<bool> inputPos(hans.size(), false);
        d_ptr->zhPosition(hans, hansList, inputPos);
        return resetZH(hans, hanziToPinyin(hansList, style, error, candidates, v_to_u, neutral_tone_with_five),
                       inputPos);
#else
        if (d_ptr->m_language == "mandarin") {
            std::vector<std::uint32_t> hansList;
            std::vector<bool> inputPos(hans.size(), false);
            hansList.reserve(hans.size());
            for (std::size_t i = 0; i < hans.size(); ++i) {
                const auto cp = firstCodepoint(hans[i]);
                if (!cp)
                    continue;
                const auto sim = FrozenMandarin::tradToSim(cp);
                if (FrozenMandarin::contains(sim)) {
                    hansList.push_back(sim);
                    inputPos[i] = true;
                }
            }
            return resetZH(hans, frozenHanziToPinyin(hansList, style, error, candidates, v_to_u,
                                                     neutral_tone_with_five), inputPos);
        }

        // No frozen Cantonese LUT was requested.  With file I/O disabled, leave such input raw.
        PinyinResVector raw;
        raw.reserve(hans.size());
        for (const auto &item : hans) {
            const auto utf8 = u16strToUtf8str(item);
            raw.emplace_back(PinyinRes{utf8, utf8, {utf8}, true});
        }
        return raw;
#endif
    }

    PinyinResVector ChineseG2p::hanziToPinyin(const std::vector<char16_t> &hansList, int style, Error error,
                                              bool candidates,
                                              bool v_to_u, bool neutral_tone_with_five) const {
        PinyinResVector result;
        result.reserve(hansList.size());
        int cursor = 0;
        while (cursor < static_cast<int>(hansList.size())) {
            const char16_t &current_char = hansList[cursor];

            if (d_ptr->word_dict.find(current_char) == d_ptr->word_dict.end()) {
                result.emplace_back(PinyinRes{u16strToUtf8str(current_char), {}, {}, true});
                cursor++;
                continue;
            }

            if (!d_ptr->isPolyphonic(current_char)) {
                const auto &pinyin = d_ptr->getDefaultPinyin(current_char, style, v_to_u, neutral_tone_with_five);
                result.emplace_back(PinyinRes{
                    u16strToUtf8str(current_char),
                    pinyin[0],
                    candidates ? pinyin : std::vector<std::string>{},
                    false
                });
                cursor++;
            } else {
                bool found = false;
                for (int length = 4; length >= 2 && !found; length--) {
                    if (cursor + length <= static_cast<int>(hansList.size())) {
                        const std::u16string subPhrase = mid(hansList, cursor, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhrase);
                        if (it != d_ptr->phrases_dict.end()) {
                            const auto &subRes = d_ptr->toneConvert(it->second, style, v_to_u, neutral_tone_with_five);
                            for (int i = 0; i < static_cast<int>(subRes.size()); i++) {
                                const auto &lyric = subPhrase[i];
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric), u16strToUtf8str(subRes[i]),
                                    candidates ? d_ptr->getDefaultPinyin(lyric, style, v_to_u,
                                                                         neutral_tone_with_five)
                                               : std::vector<std::string>{}, false
                                });
                            }
                            cursor += length;
                            found = true;
                        }

                        if (cursor >= 1) {
                            const std::u16string subPhrase1 = mid(hansList, cursor - 1, length);
                            const auto &it1 = d_ptr->phrases_dict.find(subPhrase1);
                            if (it1 != d_ptr->phrases_dict.end()) {
                                result.pop_back();
                                const auto &subRes1 = d_ptr->toneConvert(it1->second, style, v_to_u,
                                                                         neutral_tone_with_five);
                                for (int i = 0; i < static_cast<int>(subRes1.size()); i++) {
                                    const auto &lyric = subPhrase1[i];
                                    result.emplace_back(PinyinRes{
                                        u16strToUtf8str(lyric), u16strToUtf8str(subRes1[i]),
                                        candidates ? d_ptr->getDefaultPinyin(lyric, style, v_to_u,
                                                                             neutral_tone_with_five)
                                                   : std::vector<std::string>{}, false
                                    });
                                }
                                cursor += length - 1;
                                found = true;
                            }
                        }
                    }

                    if (cursor + 1 >= length && cursor + 1 <= static_cast<int>(hansList.size())) {
                        const std::u16string subPhraseBack = mid(hansList, cursor + 1 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack);
                        if (it != d_ptr->phrases_dict.end()) {
                            removeElements(result, cursor + 1 - length, length - 1);
                            const auto &subResBack = d_ptr->toneConvert(it->second, style, v_to_u,
                                                                        neutral_tone_with_five);
                            for (int i = 0; i < static_cast<int>(subResBack.size()); i++) {
                                const auto &lyric = subPhraseBack[i];
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric), u16strToUtf8str(subResBack[i]),
                                    candidates ? d_ptr->getDefaultPinyin(lyric, style, v_to_u,
                                                                         neutral_tone_with_five)
                                               : std::vector<std::string>{}, false
                                });
                            }
                            cursor += 1;
                            found = true;
                        }
                    }

                    if (cursor + 2 >= length && cursor + 2 <= static_cast<int>(hansList.size())) {
                        const std::u16string subPhraseBack1 = mid(hansList, cursor + 2 - length, length);
                        const auto &it = d_ptr->phrases_dict.find(subPhraseBack1);
                        if (it != d_ptr->phrases_dict.end()) {
                            removeElements(result, cursor + 2 - length, length - 2);
                            const auto &subResBack1 = d_ptr->toneConvert(it->second, style, v_to_u,
                                                                         neutral_tone_with_five);
                            for (int i = 0; i < static_cast<int>(subResBack1.size()); i++) {
                                const auto &lyric = subPhraseBack1[i];
                                result.emplace_back(PinyinRes{
                                    u16strToUtf8str(lyric), u16strToUtf8str(subResBack1[i]),
                                    candidates ? d_ptr->getDefaultPinyin(lyric, style, v_to_u,
                                                                         neutral_tone_with_five)
                                               : std::vector<std::string>{}, false
                                });
                            }
                            cursor += 2;
                            found = true;
                        }
                    }
                }

                if (!found) {
                    const auto &pinyin = d_ptr->getDefaultPinyin(current_char, style, v_to_u,
                                                                 neutral_tone_with_five);
                    result.emplace_back(PinyinRes{
                        u16strToUtf8str(current_char), pinyin[0],
                        candidates ? pinyin : std::vector<std::string>{}, false
                    });
                    cursor++;
                }
            }
        }

        if (error == Error::Ignore)
            return result;
        return result;
    }

    std::string ChineseG2p::tradToSim(const std::string &oneHanzi) const {
#if CPP_PINYIN_ENABLE_FILE_IO
#if CPP_PINYIN_ENABLE_TRADITIONAL
        return u16strToUtf8str(d_ptr->tradToSim(utf8strToU16str(oneHanzi)[0]));
#else
        return oneHanzi;
#endif
#else
        if (d_ptr->m_language == "mandarin") {
            const auto u16 = utf8strToU16str(oneHanzi);
            const auto cp = firstCodepoint(u16);
            return cp ? codepointToUtf8(FrozenMandarin::tradToSim(cp)) : std::string{};
        }
        return oneHanzi;
#endif
    }

    bool ChineseG2p::isPolyphonic(const std::string &oneHanzi) const {
#if CPP_PINYIN_ENABLE_FILE_IO
        return d_ptr->isPolyphonic(utf8strToU16str(oneHanzi)[0]);
#else
        if (d_ptr->m_language == "mandarin") {
            const auto cp = firstCodepoint(utf8strToU16str(oneHanzi));
            return cp && FrozenMandarin::isPolyphonic(cp);
        }
        return false;
#endif
    }

    std::vector<std::string> ChineseG2p::getDefaultPinyin(const std::string &oneHanzi, int style, bool v_to_u,
                                                          bool neutral_tone_with_five) const {
#if CPP_PINYIN_ENABLE_FILE_IO
        const auto raw = utf8strToU16str(oneHanzi)[0];
#if CPP_PINYIN_ENABLE_TRADITIONAL
        return d_ptr->getDefaultPinyin(d_ptr->tradToSim(raw), style, v_to_u, neutral_tone_with_five);
#else
        return d_ptr->getDefaultPinyin(raw, style, v_to_u, neutral_tone_with_five);
#endif
#else
        if (d_ptr->m_language == "mandarin") {
            const auto cp = firstCodepoint(utf8strToU16str(oneHanzi));
            if (!cp)
                return {};
            return getFrozenDefaultPinyin(FrozenMandarin::tradToSim(cp), style, v_to_u,
                                          neutral_tone_with_five);
        }
        return {oneHanzi};
#endif
    }
}
