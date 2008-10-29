/*  -*- tab-width: 4; -*-
 *
 *  int_pack.c -- pack N-bit integers into a 32-bit integer array.
 *
 */
#include <assert.h>

#include "int_pack.h"

#define BWIDTH 32U


size_t
pack32_len(size_t siz, unsigned nbit)
{
	/* len = (nbit * siz + BWIDTH - 1) / BWIDTH; */
	size_t n = siz / BWIDTH; /* to avoid overflow */

	siz -= n * BWIDTH;
	return nbit * n + (nbit * siz + BWIDTH - 1) / BWIDTH;
}


/*
 *  pack N-bit integers into a 32-bit integers array.
 *  'N' is between 1 and 31.
 *
 *  retrun the length of packed array.
 */
size_t
pack_bits_into32(uint32_t *packed,
				 const unsigned *data, size_t nelem,
				 unsigned nbit)
{
	unsigned off;
	unsigned value, mask;
	uint32_t *ptr;
	size_t i, len;


	assert(nbit > 0 && nbit < 32);

	mask = (1U << nbit) - 1U;
	len = pack32_len(nelem, nbit);

	for (i = 0; i < len; i++)
		packed[i] = 0;			/* clear */

	for (i = 0, ptr = packed, off = 0; i < nelem; i++, off += nbit) {
		if (off > BWIDTH) {
			off -= BWIDTH;
			ptr++;
		}

		value = data[i] & mask;

		if (BWIDTH < off + nbit) {
			*ptr     |= value >> (off + nbit - BWIDTH);
			*(ptr+1) |= value << (2 * BWIDTH - off - nbit);
		} else
			*ptr     |= value << (BWIDTH - off - nbit);
	}
	return len;
}


void
unpack_bits_from32(unsigned *data,
				   size_t len,
				   const uint32_t *packed, unsigned nbit)
{
	unsigned i, i2, i3;
	unsigned ipos, off;
	unsigned value;
	unsigned mask;

	assert(nbit > 0 && nbit < 32);
	mask = (1U << nbit) - 1U;

	for (i = 0; i < len; i++) {
		i2 = i / BWIDTH;
		i3 = i % BWIDTH;

		ipos = nbit * i2 + nbit * i3 / BWIDTH;
		off = nbit + (nbit * i3) % BWIDTH;

		if (off > BWIDTH)
			value = ((packed[ipos] << (off - BWIDTH)) & mask)
				| ((packed[ipos+1] >> (2 * BWIDTH - off)) & mask);
		else
			value = (packed[ipos] >> (BWIDTH - off)) & mask;

		data[i] = value;
	}
}


#ifdef TEST_MAIN
#include <stdio.h>


void
test0(void)
{
	int nbit;
	int nelem, len;

	for (nbit = 1; nbit < 32; nbit++)
		for (nelem = 0; nelem < 100; nelem++) {
			len = pack32_len(nelem, nbit);

			assert(len * 32 >= nelem * nbit);
			assert((len - 1) * 32 < nelem * nbit);
		}
}




void
test(void)
{
	uint32_t packed[9];
	unsigned data[9];
	size_t len;
	unsigned nbit;

	/*
	 *  16-bit packing.
	 */
	nbit = 16;
	data[0] = 0xffff;
	data[1] = 0xeeee;
	data[2] = 0xdddd;
	data[3] = 0xcccc;

	len = pack_bits_into32(packed, data, 4, nbit);
	assert(len == 2);
	assert(packed[0] == 0xffffeeee);
	assert(packed[1] == 0xddddcccc);

	/*
	 *  12-bit packing
	 */
	nbit = 12;
	data[0] = 0xfff;
	data[1] = 0xeee;
	data[2] = 0xddd;
	data[3] = 0xccc;
	data[4] = 0xbbb;
	data[5] = 0xaaa;
	data[6] = 0x999;
	data[7] = 0x888;
	data[8] = 0x777;

	len = pack_bits_into32(packed, data, 8, nbit);
	assert(len == 3);
	assert(packed[0] == 0xfffeeedd);
	assert(packed[1] == 0xdcccbbba);
	assert(packed[2] == 0xaa999888);

	len = pack_bits_into32(packed, data, 9, nbit);
	assert(len == 4);
	assert(packed[3] == 0x77700000);

	/*
	 *  4-bit packing
	 */
	nbit = 4;
	data[0] = 0xf;
	data[1] = 0xf;
	data[2] = 0xe;
	data[3] = 0xf;
	data[4] = 0xc;
	data[5] = 0xf;
	data[6] = 0xd;
	data[7] = 0xf;

	len = pack_bits_into32(packed, data, 8, nbit);
	assert(len == 1);
	assert(packed[0] == 0xffefcfdf);

	/*
	 *  1-bit packing
	 */
	nbit = 1;
	data[0] = 1;
	data[1] = 0;
	data[2] = 1;
	data[3] = 0;
	data[4] = 0;
	data[5] = 0;
	data[6] = 1;
	data[7] = 1;
	len = pack_bits_into32(packed, data, 8, nbit);
	assert(len == 1);
	assert(packed[0] == 0xa3000000);
}


void
test2(unsigned nbit)
{
#define NELEM 4096

	unsigned data[NELEM], data2[NELEM];
	uint32_t packed[NELEM];
	int i;

	for (i = 0; i < NELEM; i++)
		data[i] = i % (1U << nbit);

	pack_bits_into32(packed, data, NELEM, nbit);
	unpack_bits_from32(data2, NELEM, packed, nbit);

	for (i = 0; i < NELEM; i++)
		assert(data[i] == data2[i]);
}


int
main(int argc, char **argv)
{
	unsigned nbit;

	test0();
	test();
	for (nbit = 1; nbit < 32; nbit++) {
		test2(nbit);
	}
	return 0;
}
#endif /* TEST_MAIN */
