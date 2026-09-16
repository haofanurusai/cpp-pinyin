# Validation

Source archive used as the clean base:

```
cpp-pinyin-main.zip
SHA-256: 7fdd9c87a1b678fc66936f4db0264dd0b495a6932afeecd29e0774f11fedf147
```

## Generated dictionary statistics

```
valid frozen word entries: 17,500 (including optional biang source entry before compile-time gating)
dense word pages:          72
sparse word entries:       1,463
Plane-2 word entries:      473
polyphonic indices:        976
extra candidate readings:  1,165
phrase count:              6,093
phrase override readings:  6,465
initial fragments:         24
final fragments:           34
initial text pool:          50 logical / 51 physical bytes
final base pool:            144 logical / 145 physical bytes
accented-vowel table:       48 bytes
maximum final offset:       138
```

The only dropped word entries are `呣` (only `m`) and `噷` (only `hm`).  `嗯` and `呒` retain their standard alternatives as defaults.

## Upstream Mandarin benchmark, frozen mode

Default configuration (`FILE_IO=0`, `BIANG=0`, `TRADITIONAL=1`):

```
toneBatchTest:
  total sentences: 79,117
  errors:          7,693

unToneBatchTest:
  total characters: 192,693
  errors:            108
```

These are the same baseline counts as the original file-backed library.

## File-I/O compatibility mode

`CPP_PINYIN_ENABLE_FILE_IO=1`, traditional conversion enabled:

```
Mandarin tone:      7,693 / 79,117
Mandarin no-tone:     108 / 192,693
Cantonese tone:     4,726 / 93,451
Cantonese no-tone:  1,764 / 93,451
```

## Three-segment reverse LUT

All 420 aliases from `new_geping_completed.csv` were tested at tones 1..5:

```
420 aliases * 5 tones = 2,100 packed values
ManTone::Style::SHUANGPIN: PASS
ManTone::Style::TONE:       PASS
ManTone::Style::TONE3:      PASS
ManTone::Style::NORMAL:     PASS
failures:        0
```

## Feature build matrix

The following configurations compile successfully:

```
FILE_IO=0, TRADITIONAL=1, BIANG=0  (default)
FILE_IO=0, TRADITIONAL=0, BIANG=0
FILE_IO=0, TRADITIONAL=1, BIANG=1
FILE_IO=1, TRADITIONAL=1, BIANG=0
FILE_IO=1, TRADITIONAL=0, BIANG=0
```

Behavior checks:

```
BIANG=0:  𰻝 / 𰻞 pass through as unknown
BIANG=1:  𰻝 -> biang2; 𰻞 -> biang2 when traditional conversion is enabled
TRADITIONAL=0: tradToSim("臺") -> "臺"
TRADITIONAL=1: tradToSim("臺") -> "台"
```

## Frozen read-only data size

Measured from `FrozenMandarinData.cpp.o`, GCC 14.2, Release, x86-64:

```
TRADITIONAL=1, BIANG=0: 124,416 bytes (121.5 KiB)
TRADITIONAL=0, BIANG=0: 110,464 bytes (107.9 KiB)
TRADITIONAL=1, BIANG=1: 124,448 bytes (121.5 KiB)
```

The prior whole-syllable frozen design was about 210 KiB of read-only generated data; the main reductions come from fragment-based full-pinyin reconstruction, phrase overrides, compact candidates, compact traditional pairs, and the word-page hybrid.

## Unified style enum

The public Frozen renderer no longer exposes a second style enum. `ManTone::Style` is the only compiled output-style type and keeps the upstream numeric values unchanged:

```text
NORMAL    = 0
TONE      = 1
TONE2     = 2
TONE3     = 8
SHUANGPIN = 16
```

`getDefaultPinyin("中", SHUANGPIN)` and `hanziToPinyin("中国", SHUANGPIN)` were verified in both the default frozen build and `CPP_PINYIN_ENABLE_FILE_IO=1` build. The 2,100 packed-value exhaustive renderer test also uses `ManTone::Style` directly and reports zero failures.

## Direct-offset fragment-pool validation

The former fixed-stride fragment storage occupied 1,284 bytes across selector/text/tone tables.  After flattening the fragment strings and replacing the four complete tone-mark pools with the 48-byte accented-vowel table, the active fragment data is 436 raw bytes (`64 + 128 + 51 + 145 + 48`), saving 848 raw bytes from that original layout.

Relative to the immediately preceding direct-offset/four-tone-pool version, `FrozenMandarinData.cpp.o` `.rodata` fell from 124,992 to 124,416 bytes.  The renderer code grew slightly because tone-marked finals are assembled from the base spelling; the generated tone-string tables are gone.

The full renderer was rechecked against the preceding version over all 420 aliases at tones 1..5 (2,100 packed values): `NORMAL`, `TONE`, `TONE3`, and `SHUANGPIN` are byte-for-byte identical.  Default frozen and `FILE_IO=1` upstream benchmarks remain unchanged.
