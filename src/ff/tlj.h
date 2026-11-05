/* tlj.h - the floppy forge's binary JSON (TLJ; tools/ff/tlj makes it) back into JSON text, which the game's own parser
 * then reads, so the tree is the one the original text gives. Original code for this project. */
#ifndef A4C_TLJ_H
#define A4C_TLJ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Do these bytes start a TLJ document? */
bool tlj_is(const uint8_t *in, size_t n);
/* The document as JSON text with a 0 byte after it, *size its length; NULL when it is damaged. free() it. */
char *tlj_to_text(const uint8_t *in, size_t n, size_t *size);

#endif
