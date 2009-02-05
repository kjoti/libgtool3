/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtconv.c -- gtool3 format converter.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
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
#define RANGE_MAX 0x7fffffff

#ifndef max
#  define max(a, b) ((a)>(b) ? (a) :(b))
#endif
#ifndef min
#  define min(a, b) ((a)<(b) ? (a) :(b))
#endif

struct range {
	int str, end;
};

struct buffer {
	double *ptr;
	size_t reserved;
	size_t curr;
};


static struct range *g_xrange = NULL;
static struct range *g_yrange = NULL;
static struct sequence *zseq = NULL;
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
clip_range(struct range *range, const struct range *clip)
{
	range->str = max(range->str, clip->str);
	range->end = min(range->end, clip->end);
}


static int
conv_chunk(FILE *output, const char *dfmt, GT3_Varbuf *var, GT3_File *fp)
{
	GT3_HEADER head;
	struct range xrange, yrange;
	int zprev = -1, zstr = 1;
	int out_of_order = 0;
	int nx, ny, nz, zcnt;
	int y, z;
	size_t offset, nelems;


	if (GT3_readHeader(&head, fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	xrange.str = yrange.str = 0;
	xrange.end = fp->dimlen[0];
	yrange.end = fp->dimlen[1];
	reinitSeq(zseq, 1, fp->dimlen[2]);

	if (g_xrange)
		clip_range(&xrange, g_xrange);
	if (g_yrange)
		clip_range(&yrange, g_yrange);

	nx = xrange.end - xrange.str;
	ny = yrange.end - yrange.str;
	nz = countSeq(zseq);

	if (nx <= 0 || ny <= 0) {
		logging(LOG_WARN, "empty horizontal domain");
		return 0;
	}

	if (allocate_buffer(&g_buffer, nx * ny * nz) < 0) {
		logging(LOG_SYSERR, "");
		return -1;
	}

	zcnt = 0;
	while (nextSeq(zseq) > 0) {
		z = zseq->curr - 1;
		if (z < 0 || z >= fp->dimlen[2])
			continue;

		if (GT3_readVarZ(var, z) < 0) {
			GT3_printErrorMessages(stderr);
			return -1;
		}

		if (xrange.str > 0 || xrange.end < fp->dimlen[0]) {
			nelems = nx;
			for (y = yrange.str; y < yrange.end; y++) {
				offset = fp->dimlen[0] * y + xrange.str;

				copy_to_buffer(&g_buffer, var, offset, nelems);
			}
		} else {
			offset = fp->dimlen[0] * yrange.str;
			nelems = nx * ny;

			copy_to_buffer(&g_buffer, var, offset, nelems);
		}

		out_of_order |= zprev >= 0 && z != zprev + 1;
		if (zprev < 0)
			zstr = z;
		zprev = z;
		zcnt++;
	}

	if (zcnt == 0) {
		logging(LOG_WARN, "empty z-level");
		return 0;
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
				  nx, ny, zcnt,
				  &head, dfmt, output) < 0) {

		GT3_printErrorMessages(stderr);
		return -1;
	}
	return 0;
}


int
conv_file(const char *path, const char *fmt, FILE *output,
		  struct sequence *seq)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int rval, stat;

	if ((fp = GT3_open(path)) == NULL
		|| (var = GT3_getVarbuf(fp)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	rval = 0;
	if (seq == NULL)
		while (!GT3_eof(fp)) {
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
	else
		while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
			if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
				rval = -1;
				break;
			}
			if (stat == ITER_OUTRANGE)
				continue;

			if (conv_chunk(output, fmt, var, fp) < 0) {
				rval = -1;
				break;
			}
		}

	GT3_close(fp);
	return rval;
}


static struct range *
set_range(struct range *range, const char *str)
{
	int vals[] = { 1, RANGE_MAX };

	if (str == NULL || get_ints(vals, 2, str, ':') < 0)
		return NULL;

	range->str = vals[0] - 1;
	range->end = vals[1];
	return range;
}


static char *
toupper_string(char *str)
{
	char *p = str;

	while ((*p = toupper(*p)))
		p++;
	return str;
}


void
usage(void)
{
	const char *usage_message =
		"Usage: " PROGNAME " [options] inputfile [outputfile]\n"
		"\n"
		"File format converter.\n"
		"\n"
		"Options:\n"
		"    -h        print help message\n"
		"    -a        output in append mode\n"
		"    -f        specify output format (default: UR4)\n"
		"    -t LIST   specify data No.\n"
		"    -x RANGE  specify X-range\n"
		"    -y RANGE  specify Y-range\n"
		"    -z LIST   specify Z-planes\n";

	fprintf(stderr, "%s\n", GT3_version());
	fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
	int ch;
	struct sequence *tseq = NULL;
	int rval;
	const char *mode = "wb";
	const char *fmt = "UR4";
	char *outpath = "gtool.out";
	FILE *output;
	struct range xr, yr;

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);
	while ((ch = getopt(argc, argv, "af:t:x:y:z:h")) != -1)
		switch (ch) {
		case 'a':
			mode = "ab";
			break;
		case 'f':
			toupper_string(optarg);
			fmt = strdup(optarg);
			break;
		case 't':
			if ((tseq = initSeq(optarg, 0, RANGE_MAX)) == NULL) {
				logging(LOG_SYSERR, "");
				exit(1);
			}
			break;
		case 'x':
			g_xrange = set_range(&xr, optarg);
			if (g_xrange == NULL) {
				logging(LOG_ERR, "-x: invalid argument: %s", optarg);
				exit(1);
			}
			break;
		case 'y':
			g_yrange = set_range(&yr, optarg);
			if (g_yrange == NULL) {
				logging(LOG_ERR, "-y: invalid argument: %s", optarg);
				exit(1);
			}
			break;
		case 'z':
			zseq = initSeq(optarg, 1, RANGE_MAX);
			break;

		case 'h':
		default:
			usage();
			exit(0);
		}

	argc -= optind;
	argv += optind;
	if (argc == 0) {
		usage();
		exit(1);
	}

	if (argc > 1)
		outpath = argv[1];

	if ((output = fopen(outpath, mode)) == NULL) {
		logging(LOG_SYSERR, outpath);
		exit(1);
	}

	if (!zseq)
		zseq = initSeq(":", 1, RANGE_MAX);

	rval = conv_file(argv[0], fmt, output, tseq);
	return (rval < 0) ? 1 : 0;
}
