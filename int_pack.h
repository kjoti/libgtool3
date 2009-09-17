#ifndef INTPACK__H
#define INTPACK__H

#include "internal.h"
#include <string.h>

size_t pack32_len(size_t siz, unsigned nbit);
size_t pack_bits_into32(uint32_t *packed,
                        const unsigned *data, size_t nelem,
                        unsigned nbit);
size_t pack_bools_into32(uint32_t *packed,
                         const unsigned char *flags, size_t nelems);
void unpack_bits_from32(unsigned *data,
                        size_t len,
                        const uint32_t *packed, unsigned nbit);
#endif
