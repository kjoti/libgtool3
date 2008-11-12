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


/*
 *  chunk size of UR4(size==4) or UR8(size==8).
 */
static size_t
chunk_size_std(size_t nelem, size_t size)
{
	return 4 * sizeof(fort_size_t) /* 2 records */
		+ GT3_HEADER_SIZE		/* header */
		+ size * nelem;			/* body */
}


/*
 *  chunk size of URC or URC2.
 */
static size_t
chunk_size_urc(size_t nelem, int nz)
{
	return GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+ (8 + 4 + 4 + 2 * nelem + 8 * sizeof(fort_size_t)) * nz;
}


/*
 *  chunk size of URX.
 */
static size_t
chunk_size_urx(size_t nelem, int nz, int nbit)
{
	return 6 * sizeof(fort_size_t)	/* 3 records */
		+ GT3_HEADER_SIZE		/* header */
		+ 2 * 8 * nz			/* DMA */
		+ 4 * pack32_len(nelem, nbit) * nz;	/* body (packed)  */
}


/*
 *  chunk size of MR4 or MR8.
 *
 *  FIXME: It is assumed that the current file position is next to
 *  GTOOL3 header record.
 */
static size_t
chunk_size_mask(size_t nelem, size_t size, GT3_File *fp)
{
	uint32_t num[] = {0, 0};

	fread(&num, 4, 2, fp->fp);
	if (IS_LITTLE_ENDIAN)
		reverse_words(&num, 2);

	return GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+  4 + 2 * sizeof(fort_size_t)
		+  4 * ((nelem + 31) / 32) + 2 * sizeof(fort_size_t)
		+  size * num[1] + 2 * sizeof(fort_size_t);
}


/*
 *  chunk size of MRX.
 *
 *  FIXME: It is assumed that the current file position is next to
 *  GTOOL3 header record.
 */
static size_t
chunk_size_maskx(size_t nelem, int nz, int nbit, GT3_File *fp)
{
	uint32_t num[] = {0, 0};

	fread(&num, 4, 2, fp->fp);
	if (IS_LITTLE_ENDIAN)
		reverse_words(&num, 2);

	return 14 * sizeof(fort_size_t)  /* 7 records */
		+ GT3_HEADER_SIZE
		+ 4
		+ 4 * nz
		+ 4 * nz
		+ 2 * 8 * nz
		+ 4 * pack32_len(nelem, 1) * nz
		+ 4 * num[1];
}


/*
 *  chunk_size() returns a current chunk-size.
 *  The chunk comprises the gtool3-header and the data-body.
 */
static size_t
chunk_size(GT3_File *fp)
{
	int fmt;
	size_t siz = 0, nelem;


	fmt = (int)(fp->fmt & GT3_FMT_MASK);
	switch (fmt) {
	case GT3_FMT_UR4:
		nelem = fp->dimlen[0] * fp->dimlen[1] * fp->dimlen[2];
		siz = chunk_size_std(nelem, 4);
		break;

	case GT3_FMT_URC:
	case GT3_FMT_URC1:
		nelem = fp->dimlen[0] * fp->dimlen[1];
		siz = chunk_size_urc(nelem, fp->dimlen[2]);
		break;

	case GT3_FMT_UR8:
		nelem = fp->dimlen[0] * fp->dimlen[1] * fp->dimlen[2];
		siz = chunk_size_std(nelem, 8);
		break;

	case GT3_FMT_URX:
		nelem = fp->dimlen[0] * fp->dimlen[1];
		siz = chunk_size_urx(nelem, fp->dimlen[2], fp->fmt >> GT3_FMT_MBIT);
		break;

	case GT3_FMT_MR4:
		nelem = fp->dimlen[0] * fp->dimlen[1] * fp->dimlen[2];
		siz = chunk_size_mask(nelem, 4, fp);
		break;

	case GT3_FMT_MR8:
		nelem = fp->dimlen[0] * fp->dimlen[1] * fp->dimlen[2];
		siz = chunk_size_mask(nelem, 8, fp);
		break;

	case GT3_FMT_MRX:
		nelem = fp->dimlen[0] * fp->dimlen[1];
		siz = chunk_size_maskx(nelem, fp->dimlen[2],
							   fp->fmt >> GT3_FMT_MBIT, fp);
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
	fp->fmt = fmt;
	fp->dimlen[0] = dim[0];
	fp->dimlen[1] = dim[1];
	fp->dimlen[2] = dim[2];
	fp->chsize = chunk_size(fp); /* XXX */

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
	char temp[GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)];
	size_t siz;

	if ((siz = fread(temp, 1, sizeof temp, fp)) != sizeof temp
		|| temp[0]    != 0 || temp[1]    != 0
		|| temp[2]    != 4 || temp[3]    != 0
		|| temp[1028] != 0 || temp[1029] != 0
		|| temp[1030] != 4 || temp[1031] != 0
		|| memcmp(temp + 4, magic, 16) != 0) {

		return -1;
	}

	memcpy(header->h, temp + sizeof(fort_size_t), GT3_HEADER_SIZE);
	return 0;
}


/*
 *  offset of each z-slice indexed 'zpos'.
 */
static off_t
zslice_offset(GT3_File *fp, int zpos)
{
	off_t off, nelem;

	off = GT3_HEADER_SIZE + 2 * sizeof(fort_size_t);
	nelem = (off_t)fp->dimlen[0] * fp->dimlen[1];

	switch (fp->fmt & GT3_FMT_MASK) {
	case GT3_FMT_UR4:
		off += sizeof(fort_size_t) + 4 * nelem * zpos;
		break;
	case GT3_FMT_URC:
	case GT3_FMT_URC1:
		off += (8 + 4 + 4 + 2 * nelem + 8 * sizeof(fort_size_t)) * zpos;
		break;
	case GT3_FMT_UR8:
		off += sizeof(fort_size_t) + 8 * nelem * zpos;
		break;
	case GT3_FMT_URX:
		off += 2 * sizeof(double) * fp->dimlen[2] + 2 * sizeof(fort_size_t);
		off += sizeof(fort_size_t);
		off += zpos * sizeof(uint32_t)
			* pack32_len(nelem, fp->fmt >> GT3_FMT_MBIT);
		break;
	case GT3_FMT_MR4:
	case GT3_FMT_MR8:
	case GT3_FMT_MRX:
		off += 4 + 2 * sizeof(fort_size_t);
		off += 4 * pack32_len(nelem * fp->dimlen[2], 1)
			+ 2 * sizeof(fort_size_t);
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
	return fp->mode & GT3_CONST_CHUNK_SIZE;
}


int
GT3_format(const char *str)
{
	struct { const char *key; int val; } ftab[] = {
		{ "UR4",  GT3_FMT_UR4   },
		{ "URC2", GT3_FMT_URC   },
		{ "URC",  GT3_FMT_URC1  },
		{ "UI2",  GT3_FMT_URC1  },  /* deprecated name */
		{ "UR8",  GT3_FMT_UR8   },
		{ "MR4",  GT3_FMT_MR4   },
		{ "MR8",  GT3_FMT_MR8   },
	};
	int i;

	for (i = 0; i < sizeof ftab / sizeof ftab[0]; i++)
		if (strcmp(ftab[i].key, str) == 0)
			return ftab[i].val;

	/* URX */
	if (strncmp(str, "URX", 3) == 0) {
		unsigned nbits;
		char *endptr;

		nbits = (unsigned)strtol(str + 3, &endptr, 10);
		if (endptr == str + 3
			|| *endptr != '\0'
			|| nbits > 31)
			return -1;

		return GT3_FMT_URX | nbits << GT3_FMT_MBIT;
	}

	/* MRX */
	if (strncmp(str, "MRX", 3) == 0) {
		unsigned nbits;
		char *endptr;

		nbits = (unsigned)strtol(str + 3, &endptr, 10);
		if (endptr == str + 3
			|| *endptr != '\0'
			|| nbits > 31)
			return -1;

		return GT3_FMT_MRX | nbits << GT3_FMT_MBIT;
	}

	return -1;
}


int
GT3_format_string(char *str, int fmt)
{
	struct { int key; const char *value; } dict[] = {
		{ GT3_FMT_UR4,  "UR4"  },
		{ GT3_FMT_URC,  "URC2" },
		{ GT3_FMT_URC1, "URC"  },
		{ GT3_FMT_UR8,  "UR8"  },
		{ GT3_FMT_URX,  "URX"  },
		{ GT3_FMT_MR4,  "MR4"  },
		{ GT3_FMT_MR8,  "MR8"  },
		{ GT3_FMT_MRX,  "MRX"  }
	};
	int i;
	unsigned nbits;

	for (i = 0; i < sizeof dict / sizeof dict[0]; i++)
		if (dict[i].key == (fmt & GT3_FMT_MASK))
			break;

	if (i == sizeof dict / sizeof dict[0]) {
		gt3_error(GT3_ERR_CALL, "%d: Invalid format id", fmt);
		return -1;
	}
	if (dict[i].key == GT3_FMT_URX || dict[i].key == GT3_FMT_MRX) {
		nbits = (unsigned)fmt >> GT3_FMT_MBIT;

		if (nbits > 31) {
			gt3_error(GT3_ERR_CALL, "%d: Invalid format id (nbit)", fmt);
			return -1;
		}
		sprintf(str, "%s%02u", dict[i].value, nbits);
	} else
		sprintf(str, "%s", dict[i].value);

	return 0;
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


int
GT3_getNumChunk(const GT3_File *fp)
{
	return (fp->num_chunk >= 0) ? fp->num_chunk : GT3_countChunk(fp->path);
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
	gp->mask   = NULL;

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
	 *  check if this is a uniform-file, whose all chunks
	 *  are in the same size.
	 *
	 */
	if (gp->size % gp->chsize == 0) {
		gp->mode |= GT3_CONST_CHUNK_SIZE;
		gp->num_chunk = gp->size / gp->chsize;
	} else
		gp->num_chunk = GT3_countChunk(gp->path);

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
	if (nextoff > fp->size) {
		gt3_error(GT3_ERR_BROKEN, "%s", fp->path);
		return -1;
	}

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
		if (fp->mask)
			GT3_freeMask(fp->mask);
		free(fp->mask);
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


#ifdef TEST_MAIN
int
main(int argc, char **argv)
{
	const char *name[] = {
		"UR4", "URC", "URC2", "UR8",
		"MR4", "MR8",
		"URX01", "URX12", "URX31",
		"MRX01", "URX12", "MRX31"
	};
	int i, fmt, rval;
	char dfmt[17];


	for (i = 0; i < sizeof name / sizeof name[0]; i++) {
		fmt = GT3_format(name[i]);

		assert(fmt >= 0);

		rval = GT3_format_string(dfmt, fmt);
		assert(rval == 0);
		assert(strcmp(name[i], dfmt) == 0);

	}

	return 0;
}
#endif
