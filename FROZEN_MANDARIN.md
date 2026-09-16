# Frozen Mandarin design

This tree was rebuilt from the user-provided original `cpp-pinyin-main.zip` and keeps the upstream public `Pinyin` API largely intact.  The default Mandarin path no longer parses text dictionaries at runtime.

## Central configuration

All source-level feature defaults live in `include/cpp-pinyin/config.h`:

- `CPP_PINYIN_ENABLE_FILE_IO` (default `0`): legacy `std::filesystem` / `std::ifstream` dictionary loading and custom-file APIs.
- `CPP_PINYIN_ENABLE_BIANG` (default `0`): rare Plane-3 `U+30EDD` / `U+30EDE` biang compatibility.
- `CPP_PINYIN_ENABLE_TRADITIONAL` (default `1`): frozen traditional-to-simplified conversion.

When CMake is used, the same names are exposed as CMake options and passed as public compile definitions.  For bare-metal/manual builds, edit `config.h` or pass `-D` overrides.

## 16-bit pinyin format

`FrozenPinyin16` is deliberately byte-oriented:

```
bit 15      EXT
bit 14      final ASCII bit 6
bit 13      ALT
bit 12..8   final ASCII bits 4..0
bit 7..5    tone[2:0]
bit 4..0    initial ASCII bits 4..0
```

The original two Microsoft-shuangpin ASCII bytes are recovered as one `uint16_t` with no shifts:

```cpp
asciiPair = (packed & 0x5F1F) | 0x2040;
```

`ALT` is no longer a one-off `lo/luo` special case.  It is a general second-final selector used by the fragment LUT.  Examples:

- `Uw`: `U -> sh`, `ALT=0`, `w -> ua` => `shua`
- `Xw`: `X -> x`,  `ALT=1`, `w -> ia` => `xia`
- `Lo`: `L -> l`,  `ALT=0`, `o -> o`  => `lo`
- `Lo`: `L -> l`,  `ALT=1`, `o -> uo` => `luo`

The reverse path is a 64-entry initial selector plus a 128-entry final selector.  There is no 2048/4096 whole-syllable reverse table.

The selector values are now **direct byte offsets**, not fragment IDs.  Fragment text is stored in compact one-dimensional NUL-separated pools:

- `kInitialText`: 50 logical bytes (51 including the C literal sentinel)
- `kFinalBase`: 144 logical bytes (145 physical bytes)
- `kToneVowelUtf8`: one flat 48-byte table (`6 vowels * 4 tones * 2 UTF-8 bytes`)

All valid offsets fit in `uint8_t`; the largest generated final offset is 138.  Each `kFinalBase` record is `ASCII final, NUL, metadata`.  The metadata packs `bit0 = marked-vowel position (0/1)` and `bits3..1 = vowel id (a/e/i/o/u/v)`.  `TONE` output copies the base final while replacing that one ASCII vowel with the two bytes selected from the 48-byte table.  Legacy `TONE2` uses the same position bit (`position + 1`), so there is no second per-final table.

## O(1) pinyin rendering

There is no second public style enum.  Both the normal `Pinyin` API and `FrozenPinyin.h` use `ManTone::Style`:

- `ManTone::Style::SHUANGPIN` - direct Microsoft-shuangpin two-byte output
- `ManTone::Style::TONE` - standard tone marks
- `ManTone::Style::TONE3` - trailing tone number
- `ManTone::Style::NORMAL` - no tone
- `ManTone::Style::TONE2` - upstream-compatible embedded tone-number position

`SHUANGPIN` is assigned value 16, leaving the upstream values 0/1/2/8 unchanged.  Tone-mark placement and the marked vowel are precomputed into the metadata byte of each of the 34 final records; runtime tone rendering needs only the base final plus the 48-byte accented-vowel table.
When `CPP_PINYIN_ENABLE_FILE_IO=1`, the same `SHUANGPIN` style is also supported by the legacy text-dictionary path.  That compatibility path reuses the generated 64/128 fragment LUTs for reverse matching and does not add a second 420-entry mapping table.

## Frozen dictionary layout

### Word lookup

The old 0x3000..0x9FFF fully dense array has been replaced by a page hybrid selected from the actual dictionary distribution:

- 72 dense 256-codepoint pages
- 1,463 sparse entries
- one BMP out-of-window entry
- 473 Plane-2 entries
- optional Plane-3 biang entry behind `CPP_PINYIN_ENABLE_BIANG`

Dense pages are O(1); sparse pages perform a small in-page binary search.

### Polyphonic candidates

Only extra readings beyond the default are stored.  The index is 4 bytes per polyphonic character:

```cpp
struct CandidateIndex16 {
    uint16_t codepoint;
    uint16_t desc; // low 12 bits offset, next 3 bits extra-count
};
```

Current generated data:

- 976 indexed characters
- 1,165 extra readings

### Phrase dictionary

Phrase keys remain sorted by 2/3/4-character length for binary lookup, but phrase values no longer store every reading.  A phrase descriptor stores an override-position mask plus an offset into a per-length override pool.  On a match, defaults are fetched from the word LUT and only changed positions are patched.

Current generated data:

- 4,095 two-character phrases / 4,233 overrides
- 1,059 three-character phrases / 1,178 overrides
- 939 four-character phrases / 1,054 overrides
- 6,093 phrases total / 6,465 override readings

### Polyphonic map

The old full-range bitmap is reduced to a page selector plus 32-byte bitmaps only for occupied pages:

- 112-byte page selector
- 2,496-byte bitmap pool

Lookup remains O(1).

### Traditional-to-simplified conversion

When `CPP_PINYIN_ENABLE_TRADITIONAL=1`, mappings are split into four 16-bit pair arrays so no 32+32-bit records are needed:

- BMP -> BMP: 2,997
- BMP -> Plane 2: 364
- Plane 2 -> BMP: 14
- Plane 2 -> Plane 2: 100

The `U+30EDE -> U+30EDD` mapping exists only when both traditional conversion and biang support are enabled.

When traditional conversion is disabled, all four arrays and their lookup code are compiled out.

## Special pronunciation policy

Non-standard syllabic nasal/interjection readings are removed at generation time:

- `嗯: ǹ,en` => default `en`
- `呒: ḿ,wú` => default `wu`
- `呣: ḿ` => removed from the frozen word dictionary
- `噷: hm` => removed from the frozen word dictionary

Thus the target LUT never needs `hm`, `m`, or `n` pseudo-syllables.

## Legacy file mode

The upstream runtime loader is retained under `CPP_PINYIN_ENABLE_FILE_IO`; it was not deleted.  The original unconditional CMake dictionary-copy block is also retained as a block comment next to the gated replacement.

With file I/O enabled, the Mandarin and Cantonese text dictionaries and custom dictionary APIs behave like upstream.  With file I/O disabled, the public headers do not require `<filesystem>`.

## Regeneration

The frozen tables are generated by:

```bash
python tools/generate_frozen_mandarin.py
```

Inputs are the original Mandarin dictionary files plus `tools/new_geping_completed.csv`.
