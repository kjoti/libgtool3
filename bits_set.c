/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  bits_set.c
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#include <stdlib.h>

#include "bits_set.h"


int
resize_bits_set(bits_set *bs, unsigned nbits)
{
	size_t len;

	len = (nbits + 31) >> 5;
	if (len > bs->size) {
		uint32_t *ptr;

		if ((ptr = realloc(bs->set, sizeof(uint32_t) * len)) == NULL)
			return -1;

		bs->set = ptr;
		bs->size = len;
	}
	return 0;
}


void
free_bits_set(bits_set *bs)
{
	if (bs) {
		free(bs->set);
		bs->set = NULL;
		bs->size = 0;
	}
}


#ifdef TEST_MAIN
#include <assert.h>

int
main(int argc, char **argv)
{
	bits_set bs = { NULL, 0 };
	int i, rval;
	int nbits;

	for (i = 10; i <= 32; i++) {
		rval = resize_bits_set(&bs, 32);
		assert(rval == 0);
		assert(bs.size == 1);
	}
	rval = resize_bits_set(&bs, 33);
	assert(rval == 0);
	assert(bs.size == 2);


	nbits = 10000;
	rval = resize_bits_set(&bs, nbits);
	assert(rval == 0);

	/* test 1 */
	BS_CLSALL(bs);
	BS_SET(bs, 0);
	BS_SET(bs, nbits - 1);

	assert(BS_TEST(bs, 0) && BS_TEST(bs, nbits - 1));
	for (i = 1; i < nbits - 1; i++)
		assert(!BS_TEST(bs, i));

	/* test 2 */
	BS_SETALL(bs);
	BS_CLS(bs, 0);
	BS_CLS(bs, nbits - 1);

	assert(!BS_TEST(bs, 0) && !BS_TEST(bs, nbits - 1));
	for (i = 1; i < nbits - 1; i++)
		assert(BS_TEST(bs, i));

	free_bits_set(&bs);
	return 0;
}
#endif /* TEST_MAIN */
