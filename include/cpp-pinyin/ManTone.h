#ifndef TONEUTIL_H
#define TONEUTIL_H

#include <cpp-pinyin/PinyinGlobal.h>
#include <cpp-pinyin/ToneConverter.h>

namespace Pinyin
{
    class CPP_PINYIN_EXPORT ManTone final : public ToneConverter {
    public:
        // https://github.com/mozillazg/python-pinyin/blob/master/pypinyin/constants.py
        enum Style {
            // 普通风格，不带声调。如： 中国 -> ``zhong guo``
            NORMAL = 0,
            // 标准声调风格，拼音声调在韵母第一个字母上（默认风格）。如： 中国 -> ``zhōng guó``
            TONE = 1,
            // 声调风格2，即拼音声调在各个韵母之后，用数字 [1-4] 进行表示。如： 中国 -> ``zho1ng guo2``
            TONE2 = 2,
            // 声调风格3，即拼音声调在各个拼音之后，用数字 [1-4] 进行表示。如： 中国 -> ``zhong1 guo2``
            TONE3 = 8,
            // 微软双拼直通风格。Frozen Mandarin 路径直接输出两个双拼 ASCII 字符。
            // 选用独立 bit 值以保持原有 Style 数值 ABI 不变。
            SHUANGPIN = 16
        };

        ManTone() {
            m_converts.insert({static_cast<int>(Style::NORMAL), toneToNormal});
            m_converts.insert({static_cast<int>(Style::TONE), toneToTone});
            m_converts.insert({static_cast<int>(Style::TONE2), toneToTone2});
            m_converts.insert({static_cast<int>(Style::TONE3), toneToTone3});
        };
        ~ManTone() override = default;

        static std::u16string toneToNormal(const std::u16string &pinyin, bool v_to_u = false,
                                           bool neutral_tone_with_five = false);

        static std::u16string toneToTone(const std::u16string &pinyin, bool v_to_u = false,
                                         bool neutral_tone_with_five = false);

        static std::u16string toneToTone2(const std::u16string &pinyin, bool v_to_u = false,
                                          bool neutral_tone_with_five = false);

        static std::u16string toneToTone3(const std::u16string &pinyin, bool v_to_u = false,
                                          bool neutral_tone_with_five = false);
    };


} // Pinyin

#endif //TONEUTIL_H
