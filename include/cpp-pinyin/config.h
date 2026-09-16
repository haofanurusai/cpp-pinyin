#ifndef CPP_PINYIN_CONFIG_H
#define CPP_PINYIN_CONFIG_H

/*
 * cpp-pinyin build-time feature switches.
 *
 * Every option can be overridden by a compiler -D before including this file.
 * The defaults target the frozen Mandarin build while preserving the commonly
 * expected traditional-to-simplified behavior.
 */

/* Legacy std::filesystem / std::ifstream dictionary loading and Jyutping data.
 * 0: frozen Mandarin needs no standard file I/O (default)
 * 1: compile the original runtime text-dictionary loader and custom-file APIs
 */
#ifndef CPP_PINYIN_ENABLE_FILE_IO
#define CPP_PINYIN_ENABLE_FILE_IO 0
#endif

/* Rare Plane-3 biang compatibility:
 * U+30EDD (𰻝) and U+30EDE (𰻞 -> 𰻝 when traditional conversion is enabled).
 * Kept off by default so normal builds carry neither the data nor its branch.
 */
#ifndef CPP_PINYIN_ENABLE_BIANG
#define CPP_PINYIN_ENABLE_BIANG 0
#endif

/* Frozen traditional-to-simplified conversion table.
 * Enabled by default to preserve the original public behavior.  Set to 0 on
 * size-constrained targets to compile the whole conversion table/path out.
 */
#ifndef CPP_PINYIN_ENABLE_TRADITIONAL
#define CPP_PINYIN_ENABLE_TRADITIONAL 0
#endif

#endif /* CPP_PINYIN_CONFIG_H */
