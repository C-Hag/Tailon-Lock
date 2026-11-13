/* tlpk.h - unpacking the floppy forge's packed files (TLPK; tools/ff/tlpk makes them). Original code for this project. */
#ifndef TL_TLPK_H
#define TL_TLPK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Do these bytes start a TLPK file? */
bool tlpk_is(const uint8_t *in, size_t n);
/* The unpacked bytes, with a 0 byte after them (so text can be read as a string), *size their length; NULL when the
 * file is not TLPK, is cut short or damaged (its CRC-32 does not match), or is larger than limit bytes. free() it. */
uint8_t *tlpk_unpack(const uint8_t *in, size_t n, size_t limit, size_t *size);

#endif
