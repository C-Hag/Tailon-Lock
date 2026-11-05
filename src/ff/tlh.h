/* tlh.h - the floppy forge's terrain heights (TLH; tools/ff/tlh makes them) back into the 16-bit samples the game reads
 * (little-endian, row by row, as a .h16 file holds them). Original code for this project. */
#ifndef A4C_TLH_H
#define A4C_TLH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool tlh_is(const uint8_t *in, size_t n);
/* The samples as the .h16 file's bytes, with a 0 byte after them, *size their length; NULL when damaged (the CRC-32 of
 * the samples does not match). free() it. */
uint8_t *tlh_unpack(const uint8_t *in, size_t n, size_t *size);

#endif
