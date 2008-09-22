#ifndef INTPACK__H
#define INTPACK__H


#include <stdint.h>
#include <string.h>

unsigned pack32_len(size_t siz, unsigned nbit);
size_t pack_bits_into32(uint32_t *packed,
						const unsigned *data, size_t nelem,
						unsigned nbit);
void unpack_bits_from32(unsigned *data,
						size_t len,
						const uint32_t *packed, unsigned nbit);


#endif
