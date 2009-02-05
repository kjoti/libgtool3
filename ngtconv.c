/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtconv.c -- gtool3 format converter.
 */
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"

#define PROGNAME "ngtconv"


struct range {
	int str, end;
};

struct buffer {
	double *ptr;
	size_t reserved;
	size_t curr;
};


static char *xslice = NULL;
static char *yslice = NULL;
static struct sequence *zseq;
static struct buffer g_buffer;


static void
copy_to_buffer(struct buffer* buff, GT3_Varbuf *var, size_t off, size_t num)
{
	size_t ncopied;

	/* XXX: enough size allocated */
	assert(num <= buff->reserved - buff->curr);
	ncopied = GT3_copyVarDouble(buff->ptr + buff->curr, num, var, off, 1);
	buff->curr += ncopied;
}



static int
allocate_buffer(struct buffer *buf, size_t newsize)
{
	double *p;

	if (newsize > buf->reserved) {
		if ((p = malloc(sizeof(double) * newsize)) == NULL)
			return -1;

		free(buf->ptr);
		buf->ptr = p;
		buf->reserved = newsize;
	}
	buf->curr = 0;
	return 0;
}


static void
clip_range(struct range *range, const char *slice)
{
}



static int
conv_chunk(FILE *output, const char *dfmt, GT3_Varbuf *var, GT3_File *fp)
{
	GT3_HEADER head;
	struct range xrange, yrange;
	int zprev = -1, zstr = 1;
	int out_of_order = 0;
	int nx, ny, nz;
	int y, z;
	size_t offset, nelems;


	if (GT3_readHeader(&head, fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	xrange.str = yrange.str = 0;
	xrange.end = fp->dimlen[0];
	yrange.end = fp->dimlen[1];
	reinitSeq(zseq, 0, fp->dimlen[2] - 1);

	if (xslice)
		clip_range(&xrange, xslice);
	if (yslice)
		clip_range(&yrange, yslice);

	nx = xrange.end - xrange.str;
	ny = yrange.end - yrange.str;
	nz = countSeq(zseq);

	if (allocate_buffer(&g_buffer, nx * ny * nz) < 0) {
		logging(LOG_SYSERR, "");
		return -1;
	}

	while (nextSeq(zseq) > 0) {
		z = zseq->curr;

		if (GT3_readVarZ(var, z) < 0) {
			GT3_printErrorMessages(stderr);
			return -1;
		}

		if (xrange.str > 0 || xrange.end < fp->dimlen[0]) {
			nelems = nx;
			for (y = yrange.str; y < yrange.end; y++) {
				offset = nx * y + xrange.str;

				copy_to_buffer(&g_buffer, var, offset, nelems);
			}
		} else {
			offset = nx * yrange.str;
			nelems = nx * ny;

			copy_to_buffer(&g_buffer, var, offset, nelems);
		}

		out_of_order |= zprev >= 0 && z != zprev + 1;
		if (zprev < 0)
			zstr = z;
		zprev = z;
	}

	GT3_setHeaderInt(&head, "ASTR1", xrange.str + 1);
	GT3_setHeaderInt(&head, "ASTR2", yrange.str + 1);
	if (out_of_order) {
		logging(LOG_INFO, "zlevel: out of order");
		GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
		GT3_setHeaderInt(&head, "ASTR3", 1);
	} else
		GT3_setHeaderInt(&head, "ASTR3", zstr + 1);

	if (GT3_write(g_buffer.ptr, GT3_TYPE_DOUBLE,
				  nx, ny, nz,
				  &head, dfmt, output) < 0) {

		GT3_printErrorMessages(stderr);
		return -1;
	}
	return 0;
}


int
conv_file(const char *path, const char *fmt, FILE *output)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int rval;

	if ((fp = GT3_open(path)) == NULL
		|| (var = GT3_getVarbuf(fp)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	rval = 0;
	for (;;) {
		if (GT3_eof(fp))
			break;

		if (conv_chunk(output, fmt, var, fp) < 0) {
			rval = -1;
			break;
		}

		if (GT3_next(fp) < 0) {
			GT3_printErrorMessages(stderr);
			rval = -1;
			break;
		}
	}

	GT3_close(fp);
	return rval;
}


int
main(int argc, char **argv)
{
	zseq = initSeq(":", 0, 0x7ffffff);
	conv_file(argv[1], "MRX16", stdout);
	return 0;
}
