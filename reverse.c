/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  reverse.c -- reversing byte-order.
 *
 *  $Date: 2006/11/29 03:08:14 $
 */
#include "internal.h"


/*
 *  reverse_words() reverses the byte-order of words (32-bit).
 */
void *
reverse_words(void *vptr, int nwords)
{
	int i;
	uint32_t u;
	uint32_t *uptr = vptr;

	for (i = 0; i < nwords; i++) {
		u = uptr[i];
		uptr[i] = (u >> 24) | ((u & 0xff0000U) >> 8)
			| ((u & 0xff00U) << 8) | ((u & 0xffU) << 24);
	}
	return vptr;
}


/*
 *  reverse_dwords() reverses the byte-order of double words (64-bit).
 */
void *
reverse_dwords(void *vptr, int nwords)
{
	int i;
	uint32_t u1, u2;
	uint32_t *uptr = vptr;

	for (i = 0; i < 2 * nwords; i += 2) {
		u1 = uptr[i];
		u2 = uptr[i+1];

		uptr[i]   = (u2 >> 24) | ((u2 & 0xff0000U) >> 8)
			| ((u2 & 0xff00U) << 8) | ((u2 & 0xffU) << 24);
		uptr[i+1] = (u1 >> 24) | ((u1 & 0xff0000U) >> 8)
			| ((u1 & 0xff00U) << 8) | ((u1 & 0xffU) << 24);
	}
	return vptr;
}


#ifdef TEST_MAIN
#include <assert.h>

int
main(int argc, char **argv)
{
	unsigned u[2];

	u[0] = 0x12345678;
	u[1] = 0xfedcba98;
	reverse_words(u, 2);
	assert(u[0] == 0x78563412);
	assert(u[1] == 0x98badcfe);

	u[0] = 0x12345678;
	u[1] = 0xfedcba98;
	reverse_dwords(u, 1);
	assert(u[0] == 0x98badcfe);
	assert(u[1] == 0x78563412);

	u[0] = 0x12345678;
	u[1] = 0xfedcba98;
	reverse_words(u, 1);
	assert(u[0] == 0x78563412);
	assert(u[1] == 0xfedcba98);
	return 0;
}
#endif
