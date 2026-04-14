/*
 * utf8.c - UTF-8 decoder with CP437 mapping
 *
 * Maps Unicode codepoints to the best available CP437 equivalent.
 * Latin-1 Supplement (U+0080-U+00FF) via lookup table.
 * Common punctuation/symbols via switch.
 * Everything else maps to '?'.
 */

#include "utf8.h"

/*
 * Latin-1 Supplement mapping table (U+0080 to U+00FF).
 * Index = codepoint - 0x80. Value = CP437 character.
 */
static const unsigned char far lat1_to_cp437[128] = {
    '?', '?', '?', '?', '?', '?', '?', '?',   /* 80-87: C1 controls */
    '?', '?', '?', '?', '?', '?', '?', '?',   /* 88-8F */
    '?', '?', '?', '?', '?', '?', '?', '?',   /* 90-97 */
    '?', '?', '?', '?', '?', '?', '?', '?',   /* 98-9F */
    ' ',                                         /* A0: nbsp */
    0xAD,                                        /* A1: inverted ! */
    0x9B,                                        /* A2: cent */
    0x9C,                                        /* A3: pound */
    '?',                                         /* A4: currency */
    0x9D,                                        /* A5: yen */
    '|',                                         /* A6: broken bar */
    0x15,                                        /* A7: section */
    '"',                                         /* A8: diaeresis */
    'c',                                         /* A9: copyright */
    0xA6,                                        /* AA: fem ordinal */
    0xAE,                                        /* AB: left guillemet */
    0xAA,                                        /* AC: not sign */
    '-',                                         /* AD: soft hyphen */
    'R',                                         /* AE: registered */
    '?',                                         /* AF: macron */
    0xF8,                                        /* B0: degree */
    0xF1,                                        /* B1: plus-minus */
    0xFD,                                        /* B2: superscript 2 */
    '3',                                         /* B3: superscript 3 */
    '\'',                                        /* B4: acute accent */
    0xE6,                                        /* B5: micro */
    0x14,                                        /* B6: pilcrow */
    0xFA,                                        /* B7: middle dot */
    ',',                                         /* B8: cedilla */
    '1',                                         /* B9: superscript 1 */
    0xA7,                                        /* BA: masc ordinal */
    0xAF,                                        /* BB: right guillemet */
    0xAC,                                        /* BC: 1/4 */
    0xAB,                                        /* BD: 1/2 */
    '?',                                         /* BE: 3/4 */
    0xA8,                                        /* BF: inverted ? */
    /* C0-CF: accented capitals */
    'A', 'A', 'A', 'A',
    0x8E,                                        /* C4: A-umlaut */
    0x8F,                                        /* C5: A-ring */
    0x92,                                        /* C6: AE ligature */
    0x80,                                        /* C7: C-cedilla */
    'E', 0x90, 'E', 'E',                        /* C8-CB */
    'I', 'I', 'I', 'I',                         /* CC-CF */
    /* D0-DF */
    'D',                                         /* D0: Eth */
    0xA5,                                        /* D1: N-tilde */
    'O', 'O', 'O', 'O',
    0x99,                                        /* D6: O-umlaut */
    'x',                                         /* D7: multiply */
    'O',                                         /* D8: O-stroke */
    'U', 'U', 'U',
    0x9A,                                        /* DC: U-umlaut */
    'Y',                                         /* DD */
    '?',                                         /* DE: Thorn */
    0xE1,                                        /* DF: sharp s */
    /* E0-EF: accented lowercase */
    0x85,                                        /* E0: a-grave */
    0xA0,                                        /* E1: a-acute */
    0x83,                                        /* E2: a-circumflex */
    'a',                                         /* E3: a-tilde */
    0x84,                                        /* E4: a-umlaut */
    0x86,                                        /* E5: a-ring */
    0x91,                                        /* E6: ae ligature */
    0x87,                                        /* E7: c-cedilla */
    0x8A,                                        /* E8: e-grave */
    0x82,                                        /* E9: e-acute */
    0x88,                                        /* EA: e-circumflex */
    0x89,                                        /* EB: e-umlaut */
    0x8D,                                        /* EC: i-grave */
    0xA1,                                        /* ED: i-acute */
    0x8C,                                        /* EE: i-circumflex */
    0x8B,                                        /* EF: i-umlaut */
    /* F0-FF */
    '?',                                         /* F0: eth */
    0xA4,                                        /* F1: n-tilde */
    0x95,                                        /* F2: o-grave */
    0xA2,                                        /* F3: o-acute */
    0x93,                                        /* F4: o-circumflex */
    'o',                                         /* F5: o-tilde */
    0x94,                                        /* F6: o-umlaut */
    0xF6,                                        /* F7: division */
    'o',                                         /* F8: o-stroke */
    0x97,                                        /* F9: u-grave */
    0xA3,                                        /* FA: u-acute */
    0x96,                                        /* FB: u-circumflex */
    0x81,                                        /* FC: u-umlaut */
    'y',                                         /* FD: y-acute */
    '?',                                         /* FE: thorn */
    0x98                                         /* FF: y-umlaut */
};

/*
 * Map codepoints beyond Latin-1 to CP437.
 * Covers common punctuation, box drawing, and symbols.
 */
static unsigned char map_extended(unsigned long cp)
{
    switch (cp) {
    /* General punctuation */
    case 0x2013: return '-';     /* en-dash */
    case 0x2014: return '-';     /* em-dash */
    case 0x2018: return '\'';    /* left single quote */
    case 0x2019: return '\'';    /* right single quote */
    case 0x201C: return '"';     /* left double quote */
    case 0x201D: return '"';     /* right double quote */
    case 0x2022: return 0x07;    /* bullet */
    case 0x2026: return '.';     /* ellipsis */
    case 0x2039: return '<';     /* single left guillemet */
    case 0x203A: return '>';     /* single right guillemet */
    case 0x20AC: return 'E';     /* euro sign */

    /* Arrows */
    case 0x2190: return 0x1B;    /* left arrow */
    case 0x2191: return 0x18;    /* up arrow */
    case 0x2192: return 0x1A;    /* right arrow */
    case 0x2193: return 0x19;    /* down arrow */

    /* Box drawing (most common) */
    case 0x2500: return 0xC4;    /* light horizontal */
    case 0x2502: return 0xB3;    /* light vertical */
    case 0x250C: return 0xDA;    /* light down-right */
    case 0x2510: return 0xBF;    /* light down-left */
    case 0x2514: return 0xC0;    /* light up-right */
    case 0x2518: return 0xD9;    /* light up-left */
    case 0x251C: return 0xC3;    /* light vertical-right */
    case 0x2524: return 0xB4;    /* light vertical-left */
    case 0x252C: return 0xC2;    /* light down-horizontal */
    case 0x2534: return 0xC1;    /* light up-horizontal */
    case 0x253C: return 0xC5;    /* light cross */
    case 0x2550: return 0xCD;    /* double horizontal */
    case 0x2551: return 0xBA;    /* double vertical */

    /* Block elements */
    case 0x2588: return 0xDB;    /* full block */
    case 0x2591: return 0xB0;    /* light shade */
    case 0x2592: return 0xB1;    /* medium shade */
    case 0x2593: return 0xB2;    /* dark shade */

    /* Math */
    case 0x2248: return 0xF7;    /* almost equal */
    case 0x2260: return '!';     /* not equal (best effort) */
    case 0x2264: return 0xF3;    /* less-equal */
    case 0x2265: return 0xF2;    /* greater-equal */

    default:     return '?';
    }
}

void utf8_init(utf8_decoder_t *d)
{
    d->state = 0;
    d->codepoint = 0;
    d->min_cp = 0;
}

unsigned char utf8_feed(utf8_decoder_t *d, unsigned char byte)
{
    unsigned long cp;

    /* ASCII: pass through directly */
    if (byte < 0x80) {
        if (d->state != 0) {
            /* Incomplete sequence — reset and emit replacement */
            d->state = 0;
            /* Don't lose this byte: treat as new start */
        }
        d->state = 0;
        return byte;
    }

    /* Continuation byte (10xxxxxx) */
    if ((byte & 0xC0) == 0x80) {
        if (d->state == 0) return '?';  /* unexpected continuation */
        d->codepoint = (d->codepoint << 6) | (byte & 0x3F);
        d->state--;
        if (d->state > 0) return 0;     /* still accumulating */

        /* Sequence complete — map codepoint */
        cp = d->codepoint;

        /* Reject overlong sequences */
        if ((d->min_cp == 0x80 && cp < 0x80) ||
            (d->min_cp == 0x00 && cp < 0x800)) {
            return '?';
        }

        /* Latin-1 Supplement range */
        if (cp >= 0x80 && cp <= 0xFF)
            return lat1_to_cp437[cp - 0x80];

        /* Extended mappings */
        return map_extended(cp);
    }

    /* 2-byte start (110xxxxx) */
    if ((byte & 0xE0) == 0xC0) {
        d->state = 1;
        d->codepoint = byte & 0x1F;
        d->min_cp = 0x80;
        return 0;
    }

    /* 3-byte start (1110xxxx) */
    if ((byte & 0xF0) == 0xE0) {
        d->state = 2;
        d->codepoint = byte & 0x0F;
        d->min_cp = 0;  /* check cp >= 0x800 on completion */
        return 0;
    }

    /* 4-byte start (11110xxx) — emoji/CJK, map to ? */
    if ((byte & 0xF8) == 0xF0) {
        d->state = 3;
        d->codepoint = byte & 0x07;
        d->min_cp = 0;
        return 0;
    }

    /* Invalid byte */
    d->state = 0;
    return '?';
}
