/*
 * utf8.h - UTF-8 decoder with CP437 mapping for Cathode browser
 *
 * Decodes multi-byte UTF-8 sequences and maps codepoints to CP437
 * characters. Returns 0 while accumulating, CP437 char when complete.
 */

#ifndef UTF8_H
#define UTF8_H

typedef struct {
    unsigned char state;        /* 0=idle, 1-3=continuation bytes remaining */
    unsigned long codepoint;    /* accumulated codepoint */
    unsigned char min_cp;       /* minimum valid codepoint for byte length */
} utf8_decoder_t;

void          utf8_init(utf8_decoder_t *d);
unsigned char utf8_feed(utf8_decoder_t *d, unsigned char byte);

/* Map a Unicode codepoint directly to CP437 (for entity resolution) */
unsigned char utf8_cp_to_cp437(unsigned long cp);

#endif /* UTF8_H */
