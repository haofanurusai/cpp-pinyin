#ifndef CPP_PINYIN_FROZEN_PINYIN_INTERNAL_H
#define CPP_PINYIN_FROZEN_PINYIN_INTERNAL_H

#include <string>
#include <cpp-pinyin/FrozenPinyin.h>

namespace Pinyin
{
    std::string frozenPinyinToLegacyStyle(FrozenPinyin16 packed, int style,
                                          bool v_to_u, bool neutral_tone_with_five);
#if CPP_PINYIN_ENABLE_FILE_IO
    // Legacy text-dictionary compatibility path.  Reuses the frozen fragment LUTs
    // instead of introducing a second shuangpin mapping table.
    std::u16string frozenFullPinyinToShuangpin(const std::u16string &pinyin);
#endif
}

#endif
