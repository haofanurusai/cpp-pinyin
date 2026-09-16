#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

BMP_PAGE_BASE = 0x30
BMP_PAGE_COUNT = 0x70      # 0x3000..0x9FFF
WORD_DENSE_THRESHOLD = 169 # measured minimum-size threshold for current word.txt
INVALID_ID = 0xFF

# PackedPinyin16, numeric bit positions:
#   15      EXT flag              (reuses final ASCII bit7)
#   14      final ASCII bit6      (preserved)
#   13      ALT flag              (reuses final ASCII fixed bit5)
#   12..8   final ASCII bit4..0   (preserved)
#   7..5    tone[2:0]             (reuses initial ASCII fixed bits7..5)
#   4..0    initial ASCII bit4..0 (preserved)
# Restoring the two raw shuangpin ASCII bytes as one uint16_t requires only:
#   (packed & 0x5F1F) | 0x2040
ASCII_MASK = 0x5F1F
ASCII_RESTORE = 0x2040
EXT_MASK = 0x8000
ALT_MASK = 0x2000
TONE_MASK = 0x00E0
BIANG_CP = 0x30EDD
BIANG_TRAD_CP = 0x30EDE

INITIAL_BY_BANK = {
    'B':'b','C':'c','D':'d','F':'f','G':'g','H':'h','I':'ch','J':'j','K':'k','L':'l','M':'m','N':'n',
    'O':'','P':'p','Q':'q','R':'r','S':'s','T':'t','U':'sh','V':'zh','W':'w','X':'x','Y':'y','Z':'z',
}
# Chosen so ALT=0 is the common Microsoft-shuangpin interpretation.  ALT=1
# selects the second final fragment without any branch in the decoder.
FINAL_PRIMARY = {
    'd':'uang', 'o':'o', 'r':'uan', 's':'ong', 't':'ue', 'u':'u', 'w':'ua', 'y':'uai',
}

TONE_CHAR_MAP = {
    'ā': ('a', 1), 'á': ('a', 2), 'ǎ': ('a', 3), 'à': ('a', 4),
    'ō': ('o', 1), 'ó': ('o', 2), 'ǒ': ('o', 3), 'ò': ('o', 4),
    'ē': ('e', 1), 'é': ('e', 2), 'ě': ('e', 3), 'è': ('e', 4),
    'ī': ('i', 1), 'í': ('i', 2), 'ǐ': ('i', 3), 'ì': ('i', 4),
    'ū': ('u', 1), 'ú': ('u', 2), 'ǔ': ('u', 3), 'ù': ('u', 4),
    'ǖ': ('v', 1), 'ǘ': ('v', 2), 'ǚ': ('v', 3), 'ǜ': ('v', 4),
    'ü': ('v', 5), 'ń': ('n', 2), 'ň': ('n', 3), 'ǹ': ('n', 4), 'ḿ': ('m', 2),
}
TONE_VOWELS = {
    'a': ['', 'ā', 'á', 'ǎ', 'à'], 'o': ['', 'ō', 'ó', 'ǒ', 'ò'],
    'e': ['', 'ē', 'é', 'ě', 'è'], 'i': ['', 'ī', 'í', 'ǐ', 'ì'],
    'u': ['', 'ū', 'ú', 'ǔ', 'ù'], 'v': ['', 'ǖ', 'ǘ', 'ǚ', 'ǜ'],
}


def split_tone(token: str) -> tuple[str, int]:
    tone = 5
    out = []
    for ch in token.strip():
        if ch in TONE_CHAR_MAP:
            base, tone = TONE_CHAR_MAP[ch]
            out.append(base)
        elif '1' <= ch <= '5':
            tone = int(ch)
        else:
            out.append(ch)
    base = ''.join(out).replace('ü', 'v')
    if base == 'lue': base = 'lve'
    if base == 'nue': base = 'nve'
    return base, tone


def tone_mark_index(base: str) -> int:
    if 'a' in base: return base.index('a')
    if 'e' in base: return base.index('e')
    if 'ou' in base: return base.index('o')
    if 'iu' in base: return base.index('u')
    if 'ui' in base: return base.index('i')
    for i in range(len(base) - 1, -1, -1):
        if base[i] in 'aiouv': return i
    return max(0, len(base) - 1)


def tone_marked(base: str, tone: int) -> str:
    if tone < 1 or tone > 4: return base
    i = tone_mark_index(base)
    c = base[i]
    if c not in TONE_VOWELS: return base
    return base[:i] + TONE_VOWELS[c][tone] + base[i+1:]


def cstr(s: str) -> str:
    out = '"'
    for b in s.encode('utf-8'):
        if b == 0x22: out += '\\"'
        elif b == 0x5C: out += '\\\\'
        elif 0x20 <= b < 0x7f: out += chr(b)
        else: out += f'\\{b:03o}'
    return out + '"'



def cbytes(data: bytes) -> str:
    """Emit one C string literal preserving embedded NUL/metadata bytes."""
    out = '"'
    for b in data:
        if b == 0x22: out += '\"'
        elif b == 0x5C: out += '\\\\'
        elif 0x20 <= b < 0x7f: out += chr(b)
        else: out += f'\\{b:03o}'
    return out + '"'


def make_nul_pool(strings):
    pool = bytearray()
    offsets = {}
    for text in strings:
        offsets[text] = len(pool)
        pool += text.encode('utf-8') + b'\0'
    if len(pool) >= 0xFF:
        raise ValueError(f'NUL pool too large for uint8_t offset: {len(pool)}')
    return bytes(pool), offsets

def parse_mapping(path: Path):
    rows = list(csv.DictReader(path.open(encoding='ascii', newline='')))
    aliases = {}
    details = {}
    final_variants = defaultdict(set)
    for r in rows:
        alias, bank, final = r['alias'], r['midi_bank_char'], r['midi_prog_char']
        if len(bank) != 1 or len(final) != 1 or ord(bank) >= 128 or ord(final) >= 128:
            raise ValueError(r)
        if (ord(bank) & 0xE0) != 0x40:
            raise ValueError(f'bank must match ASCII 010xxxxx: {r}')
        if (ord(final) & 0xA0) != 0x20:
            raise ValueError(f'final must have ASCII bit7=0 and bit5=1: {r}')
        initial_frag = INITIAL_BY_BANK[bank]
        # The spelling "v" shares Yu with "yu", but is a zero-initial ü syllable.
        if alias == 'v': initial_frag = ''
        if not alias.startswith(initial_frag):
            raise ValueError(f'Cannot split {alias} using bank {bank}->{initial_frag}')
        final_frag = alias[len(initial_frag):]
        final_variants[final].add(final_frag)
        aliases[alias] = (bank, final)
        details[alias] = [initial_frag, final_frag, None]

    for key, variants in final_variants.items():
        if len(variants) > 2:
            raise ValueError(f'final key {key} has >2 variants: {variants}')
        variants = sorted(variants)
        if len(variants) == 1:
            primary = variants[0]
        else:
            primary = FINAL_PRIMARY.get(key)
            if primary not in variants:
                raise ValueError(f'No primary selected for {key}: {variants}')
        for alias, (bank, final) in aliases.items():
            if final != key: continue
            frag = details[alias][1]
            details[alias][2] = 0 if frag == primary else 1

    # Build 64 initial-fragment and 128 final-fragment selector tables.
    initial_slots = [None] * 64
    final_slots = [None] * 128
    for alias, (bank, final) in aliases.items():
        ini, fin, alt = details[alias]
        ii = (ord(bank) & 0x1f) | (alt << 5)
        fi = (ord(final) & 0x5f) | (alt << 5)  # bit5 was fixed 1; now ALT occupies it
        if initial_slots[ii] not in (None, ini):
            raise ValueError(f'initial slot collision {ii}: {initial_slots[ii]} vs {ini} ({alias})')
        if final_slots[fi] not in (None, fin):
            raise ValueError(f'final slot collision {fi}: {final_slots[fi]} vs {fin} ({alias})')
        initial_slots[ii] = ini
        final_slots[fi] = fin

    initials = sorted({x for x in initial_slots if x is not None}, key=lambda x:(len(x),x))
    finals = sorted({x for x in final_slots if x is not None}, key=lambda x:(len(x),x))
    initial_id = {s:i for i,s in enumerate(initials)}
    final_id = {s:i for i,s in enumerate(finals)}
    initial_lut = [INVALID_ID if s is None else initial_id[s] for s in initial_slots]
    final_lut = [INVALID_ID if s is None else final_id[s] for s in final_slots]

    # Exhaustively verify three-segment reconstruction.
    for alias,(bank,final) in aliases.items():
        alt = details[alias][2]
        ii = (ord(bank)&0x1f)|(alt<<5)
        fi = (ord(final)&0x5f)|(alt<<5)
        rebuilt = initials[initial_lut[ii]] + finals[final_lut[fi]]
        if rebuilt != alias:
            raise ValueError(f'rebuild {alias} -> {rebuilt}')
    return aliases, details, initials, finals, initial_lut, final_lut


def pack(alias: str, tone: int, ext: bool, aliases, details) -> int:
    bank, final = aliases[alias]
    alt = details[alias][2]
    low = (ord(bank) & 0x1f) | ((tone & 7) << 5)
    high = ord(final) & 0x5f
    if alt: high |= 0x20
    if ext: high |= 0x80
    return low | (high << 8)


def parse_word(path: Path, aliases, details):
    words = {}
    dropped = []
    bad = []
    for ln,line in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        if ':' not in line: continue
        k,v = line.split(':',1)
        cp = ord(k[0]); vals=[]
        for tok in v.split(','):
            base,tone=split_tone(tok)
            if base in ('hm','m','n'): continue
            if base not in aliases:
                bad.append((ln,k,tok,base)); continue
            vals.append(pack(base,tone,cp>0xffff,aliases,details))
        if vals: words[cp]=vals
        else: dropped.append((cp,k,v))
    if bad: raise ValueError(f'unmapped word tokens: {bad[:10]}')
    return words,dropped


def parse_phrase(path: Path, user: bool, aliases, details):
    out={}; bad=[]
    for ln,line in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        if ':' not in line: continue
        k,v=line.split(':',1); cps=tuple(map(ord,k))
        toks=[x for x in v.strip().split(' ' if user else ',') if x]
        if not (2<=len(cps)<=4) or len(toks)!=len(cps):
            raise ValueError(f'bad phrase {path}:{ln}:{k}')
        vals=[]
        for cp,tok in zip(cps,toks):
            base,tone=split_tone(tok)
            if base in ('hm','m','n') or base not in aliases:
                bad.append((ln,k,tok,base)); break
            vals.append(pack(base,tone,cp>0xffff,aliases,details))
        else: out[cps]=vals
    if bad: raise ValueError(f'unmapped phrase tokens: {bad[:10]}')
    return out


def parse_poly(path: Path):
    return sorted({ord(line.split(':',1)[0][0]) for line in path.read_text(encoding='utf-8').splitlines() if ':' in line})


def parse_trans(path: Path):
    d={}
    for line in path.read_text(encoding='utf-8').splitlines():
        if ':' not in line: continue
        k,v=line.split(':',1)
        if k and v: d[ord(k[0])]=ord(v[0])
    return sorted(d.items())


def phrase_key(cps):
    if any(cp>0xffff for cp in cps): raise ValueError(f'non-BMP phrase key {cps}')
    x=0
    for cp in cps: x=(x<<16)|cp
    return x


def fmt(vals, per=12, fn=str):
    return '\n'.join('    '+', '.join(fn(x) for x in vals[i:i+per])+',' for i in range(0,len(vals),per))


def build(dict_dir: Path, mapping: Path, out_h: Path, out_cpp: Path):
    aliases, details, initials, finals, initial_lut, final_lut = parse_mapping(mapping)
    words,dropped=parse_word(dict_dir/'word.txt',aliases,details)
    phrases=parse_phrase(dict_dir/'phrases_dict.txt',False,aliases,details)
    phrases.update(parse_phrase(dict_dir/'user_dict.txt',True,aliases,details)) # legacy override semantics
    poly=parse_poly(dict_dir/'phrases_map.txt')
    trans=parse_trans(dict_dir/'trans_word.txt')

    # BIANG is isolated so default builds contain neither a Plane-3 branch nor array entry.
    biang_pinyin = words.pop(BIANG_CP, [0])[0]
    if not biang_pinyin: raise ValueError('expected U+30EDD in word.txt')
    words.pop(BIANG_TRAD_CP, None)

    # Word page hybrid: dense pages are direct 256-entry arrays; sparse pages keep low8+value.
    pages=defaultdict(dict)
    extra=[]; ext=[]
    for cp,vals in sorted(words.items()):
        py=vals[0]
        if 0x3000<=cp<0xA000:
            pages[cp>>8][cp&0xff]=py
        elif cp<=0xffff: extra.append((cp,py))
        elif (cp>>16)==2: ext.append((cp&0xffff,py))
        else: raise ValueError(f'unexpected non-Plane2 word U+{cp:X}')
    page_off=[]; page_count=[]; dense=[]; sparse_low=[]; sparse_py=[]
    dense_pages=0; sparse_entries=0
    for page in range(BMP_PAGE_BASE,BMP_PAGE_BASE+BMP_PAGE_COUNT):
        items=sorted(pages.get(page,{}).items())
        if len(items)>=WORD_DENSE_THRESHOLD:
            off=len(dense); page_off.append(0x8000|off); page_count.append(0); dense_pages+=1
            row=[0]*256
            for lo,py in items: row[lo]=py
            dense.extend(row)
        else:
            off=len(sparse_low); page_off.append(off); page_count.append(len(items)); sparse_entries+=len(items)
            for lo,py in items: sparse_low.append(lo); sparse_py.append(py)
    if max(page_off,default=0)&0x7fff >=0x8000: raise ValueError('page offset overflow')

    # Candidates: index only multi-pronunciation BMP chars and store only alternatives after default.
    cand_index=[]; cand_pool=[]
    for cp,vals in sorted(words.items()):
        if cp>0xffff or len(vals)<=1: continue
        extras=vals[1:]
        if not extras: continue
        if len(cand_pool)>=4096 or len(extras)>7: raise ValueError('candidate descriptor overflow')
        desc=len(cand_pool)|(len(extras)<<12)
        cand_index.append((cp,desc)); cand_pool.extend(extras)

    # Compact polyphonic page bitmap: 1 byte page->bitmap-block+1, only occupied pages get 32 bytes.
    poly_by_page=defaultdict(list)
    for cp in poly:
        if not 0x3000<=cp<0xA000: raise ValueError(f'poly outside BMP window U+{cp:X}')
        poly_by_page[cp>>8].append(cp&0xff)
    poly_page=[0]*BMP_PAGE_COUNT; poly_bits=[]
    block=1
    for page in range(BMP_PAGE_BASE,BMP_PAGE_BASE+BMP_PAGE_COUNT):
        lows=poly_by_page.get(page)
        if not lows: continue
        poly_page[page-BMP_PAGE_BASE]=block; block+=1
        bits=[0]*32
        for lo in lows: bits[lo>>3]|=1<<(lo&7)
        poly_bits.extend(bits)

    # Phrase descriptors: defaults come from word LUT; pool stores changed positions only.
    pkeys={2:[],3:[],4:[]}; pdesc={2:[],3:[],4:[]}; povr={2:[],3:[],4:[]}
    mask_shift={2:13,3:11,4:11}; offset_bits={2:13,3:11,4:11}
    for cps,vals in sorted(phrases.items(), key=lambda kv:(len(kv[0]),kv[0])):
        n=len(cps); defaults=[]
        for cp in cps:
            if cp==BIANG_CP: d=biang_pinyin
            else:
                vv=words.get(cp)
                if not vv: raise ValueError(f'phrase char missing word U+{cp:X}')
                d=vv[0]
            defaults.append(d)
        mask=0; changed=[]
        for i,(a,b) in enumerate(zip(defaults,vals)):
            if a!=b: mask|=1<<i; changed.append(b)
        off=len(povr[n])
        if off >= (1<<offset_bits[n]): raise ValueError(f'phrase{n} override offset overflow')
        desc=off|(mask<<mask_shift[n])
        pkeys[n].append(phrase_key(cps)); pdesc[n].append(desc); povr[n].extend(changed)

    # Traditional conversion split by source/target plane so every record stays 4 bytes.
    bb=[]; be=[]; eb=[]; ee=[]; biang_trans=False
    for a,b in trans:
        if a in (BIANG_CP,BIANG_TRAD_CP) or b in (BIANG_CP,BIANG_TRAD_CP):
            if a==BIANG_TRAD_CP and b==BIANG_CP: biang_trans=True; continue
            raise ValueError(f'unexpected Plane3 trans U+{a:X}->U+{b:X}')
        aext=a>0xffff; bext=b>0xffff
        if aext and (a>>16)!=2: raise ValueError(hex(a))
        if bext and (b>>16)!=2: raise ValueError(hex(b))
        pair=(a&0xffff,b&0xffff)
        (ee if aext and bext else eb if aext else be if bext else bb).append(pair)
    if not biang_trans: raise ValueError('expected U+30EDE->U+30EDD')

    # Fragment text pools. Selector LUTs store direct byte offsets.
    initial_pool, initial_offset = make_nul_pool(initials)
    initial_lut = [INVALID_ID if x == INVALID_ID else initial_offset[initials[x]] for x in initial_lut]

    # Final records are kept in one NUL-separated byte pool.  The metadata
    # byte immediately after each NUL packs everything needed by both TONE
    # and legacy TONE2 without any per-final tone strings:
    #   bit0     : tone-mark character position (all current finals use 0/1)
    #   bits3..1 : marked-vowel id (a/e/i/o/u/v -> 0..5)
    #   bits7..4 : reserved
    # The actual accented UTF-8 vowel is selected from a flat 6*4*2 = 48 B
    # table at runtime.  kFinalId therefore remains a direct uint8 byte
    # offset into kFinalBase.
    tone_vowel_order = 'aeiouv'
    tone_vowel_id = {ch:i for i,ch in enumerate(tone_vowel_order)}
    final_base_pool = bytearray()
    final_offset = {}
    for text in finals:
        off = len(final_base_pool)
        final_offset[text] = off
        mark_pos = tone_mark_index(text)
        if mark_pos > 1:
            raise ValueError(f'tone mark position no longer fits 1 bit: {text} -> {mark_pos}')
        mark_vowel = text[mark_pos]
        if mark_vowel not in tone_vowel_id:
            raise ValueError(f'unsupported tone vowel in final: {text} -> {mark_vowel}')
        meta = mark_pos | (tone_vowel_id[mark_vowel] << 1)
        final_base_pool += text.encode('ascii') + b'\0' + bytes((meta,))
    if len(final_base_pool) >= 0xFF:
        raise ValueError(f'final pool too large for uint8_t offset: {len(final_base_pool)}')

    tone_vowel_utf8 = bytearray()
    for vowel in tone_vowel_order:
        for tone in range(1,5):
            encoded = TONE_VOWELS[vowel][tone].encode('utf-8')
            if len(encoded) != 2:
                raise ValueError(f'accented vowel must be 2-byte UTF-8: {vowel}{tone}')
            tone_vowel_utf8 += encoded
    if len(tone_vowel_utf8) != 48:
        raise ValueError(f'expected 48-byte tone vowel table, got {len(tone_vowel_utf8)}')

    final_lut = [INVALID_ID if x == INVALID_ID else final_offset[finals[x]] for x in final_lut]

    h=f'''// Generated by tools/generate_frozen_mandarin.py. DO NOT EDIT BY HAND.\n#ifndef CPP_PINYIN_FROZEN_MANDARIN_DATA_H\n#define CPP_PINYIN_FROZEN_MANDARIN_DATA_H\n\n#include <cstddef>\n#include <cstdint>\n#include <cpp-pinyin/PinyinGlobal.h>\n\nnamespace Pinyin {{ namespace FrozenData {{\nusing PackedPinyin16 = std::uint16_t;\nconstexpr std::uint16_t kAsciiPairMask=0x{ASCII_MASK:04X}u;\nconstexpr std::uint16_t kAsciiPairRestore=0x{ASCII_RESTORE:04X}u;\nconstexpr std::uint16_t kExtMask=0x{EXT_MASK:04X}u;\nconstexpr std::uint16_t kAltMask=0x{ALT_MASK:04X}u;\nconstexpr std::uint16_t kToneMask=0x{TONE_MASK:04X}u;\nconstexpr std::uint8_t kBmpPageBase=0x{BMP_PAGE_BASE:02X}u;\nconstexpr std::uint8_t kBmpPageCount=0x{BMP_PAGE_COUNT:02X}u;\nconstexpr std::uint16_t kDensePageMask=0x8000u;\nconstexpr std::uint8_t kInvalidFragment=0xFFu;\n\nstruct WordPair16 {{ std::uint16_t key; PackedPinyin16 pinyin; }};\nstruct CandidateIndex16 {{ std::uint16_t codepoint; std::uint16_t desc; }};\nstruct Pair16 {{ std::uint16_t from; std::uint16_t to; }};\n\nextern const std::uint16_t kWordPageOffset[{BMP_PAGE_COUNT}];\nextern const std::uint8_t kWordPageCount[{BMP_PAGE_COUNT}];\nextern const PackedPinyin16 kWordDense[{len(dense)}];\nextern const std::uint8_t kWordSparseLow[{len(sparse_low)}];\nextern const PackedPinyin16 kWordSparsePinyin[{len(sparse_py)}];\nextern const WordPair16 kWordExtra[{len(extra)}];\nextern const WordPair16 kWordExt[{len(ext)}];\n#if CPP_PINYIN_ENABLE_BIANG\nextern const PackedPinyin16 kBiangPinyin;\n#endif\n\nextern const CandidateIndex16 kCandidateIndex[{len(cand_index)}];\nextern const PackedPinyin16 kCandidatePool[{len(cand_pool)}];\nextern const std::uint8_t kPolyPage[{len(poly_page)}];\nextern const std::uint8_t kPolyBitmap[{len(poly_bits)}];\n\nextern const std::uint32_t kPhrase2Keys[{len(pkeys[2])}];\nextern const std::uint16_t kPhrase2Desc[{len(pdesc[2])}];\nextern const PackedPinyin16 kPhrase2Override[{len(povr[2])}];\nextern const std::uint64_t kPhrase3Keys[{len(pkeys[3])}];\nextern const std::uint16_t kPhrase3Desc[{len(pdesc[3])}];\nextern const PackedPinyin16 kPhrase3Override[{len(povr[3])}];\nextern const std::uint64_t kPhrase4Keys[{len(pkeys[4])}];\nextern const std::uint16_t kPhrase4Desc[{len(pdesc[4])}];\nextern const PackedPinyin16 kPhrase4Override[{len(povr[4])}];\n\n#if CPP_PINYIN_ENABLE_TRADITIONAL\nextern const Pair16 kTransBmpBmp[{len(bb)}];\nextern const Pair16 kTransBmpExt[{len(be)}];\nextern const Pair16 kTransExtBmp[{len(eb)}];\nextern const Pair16 kTransExtExt[{len(ee)}];\n#endif\n\nextern const std::uint8_t kInitialId[64]; // direct byte offset into kInitialText\nextern const std::uint8_t kFinalId[128];  // direct byte offset shared by all final pools\nextern const char kInitialText[{len(initial_pool)+1}];\nextern const char kFinalBase[{len(final_base_pool)+1}];\nextern const std::uint8_t kToneVowelUtf8[48];\n\n/* Legacy tone-string layouts, retained for reference only:\nextern const char kInitialText[kInitialCount][3];\nextern const char kFinalBase[kFinalCount][5];\nextern const std::uint8_t kFinalTone2Position[kFinalCount];\nextern const char kFinalToneMarked[kFinalCount][4][6];\nextern const char kFinalToneMarked1[145];\nextern const char kFinalToneMarked2[145];\nextern const char kFinalToneMarked3[145];\nextern const char kFinalToneMarked4[145];\n*/\n\nconstexpr std::size_t kWordExtraCount={len(extra)};\nconstexpr std::size_t kWordExtCount={len(ext)};\nconstexpr std::size_t kCandidateIndexCount={len(cand_index)};\nconstexpr std::size_t kPhrase2Count={len(pkeys[2])};\nconstexpr std::size_t kPhrase3Count={len(pkeys[3])};\nconstexpr std::size_t kPhrase4Count={len(pkeys[4])};\n#if CPP_PINYIN_ENABLE_TRADITIONAL\nconstexpr std::size_t kTransBmpBmpCount={len(bb)};\nconstexpr std::size_t kTransBmpExtCount={len(be)};\nconstexpr std::size_t kTransExtBmpCount={len(eb)};\nconstexpr std::size_t kTransExtExtCount={len(ee)};\n#endif\nconstexpr std::size_t kInitialCount={len(initials)};\nconstexpr std::size_t kFinalCount={len(finals)};\n\n}} }}\n#endif\n'''
    out_h.write_text(h,encoding='utf-8')

    c=[]
    c.append('// Generated by tools/generate_frozen_mandarin.py. DO NOT EDIT BY HAND.\n#include "FrozenMandarinData.h"\n\nnamespace Pinyin { namespace FrozenData {\n')
    c.append(f'const std::uint16_t kWordPageOffset[{len(page_off)}]={{\n{fmt(page_off,12,lambda x:f"0x{x:04X}u")}\n}};\n')
    c.append(f'const std::uint8_t kWordPageCount[{len(page_count)}]={{\n{fmt(page_count,16,lambda x:f"{x}u")}\n}};\n')
    c.append(f'const PackedPinyin16 kWordDense[{len(dense)}]={{\n{fmt(dense,12,lambda x:f"0x{x:04X}u")}\n}};\n')
    c.append(f'const std::uint8_t kWordSparseLow[{len(sparse_low)}]={{\n{fmt(sparse_low,16,lambda x:f"0x{x:02X}u")}\n}};\n')
    c.append(f'const PackedPinyin16 kWordSparsePinyin[{len(sparse_py)}]={{\n{fmt(sparse_py,12,lambda x:f"0x{x:04X}u")}\n}};\n')
    for name,data in [('kWordExtra',extra),('kWordExt',ext)]:
        c.append(f'const WordPair16 {name}[{len(data)}]={{\n')
        for a,b in data:c.append(f'    {{0x{a:04X}u,0x{b:04X}u}},\n')
        c.append('};\n')
    c.append(f'#if CPP_PINYIN_ENABLE_BIANG\nconst PackedPinyin16 kBiangPinyin=0x{biang_pinyin:04X}u;\n#endif\n')
    c.append(f'const CandidateIndex16 kCandidateIndex[{len(cand_index)}]={{\n')
    for cp,desc in cand_index:c.append(f'    {{0x{cp:04X}u,0x{desc:04X}u}},\n')
    c.append('};\n')
    c.append(f'const PackedPinyin16 kCandidatePool[{len(cand_pool)}]={{\n{fmt(cand_pool,12,lambda x:f"0x{x:04X}u")}\n}};\n')
    c.append(f'const std::uint8_t kPolyPage[{len(poly_page)}]={{\n{fmt(poly_page,16,lambda x:f"{x}u")}\n}};\n')
    c.append(f'const std::uint8_t kPolyBitmap[{len(poly_bits)}]={{\n{fmt(poly_bits,16,lambda x:f"0x{x:02X}u")}\n}};\n')
    for n,kt,suf,w in [(2,'std::uint32_t','u',8),(3,'std::uint64_t','ull',12),(4,'std::uint64_t','ull',16)]:
        c.append(f'const {kt} kPhrase{n}Keys[{len(pkeys[n])}]={{\n{fmt(pkeys[n],6,lambda x,w=w,s=suf:f"0x{x:0{w}X}{s}")}\n}};\n')
        c.append(f'const std::uint16_t kPhrase{n}Desc[{len(pdesc[n])}]={{\n{fmt(pdesc[n],12,lambda x:f"0x{x:04X}u")}\n}};\n')
        c.append(f'const PackedPinyin16 kPhrase{n}Override[{len(povr[n])}]={{\n{fmt(povr[n],12,lambda x:f"0x{x:04X}u")}\n}};\n')
    c.append('#if CPP_PINYIN_ENABLE_TRADITIONAL\n')
    for name,data in [('kTransBmpBmp',bb),('kTransBmpExt',be),('kTransExtBmp',eb),('kTransExtExt',ee)]:
        c.append(f'const Pair16 {name}[{len(data)}]={{\n')
        for a,b in data:c.append(f'    {{0x{a:04X}u,0x{b:04X}u}},\n')
        c.append('};\n')
    c.append('#endif\n')
    c.append(f'const std::uint8_t kInitialId[64]={{\n{fmt(initial_lut,16,lambda x:"0xFFu" if x==255 else f"{x}u")}\n}};\n')
    c.append(f'const std::uint8_t kFinalId[128]={{\n{fmt(final_lut,16,lambda x:"0xFFu" if x==255 else f"{x}u")}\n}};\n')
    c.append(f'const char kInitialText[{len(initial_pool)+1}]={cbytes(initial_pool)};\n')
    c.append(f'const char kFinalBase[{len(final_base_pool)+1}]={cbytes(bytes(final_base_pool))};\n')
    c.append(f'const std::uint8_t kToneVowelUtf8[48]={{\n{fmt(tone_vowel_utf8,16,lambda x:f"0x{x:02X}u")}\n}};\n')
    c.append('\n} }\n')
    out_cpp.write_text(''.join(c),encoding='utf-8')

    print('generated',out_h,out_cpp)
    print(f'words={len(words)+1} dense_pages={dense_pages} dense_values={len(dense)} sparse={sparse_entries} ext={len(ext)} extra={len(extra)}')
    print(f'candidates index={len(cand_index)} extra_pool={len(cand_pool)} poly={len(poly)} poly_bitmap={len(poly_bits)}')
    print('phrases',len(phrases),'counts',','.join(f'{n}:{len(pkeys[n])}/{len(povr[n])}ovr' for n in (2,3,4)))
    print(f'trans bb={len(bb)} be={len(be)} eb={len(eb)} ee={len(ee)} + optional biang')
    print(f'fragments initial={len(initials)} final={len(finals)} pools initial={len(initial_pool)} final={len(final_base_pool)} tone_vowels={len(tone_vowel_utf8)}')
    print('dropped',[(x[1],x[2]) for x in dropped])


def main():
    ap=argparse.ArgumentParser(); here=Path(__file__).resolve().parent; root=here.parent
    ap.add_argument('--dict-dir',type=Path,default=root/'res/dict/mandarin')
    ap.add_argument('--mapping',type=Path,default=here/'new_geping_completed.csv')
    ap.add_argument('--out-h',type=Path,default=root/'src/frozen/FrozenMandarinData.h')
    ap.add_argument('--out-cpp',type=Path,default=root/'src/frozen/FrozenMandarinData.cpp')
    a=ap.parse_args(); build(a.dict_dir,a.mapping,a.out_h,a.out_cpp)
if __name__=='__main__':main()
