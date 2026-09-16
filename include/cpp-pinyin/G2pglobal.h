#ifndef G2PGLOBAL_H
#define G2PGLOBAL_H

#include <cpp-pinyin/PinyinGlobal.h>

#if CPP_PINYIN_ENABLE_FILE_IO
#include <filesystem>
#endif

namespace Pinyin
{
#if CPP_PINYIN_ENABLE_FILE_IO
    std::filesystem::path CPP_PINYIN_EXPORT dictionaryPath();

    void CPP_PINYIN_EXPORT setDictionaryPath(const std::filesystem::path &dir);
#endif

    bool CPP_PINYIN_EXPORT isLetter(const char16_t &c);

    bool CPP_PINYIN_EXPORT isHanzi(const char16_t &c);

    bool CPP_PINYIN_EXPORT isKana(const char16_t &c);

    bool CPP_PINYIN_EXPORT isDigit(const char16_t &c);

    bool CPP_PINYIN_EXPORT isSpace(const char16_t &c);

    bool CPP_PINYIN_EXPORT isSpecialKana(const char16_t &c);
}

#endif // G2PGLOBAL_H
