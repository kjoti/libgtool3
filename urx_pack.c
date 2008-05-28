/*  -*- tab-width: 4; -*-
 *
 *  urx_pack.c -- gtool URX format pack/unpack routine.
 *
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>


#define BWIDTH 32U


static unsigned
pack32_len(size_t siz, unsigned nbit)
{
	/* len = (nbit * siz + BWIDTH - 1) / BWIDTH; */
	unsigned n = siz / BWIDTH; /* to avoid overflow */

	siz -= n * BWIDTH;
	return nbit * n + (nbit * siz + BWIDTH - 1) / BWIDTH;
}


/*
 *  pack N-bit unsinged integers into a 32-bit unsigned integers
 *  array bitwise.
 *
 *  standard version.
 */
static size_t
packing_into32(uint32_t *packed,
			   const unsigned *data, size_t siz,
			   unsigned nbit)
{
	unsigned i, ipos, off;
	unsigned value, mask;
	size_t len;


	assert(nbit > 0 && nbit < 32);

	mask = (1U << nbit) - 1U;
	len = pack32_len(siz, nbit);

	for (i = 0; i < len; i++)
		packed[i] = 0;			/* clear */

	for (i = 0, ipos = 0, off = 0; i < siz; i++, off += nbit) {
		if (off > BWIDTH) {
			off -= BWIDTH;
			ipos++;
		}

		value = data[i] & mask;

		if (BWIDTH < off + nbit) {
			packed[ipos]   |= value >> (off + nbit - BWIDTH);
			packed[ipos+1] |= value << (2 * BWIDTH - off - nbit);
		} else {
			packed[ipos]   |= value << (BWIDTH - off - nbit);
		}
	}
	return len;
}


/*
 *  pack N-bit unsinged integers into a 32-bit unsigned integers array
 *
 *  vectorized version.
 */
static size_t
packing_into32v(uint32_t *packed,
				const unsigned *data, size_t siz,
				unsigned nbit)
{
	unsigned step;
	unsigned bpos, ipos, off;
	unsigned n, i;
	unsigned value, mask;
	size_t len;


	assert(nbit > 0 && nbit < 32);

	step = 2 + (BWIDTH - 1) / nbit;
	mask = (1U << nbit) - 1U;

	len = pack32_len(siz, nbit);
	for (n = 0; n < len; n++)
		packed[n] = 0;			/* clear */

	for (n = 0; n < step; n++) {
		for (i = n; i < siz; i += step) {
			/* XXX This loop can be vectorized. */

			bpos = nbit * i;
			ipos = bpos / BWIDTH;

			off = bpos - BWIDTH * ipos;
			value = data[i] & mask;

			if (BWIDTH < nbit + off) {
				packed[ipos]   |= value >> (off + nbit - BWIDTH);
				packed[ipos+1] |= value << (2 * BWIDTH - off - nbit);
			} else
				packed[ipos]   |= value << (BWIDTH - off - nbit);
		}
	}
	return len;
}


static int
unpacking_from32(unsigned *data,
				 size_t len,
				 const uint32_t *packed, unsigned nbit)
{
	unsigned i;
	unsigned bpos, ipos, off;
	unsigned value;
	unsigned mask;


	assert(nbit > 0 && nbit < 32);
	mask = (1U << nbit) - 1U;

	for (i = 0; i < len; i++) {
		bpos = nbit * i;
		ipos = bpos / BWIDTH;

		off = bpos - ipos * BWIDTH;

		if (off + nbit > BWIDTH)
			value = ((packed[ipos] << (off + nbit - BWIDTH)) & mask)
				| ((packed[ipos+1] >> (2 * BWIDTH - off - nbit)) & mask);
		else
			value = (packed[ipos] >> (BWIDTH - off - nbit)) & mask;

		data[i] = value;
	}
	return 0;
}


#ifdef TEST_MAIN
#include <stdio.h>


void
test(unsigned nbit)
{
	unsigned data[1024];
	unsigned data2[1024];
	size_t siz = sizeof data / sizeof(data[0]);
	int i;
	uint32_t packed[1024];


	for (i = 0; i < siz; i++)
		data[i] = i % (1U << nbit);

	packing_into32(packed, data, siz, nbit);

	unpacking_from32(data2, siz, packed, nbit);

	for (i = 0; i < siz; i++) {
		assert(data[i] == data2[i]);
	}
}


void
test2(unsigned nbit)
{
	unsigned data[1024];
	size_t siz = sizeof data / sizeof(data[0]);
	int i;
	uint32_t packed[1024];
	uint32_t packed2[1024];
	size_t len, len2;


	for (i = 0; i < siz; i++)
		data[i] = i % (1U << nbit);

	len  = packing_into32(packed, data, siz, nbit);
	len2 = packing_into32v(packed2, data, siz, nbit);

	assert(len == len2);
	for (i = 0; i < len; i++)
		assert(packed[i] == packed2[i]);
}


int
main(int argc, char **argv)
{
	unsigned nbit;

	for (nbit = 1; nbit < 32; nbit++) {
		test(nbit);
		test2(nbit);
	}
	return 0;
}
#endif /* TEST_MAIN */
