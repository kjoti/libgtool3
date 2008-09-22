/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  file.c -- support GTOOL3-formatted file.
 *
 *  $Date: 2006/12/04 06:54:28 $
 */
#include "internal.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "int_pack.h"
#include "debug.h"

#define CHNUM_UNKNOWN -1


static int
get_dimsize(int dim[], const GT3_HEADER *hh)
{
	const char *axis[] = {"ASTR1", "AEND1",
						  "ASTR2", "AEND2",
						  "ASTR3", "AEND3"};
	int i, idx[6];

	for (i = 0; i < 6; i++)
		if (GT3_decodeHeaderInt(&idx[i], hh, axis[i]) < 0) {
			gt3_error(GT3_ERR_HEADER, axis[i]);
			return -1;
		}

	dim[0] = idx[1] - idx[0] + 1;
	dim[1] = idx[3] - idx[2] + 1;
	dim[2] = idx[5] - idx[4] + 1;
	return 0;
}


static size_t
chunk_size(int fmt, int nx, int ny, int nz)
{
	size_t siz = GT3_HEADER_SIZE + 2 * sizeof(FTN_HEAD);
	unsigned mask = (1U << GT3_FMT_MASKBIT) - 1U;

	switch (fmt & mask) {
	case GT3_FMT_UR4:
		siz += 4 * (nx*ny*nz) + 2 * sizeof(FTN_HEAD);
		break;
	case GT3_FMT_URC:
	case GT3_FMT_URC1:
		siz += (8 + 4 + 4 + 2 * nx*ny + 8 * sizeof(FTN_HEAD)) * nz;
		break;
	case GT3_FMT_UR8:
		siz += 8 * (nx*ny*nz) + 2 * sizeof(FTN_HEAD);
		break;
	case GT3_FMT_URX:
		siz += 8 * 2 * nz + 2 * sizeof(FTN_HEAD)  /* DMA */
			+  sizeof(uint32_t)
			* pack32_len(nx*ny, fmt >> GT3_FMT_MASKBIT) * nz
			+  2 * sizeof(FTN_HEAD);
		break;
	default:
		assert(!"Unknown format");
		break;
	}

	return siz;
}


/*
 *  Updates GT3_File with a header (when going into a new chunk).
 */
static int
update(GT3_File *fp, const GT3_HEADER *headp)
{
	char dfmt[8];
	int dim[3];
	int fmt;

	dfmt[0] = '\0';
	(void)GT3_copyHeaderItem(dfmt, sizeof dfmt, headp, "DFMT");

	if ((fmt = GT3_format(dfmt)) < 0) {
		gt3_error(GT3_ERR_HEADER, "Unknown format: %s", dfmt);
		return -1;
	}

	if (get_dimsize(dim, headp) < 0)
		return -1;

	if (dim[0] < 1 || dim[1] < 1 || dim[2] < 1) {
		gt3_error(GT3_ERR_HEADER, "Invalid dim-size: %d %d %d",
			  dim[0], dim[1], dim[2]);
		return -1;
	}

	/*
	 *  updates GT3_File member.
	 */
	fp->fmt    = fmt;
	fp->chsize = chunk_size(fmt, dim[0], dim[1], dim[2]);

	fp->dimlen[0] = dim[0];
	fp->dimlen[1] = dim[1];
	fp->dimlen[2] = dim[2];
	return 0;
}


static int
seekhist(GT3_File *fp, int ch)
{
	off_t nextoff = ch;

	nextoff *= fp->chsize;
	if (fseeko(fp->fp, nextoff, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	fp->curr = ch;
	fp->off  = nextoff;

	return 0;
}


static int
read_header(GT3_HEADER *header, FILE *fp)
{
	const char *magic = "            9010";
	char temp[GT3_HEADER_SIZE + 2 * sizeof(FTN_HEAD)];
	size_t siz;

	if ((siz = fread(temp, 1, sizeof temp, fp)) != sizeof temp
		|| temp[0]    != 0 || temp[1]    != 0
		|| temp[2]    != 4 || temp[3]    != 0
		|| temp[1028] != 0 || temp[1029] != 0
		|| temp[1030] != 4 || temp[1031] != 0
		|| memcmp(temp + 4, magic, 16) != 0) {

		return -1;
	}

	memcpy(header->h, temp + sizeof(FTN_HEAD), GT3_HEADER_SIZE);
	return 0;
}


/*
 *  offset of each z-slice indexed 'zpos'.
 */
static off_t
zslice_offset(GT3_File *fp, int zpos)
{
	off_t off, nelem;
	unsigned mask = (1 << GT3_FMT_MASKBIT) - 1;

	off = GT3_HEADER_SIZE + 2 * sizeof(FTN_HEAD);
	nelem = (off_t)fp->dimlen[0] * fp->dimlen[1];

	switch (fp->fmt & mask) {
	case GT3_FMT_UR4:
		off += sizeof(FTN_HEAD) + 4 * nelem * zpos;
		break;
	case GT3_FMT_URC:
	case GT3_FMT_URC1:
		off += (8 + 4 + 4 + 2 * nelem + 8 * sizeof(FTN_HEAD)) * zpos;
		break;
	case GT3_FMT_UR8:
		off += sizeof(FTN_HEAD) + 8 * nelem * zpos;
		break;
	case GT3_FMT_URX:
		off += 2 * sizeof(double) * fp->dimlen[2] + 2 * sizeof(FTN_HEAD);
		off += sizeof(FTN_HEAD);
		off += zpos * sizeof(uint32_t)
			* pack32_len(nelem, fp->fmt >> GT3_FMT_MASKBIT);
		break;
	default:
		assert(!"Unknown format");
	}
	return off;
}


int
GT3_readHeader(GT3_HEADER *header, GT3_File *fp)
{
	if (fseeko(fp->fp, fp->off, SEEK_SET) < 0) {
		gt3_error(SYSERR, fp->path);
		return -1;
	}
	if (read_header(header, fp->fp) < 0) {
		gt3_error(GT3_ERR_BROKEN, fp->path);
		return -1;
	}
	return 0;
}


int
GT3_isHistfile(GT3_File *fp)
{
	return fp->mode & 1;
}


int
GT3_format(const char *str)
{
	struct { const char *key; int val; } ftab[] = {
		{ "UR4",  GT3_FMT_UR4   },
		{ "URC2", GT3_FMT_URC   },
		{ "URC",  GT3_FMT_URC1  },
		{ "UI2",  GT3_FMT_URC1  },  /* deprecated name */
		{ "UR8",  GT3_FMT_UR8   }
	};
	int i;

	for (i = 0; i < sizeof ftab / sizeof ftab[0]; i++)
		if (strcmp(ftab[i].key, str) == 0)
			return ftab[i].val;

	/* URX */
	if (strncmp(str, "URX", 3) == 0) {
		unsigned nbit;
		char *endptr;

		nbit = (unsigned)strtol(str + 3, &endptr, 10);
		if (endptr == str + 3
			|| *endptr != '\0'
			|| nbit > 31)
			return -1;

		return GT3_FMT_URX | nbit << GT3_FMT_MASKBIT;
	}

	return -1;
}


int
GT3_countChunk(const char *path)
{
	GT3_File *fp;
	int cnt;

	if ((fp = GT3_open(path)) == NULL)
		return -1;

	while (!GT3_eof(fp))
		if (GT3_next(fp) < 0)
			return -1;

	cnt = fp->curr;
	GT3_close(fp);
	return cnt;
}


static GT3_File *
open_gt3file(const char *path, const char *mode)
{
	FILE *fp;
	struct stat sb;
	GT3_File *gp = NULL;
	GT3_HEADER head;

	if (stat(path, &sb) < 0 || (fp = fopen(path, mode)) == NULL) {
		gt3_error(SYSERR, path);
		return NULL;
	}

	if ((gp = (GT3_File *)malloc(sizeof(GT3_File))) == NULL) {
		gt3_error(SYSERR, NULL);
		goto error;
	}

	if (read_header(&head, fp) < 0) {
		gt3_error(GT3_ERR_FILE, path);
		goto error;
	}

	gp->path   = strdup(path);
	gp->fp     = fp;
	gp->size   = sb.st_size;
	gp->mode   = 0;
	gp->curr   = 0;
	gp->off    = 0;
	gp->num_chunk = CHNUM_UNKNOWN;

	if (update(gp, &head) < 0)
		goto error;

	return gp;

error:
	fclose(fp);
	free(gp);
	return NULL;
}


GT3_File *
GT3_open(const char *path)
{
	return open_gt3file(path, "rb");
}


GT3_File *
GT3_openRW(const char *path)
{
	return open_gt3file(path, "r+b");
}


/*
 *  GT3_openHistFile() opens a gtool3 file as a history-file
 *  with simplified check.
 *  If the file is not a history-file, this operation fails.
 */
GT3_File *
GT3_openHistFile(const char *path)
{
	GT3_File *gp;

	if ((gp = GT3_open(path)) == NULL)
		return NULL;

	/*
	 *  check if this is a history-file.
	 */
	if (gp->size % gp->chsize == 0) {
		gp->mode |= 1;
		gp->num_chunk = gp->size / gp->chsize;
	} else {
		gt3_error(GT3_ERR_CALL, "%s: Not a history-file", path);

		GT3_close(gp);
		gp = NULL;
	}
	return gp;
}


int
GT3_eof(GT3_File *fp)
{
	assert(fp->off <= fp->size);
	return fp->off == fp->size;
}


int
GT3_next(GT3_File *fp)
{
	off_t nextoff;
	GT3_HEADER head;
	int broken;

	if (GT3_eof(fp)) {
		assert(fp->curr == fp->num_chunk);
		return 0;				/* nothing to do */
	}

	nextoff = fp->off + fp->chsize;
	assert(nextoff <= fp->size);

	if (fseeko(fp->fp, nextoff, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	/*
	 *  update GT3_File and verify next chunk.
	 */
	broken = 0;
	if (nextoff < fp->size) { /* not EOF yet */
		if (read_header(&head, fp->fp) < 0) {
			gt3_error(GT3_ERR_BROKEN, fp->path);
			broken = 1;
		} else if (update(fp, &head) < 0)
			broken = 1;
		else if (nextoff + fp->chsize > fp->size) {
			gt3_error(GT3_ERR_BROKEN, "unexpected EOF(%s)", fp->path);
			broken = 1;
		}
	}

	/*
	 *  if next chunk is broken, back to previous chunk.
	 */
	if (broken) {
		GT3_seek(fp, 0, SEEK_CUR);
		return -1;
	}

	fp->curr++;
	fp->off = nextoff;
	debug1("GT3_next(): No. %d", fp->curr);

	if (GT3_eof(fp)) {
		if (fp->num_chunk != CHNUM_UNKNOWN) {
			/* Already num_chunk is known. */
			assert(fp->num_chunk == fp->curr);
		}
		fp->num_chunk = fp->curr;
	}
	return 0;
}


void
GT3_close(GT3_File *fp)
{
	if (fp) {
		fclose(fp->fp);
		free(fp->path);
		free(fp);
	}
}


int
GT3_rewind(GT3_File *fp)
{
	GT3_HEADER head;

	if (fseeko(fp->fp, 0, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	read_header(&head, fp->fp);
	update(fp, &head);
	fp->curr = 0;
	fp->off  = 0;
	return 0;
}


int
GT3_seek(GT3_File *fp, int dest, int whence)
{
	int num;

	/*
	 *  chunk number of dest
	 */
	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		dest += fp->curr;
		break;
	case SEEK_END:
		if (fp->num_chunk == CHNUM_UNKNOWN) {
			int cnt;

			if ((cnt = GT3_countChunk(fp->path)) < 0)
				return -1;

			fp->num_chunk = cnt;
		}
		dest += fp->num_chunk;
		break;
	default:
		break;
	}

	/*
	 *  check range.
	 *  If this file is not a history-file and 'num_chunk' is
	 *  still unknown, we cannot detect the over-running.
	 */
	if (dest < 0 || (fp->num_chunk != CHNUM_UNKNOWN && dest > fp->num_chunk)) {
		gt3_error(GT3_ERR_INDEX, "GT3_seek() %d", dest);
		return -1;
	}

	if (GT3_isHistfile(fp))
		return seekhist(fp, dest);

	if (dest < fp->curr)		/* backward */
		GT3_rewind(fp);

	num = dest - fp->curr;
	while (num-- > 0 && !GT3_eof(fp))
		if (GT3_next(fp) < 0)
			return -1;			/* I/O error or broken file ? */

	if (num > 0) {
		gt3_error(GT3_ERR_INDEX, "GT3_seek() %d", dest);
		return -1;
	}

	if (fseeko(fp->fp, fp->off, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


int
GT3_skipZ(GT3_File *fp, int z)
{
	off_t off;

	if (z < 0 || z >= fp->dimlen[2]) {
		gt3_error(GT3_ERR_INDEX, "GT3_skipZ() %d", z);
		return -1;
	}

	off = fp->off + zslice_offset(fp, z);
	if (fseeko(fp->fp, off, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}



#ifdef TEST
int
test(const char *path)
{
	GT3_File *fp;
	int i;

	if ((fp = GT3_open(path)) == NULL) {
		return -1;
	}

	for (i = 0; i < 20; i++) {
		GT3_seek(fp, 1, SEEK_CUR);
		printf("%d %d %d\n", fp->curr, fp->num_chunk, (int)fp->off);
	}

	for (i = 0; i < 20; i++) {
		GT3_seek(fp, -1, SEEK_CUR);
		printf("%d %d %d\n", fp->curr, fp->num_chunk, (int)fp->off);
	}

	GT3_seek(fp, -9999, SEEK_SET); /* out-of-range */
	GT3_seek(fp, 9999, SEEK_SET); /* may be out-of-range */
	GT3_close(fp);
	return 0;
}

int
main(int argc, char **argv)
{
	GT3_setErrorFile(stderr);
	GT3_setPrintOnError(1);
	while (--argc > 0 && *++argv)
		test(*argv);
	return 0;
}
#endif
