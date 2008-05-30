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
 *  pack N-bit unsinged integers into a 32-bit unsigned integers array.
 *  'N' is between 1 and 31.
 *
 *  standard version.
 */
static size_t
packing_into32(uint32_t *packed,
			   const unsigned *data, size_t siz,
			   unsigned nbit)
{
	unsigned i, off;
	unsigned value, mask, *ptr;
	size_t len;


	assert(nbit > 0 && nbit < 32);

	mask = (1U << nbit) - 1U;
	len = pack32_len(siz, nbit);

	for (i = 0; i < len; i++)
		packed[i] = 0;			/* clear */

	for (i = 0, ptr = packed, off = 0; i < siz; i++, off += nbit) {
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


#if 0
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
	unsigned ipos, off;
	unsigned n, i, i2, i3;
	unsigned value, mask;
	size_t len;


	assert(nbit > 0 && nbit < 32);

	step = 2 + (BWIDTH - 1) / nbit;
	mask = (1U << nbit) - 1U;

	len = pack32_len(siz, nbit);
	for (n = 0; n < len; n++)
		packed[n] = 0;			/* clear */

	for (n = 0; n < step; n++) {
		for (i = n; i < siz; i += step) { /* XXX: can be vectorized. */
			i2 = i / BWIDTH;
			i3 = i % BWIDTH;
			ipos = nbit * i2 + (nbit * i3) / BWIDTH;
			off  = (nbit * i3) % BWIDTH;

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
#else
/*
 *  pack N-bit unsinged integers into a 32-bit unsigned integers array
 *  'N' is between 8 and 15.
 *
 *  VECTORIZED VERSION
 */
static size_t
packing_into32v_8(uint32_t *packed,
				  const unsigned *data, size_t siz,
				  unsigned nbit)
{
	unsigned base;
	unsigned i, m, i2;
	unsigned mask;
	unsigned vmask[5];
	size_t len;


	assert(nbit >= 8 && nbit < 16);

	mask = (1U << nbit) - 1U;

	len = pack32_len(siz, nbit);

	for (i = 0; i < len; i++) {
		i2 = BWIDTH * i;
		m = i2 / nbit;

		base = nbit - (i2 - m * nbit);

		vmask[0] = (data[m] & mask) << (BWIDTH - base);

		base += nbit;
		if (base > BWIDTH)
			vmask[1] = (data[m+1] & mask) >> (base - BWIDTH);
		else
			vmask[1] = (data[m+1] & mask) << (BWIDTH - base);

		base += nbit;
		if (base > BWIDTH)
			vmask[2] = (data[m+2] & mask) >> (base - BWIDTH);
		else
			vmask[2] = (data[m+2] & mask) << (BWIDTH - base);

		base += nbit;
		if (base > BWIDTH)
			vmask[3] = (data[m+3] & mask) >> (base - BWIDTH);
		else
			vmask[3] = (data[m+3] & mask) << (BWIDTH - base);

		base += nbit;
		if (base >= 2 * BWIDTH)
			vmask[4] = 0;
		else if (base > BWIDTH)
			vmask[4] = (data[m+4] & mask) >> (base - BWIDTH);
		else
			vmask[4] = (data[m+4] & mask) << (BWIDTH - base);


		packed[i] = vmask[0] | vmask[1] | vmask[2] | vmask[3] | vmask[4];
	}
	return len;
}
/*
 *  pack N-bit unsinged integers into a 32-bit unsigned integers array
 *  'N' is between 16 and 31.
 *
 *  VECTORIZED VERSION
 */
static size_t
packing_into32v_16(uint32_t *packed,
				   const unsigned *data, size_t siz,
				   unsigned nbit)
{
	unsigned base;
	unsigned i, m, i2;
	unsigned mask;
	unsigned vmask[3];
	size_t len;


	assert(nbit >= 16 && nbit < 32);

	mask = (1U << nbit) - 1U;

	len = pack32_len(siz, nbit);

	for (i = 0; i < len; i++) {
		i2 = BWIDTH * i;
		m = i2 / nbit;

		base = nbit - (i2 - m * nbit);

		vmask[0] = (data[m] & mask) << (BWIDTH - base);

		base += nbit;
		if (base > BWIDTH)
			vmask[1] = (data[m+1] & mask) >> (base - BWIDTH);
		else
			vmask[1] = (data[m+1] & mask) << (BWIDTH - base);

		base += nbit;
		if (base >= 2 * BWIDTH)
			vmask[2] = 0;
		else if (base > BWIDTH)
			vmask[2] = (data[m+2] & mask) >> (base - BWIDTH);
		else
			vmask[2] = (data[m+2] & mask) << (BWIDTH - base);

		packed[i] = vmask[0] | vmask[1] | vmask[2];
	}
	return len;
}
static size_t
packing_into32v_1(uint32_t *packed,
				  const unsigned *data, size_t siz,
				  unsigned nbit)
{
	unsigned base;
	unsigned i, j, m, i2;
	unsigned mask, vmask;
	size_t len;


	assert(nbit < 32);

	mask = (1U << nbit) - 1U;

	len = pack32_len(siz, nbit);

	for (i = 0; i < len; i++) {
		i2 = BWIDTH * i;
		m = i2 / nbit;

		base = nbit - (i2 - m * nbit);

		packed[i] = (data[m] & mask) << (BWIDTH - base);

		for (j = 1; j < 32; j++) {
			base += nbit;

			if (base >= 2 * BWIDTH)
				vmask = 0;
			else if (base > BWIDTH)
				vmask = (data[m+j] & mask) >> (base - BWIDTH);
			else
				vmask = (data[m+j] & mask) << (BWIDTH - base);

			packed[i] |= vmask;
		}
	}
	return len;
}

static size_t
packing_into32v(uint32_t *packed,
				const unsigned *data, size_t siz,
				unsigned nbit)
{
	if (nbit >= 8 && nbit < 16)
		return packing_into32v_8(packed, data, siz, nbit);
	if (nbit >= 16 && nbit < 32)
		return packing_into32v_16(packed, data, siz, nbit);

	return packing_into32v_1(packed, data, siz, nbit);
}
#endif



static int
unpacking_from32(unsigned *data,
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

		ipos = nbit * i2 + (nbit * i3) / BWIDTH;
		off  = (nbit * i3) % BWIDTH;

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
test()
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

	len = packing_into32(packed, data, 4, nbit);
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

	len = packing_into32(packed, data, 8, nbit);
	assert(len == 3);
	assert(packed[0] == 0xfffeeedd);
	assert(packed[1] == 0xdcccbbba);
	assert(packed[2] == 0xaa999888);

	len = packing_into32(packed, data, 9, nbit);
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

	len = packing_into32(packed, data, 8, nbit);
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
	len = packing_into32(packed, data, 8, nbit);
	assert(len == 1);
	assert(packed[0] == 0xa3000000);
}


void
test2(unsigned nbit)
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

	for (i = 0; i < siz; i++)
		assert(data[i] == data2[i]);
}


/* test betweeen packing_into32 and packing_into32v */
void
test3(unsigned nbit)
{
#define ND (1U << 13)

	unsigned data[ND + 32];
	int i;
	uint32_t packed[ND];
	uint32_t packed2[ND];
	size_t len, len2;


	for (i = 0; i < ND; i++)
		data[i] = i % (1U << nbit);

	for (i = 0; i < 32; i++)
		data[ND+i] = 0;

	len  = packing_into32(packed, data, ND, nbit);
	len2 = packing_into32v(packed2, data, ND, nbit);

	assert(len == len2);
	for (i = 0; i < len; i++) {
		assert(packed[i] == packed2[i]);
	}
}


int
main(int argc, char **argv)
{
	unsigned nbit;

	test();
	for (nbit = 1; nbit < 32; nbit++) {
		test2(nbit);
		test3(nbit);
		printf("N = %d test ... done\n", nbit);
	}
	return 0;
}
#endif /* TEST_MAIN */
