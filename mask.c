/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  mask.c -- mask for MR4, MR8, and MRX.
 */
#include "internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"


static void
reset_mask(GT3_Datamask *ptr)
{
	ptr->nelem = 0;
	ptr->loaded = -1;
	ptr->indexed = 0;
}


static int
getbit(const uint32_t *mask, unsigned i)
{
	return (mask[i >> 5U] >> (31U - (i & 0x1fU))) & 1U;
}


/*
 *  allocate a new GT3_Datamask.
 */
GT3_Datamask *
GT3_newMask(void)
{
	GT3_Datamask *mask;

	if ((mask = (GT3_Datamask *)malloc(sizeof(GT3_Datamask))) == NULL) {
		gt3_error(SYSERR, NULL);
		return NULL;
	}

	reset_mask(mask);
	mask->reserved = 0;
	mask->mask = NULL;
	mask->index = NULL;
	return mask;
}


void
GT3_freeMask(GT3_Datamask *ptr)
{
	free(ptr->mask);
	free(ptr->index);

	reset_mask(ptr);
	ptr->reserved = 0;
	ptr->mask = NULL;
	ptr->index = NULL;
}


int
GT3_setMaskSize(GT3_Datamask *ptr, size_t nelem)
{
	uint32_t *mask = NULL;
	int *idx = NULL;
	size_t mlen;				/* mask size per z-plane */


	if (ptr->reserved >= nelem) {
		ptr->nelem = nelem;
		return 0;
	}

	GT3_freeMask(ptr);
	mlen = (nelem + 31) / 32;
	if ((mask = (uint32_t *)malloc(sizeof(uint32_t) * mlen)) == NULL
		|| (idx  = (int *)malloc(sizeof(int) * (nelem + 1))) == NULL) {
		free(idx);
		free(mask);
		gt3_error(SYSERR, NULL);
		return -1;
	}

	reset_mask(ptr);

	ptr->nelem = nelem;
	ptr->reserved = nelem;
	ptr->mask = mask;
	ptr->index = idx;
	return 0;
}


void
GT3_updateMaskIndex(GT3_Datamask *mask)
{
	int i;
	int idx = 0;

	if (mask->indexed)
		return;					/* no need to update */

	for (i = 0; i < mask->nelem; i++) {
		mask->index[i] = idx;

		if (getbit(mask->mask, i))
			idx++;
	}
	mask->index[mask->nelem] = idx;
	mask->indexed = 1;
}


int
GT3_getMaskValue(const GT3_Datamask *mask, int i)
{
	return getbit(mask->mask, i);
}


/*
 *  load mask data from MR4 or MR8.
 */
int
GT3_loadMask(GT3_Datamask *mask, GT3_File *fp)
{
	size_t nelem, mlen;


	if (mask->loaded == fp->curr)
		return 0;

	nelem = fp->dimlen[0] * fp->dimlen[1] * fp->dimlen[2];
	mlen = (nelem + 31) / 32;
	if (GT3_setMaskSize(mask, nelem) < 0)
		return -1;

	if (fseeko(fp->fp,
			   fp->off + GT3_HEADER_SIZE + 4
			   + 5 * sizeof(FTN_HEAD),
			   SEEK_SET) < 0
		|| fread(mask->mask, 4, mlen, fp->fp) != mlen) {
		gt3_error(GT3_ERR_BROKEN, fp->path);
		return -1;
	}

	if (IS_LITTLE_ENDIAN)
		reverse_words(mask->mask, mlen);

	reset_mask(mask);
	mask->loaded = fp->curr;
	return 0;
}


/*
 *  load mask data from MRX.
 */
int
GT3_loadMaskX(GT3_Datamask *mask, int zpos, GT3_File *fp)
{
	size_t nelem, mlen;

	/* FIXME: It is assumed that zpos is not so large. */
	assert(zpos >= 0 && zpos < (1U << 16));


	if (mask->loaded == (fp->curr << 16 | zpos))
		return 0;

	nelem = fp->dimlen[0] * fp->dimlen[1];
	mlen = (nelem + 31) / 32;
	if (GT3_setMaskSize(mask, nelem) < 0)
		return -1;

	if (fseeko(fp->fp,
			   fp->off + 10 * sizeof(FTN_HEAD)
			   + GT3_HEADER_SIZE
			   + 4
			   + 4 * fp->dimlen[2]
			   + 4 * fp->dimlen[2]
			   + 2 * 8 * fp->dimlen[2]
			   + sizeof(FTN_HEAD) + 4 * mlen * zpos,
			   SEEK_SET) < 0
		|| fread(mask->mask, 4, mlen, fp->fp) != mlen) {
		gt3_error(GT3_ERR_BROKEN, fp->path);
		return -1;
	}
	if (IS_LITTLE_ENDIAN)
		reverse_words(mask->mask, mlen);

	reset_mask(mask);
	mask->loaded = (fp->curr << 16 | zpos);
	return 0;
}


#ifdef TEST_MAIN
#include <stdio.h>
#include <assert.h>


int
main(int argc, char **argv)
{
	uint32_t mask[] = {
		0x00000000,
		0xffffffff,
		0xaaaaaaaa
	};
	int i;

	for (i = 0; i < 32; i++)
		assert(getbit(mask, i) == 0);

	for (i = 32; i < 64; i++)
		assert(getbit(mask, i) == 1);

	for (i = 64; i < 96; i++)
		assert(getbit(mask, i) == (i+1) % 2);

	for (i = 0; i < 96; i++)
		printf("%3d %3d\n", i, getbit(mask, i));

	return 0;
}
#endif
