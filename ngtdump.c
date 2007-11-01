/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtdump.c -- print data.
 */
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif


#define PROGNAME "ngtdump"

#define DATA(vbuf, i) \
	(((vbuf)->type == GT3_TYPE_DOUBLE) \
	? *((double *)((vbuf)->data) + (i)) \
	: *((float *) ((vbuf)->data) + (i)) )

#define ISMISS(vbuf, i) \
	(((vbuf)->type == GT3_TYPE_DOUBLE) \
	? *((double *)((vbuf)->data) + (i)) == vbuf->miss \
	: *((float *) ((vbuf)->data) + (i)) == (float)vbuf->miss )

/*
 * global range setting.
 */
static int xrange[] = { 0, 0x7fffffff };
static int yrange[] = { 0, 0x7fffffff };
static int zrange[] = { 0, 0x7fffffff };


char *
snprintf_date(char *buf, size_t len, const GT3_Date *date)
{
	snprintf(buf, len, "%d-%02d-%02d %02d:%02d:%02d",
			date->year, date->mon, date->day,
			date->hour, date->min, date->sec);
		
	return buf;
}


int
get_range_in_chunk(int x[], int y[], int z[], const GT3_File *fp)
{
	x[0] = xrange[0];
	y[0] = yrange[0];
	z[0] = zrange[0];
	x[1] = min(fp->dimlen[0], xrange[1]);
	y[1] = min(fp->dimlen[1], yrange[1]);
	z[1] = min(fp->dimlen[2], zrange[1]);

	return x[1] > x[0] && y[1] > y[0] && z[1] > z[0];
}


int
dump_info(GT3_File *fp)
{
	GT3_HEADER head;
	char hbuf[33];
	GT3_Date date;

	if (GT3_readHeader(&head, fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	printf("#\n");
	printf("#       Data No.: %d\n", fp->curr + 1);
	GT3_copyHeaderItem(hbuf, sizeof hbuf, &head, "TITLE");
	printf("#          TITLE: %s\n", hbuf);

	GT3_decodeHeaderDate(&date, &head, "DATE");
	snprintf_date(hbuf, sizeof hbuf, &date);
	printf("#           DATE: %s\n", hbuf);
	printf("#\n");
	return 0;
}

void
set_dimvalue(char *hbuf, size_t len, GT3_Dim *dim, int idx)
{
	hbuf[0] = '\0';
	if (dim)
		snprintf(hbuf, len, "%18.10g", dim->values[idx]);
}


int
dump_var(GT3_Varbuf *var)
{
	int x, y, z, n, ij;
	GT3_HEADER head;
	GT3_Dim *dim[] = { NULL, NULL, NULL };
	const char *dimname[] = { "AITM1", "AITM2", "AITM3" }; 
	char vstr[32];
	unsigned missf;
	double val;
	int xr[2], yr[2], zr[2];
	int xoff = 1, yoff = 1, zoff = 1;
	char hbuf[17];
	char dimv[3][32];
	char items[4][32];


	GT3_readHeader(&head, var->fp);
	for (n = 0; n < 3; n++) {
		items[n][0] = '\0';
		GT3_copyHeaderItem(hbuf, sizeof hbuf, &head, dimname[n]);
		if (hbuf[0] != '\0' && strcmp(hbuf, "SFC1") != 0) {
			snprintf(items[n], sizeof items[n], "%18s", hbuf);
			if ((dim[n] = GT3_getDim(hbuf)) == NULL) {
				GT3_printErrorMessages(stderr);
				logging(LOG_ERR, "%s: Unknown axis name.", hbuf);

				snprintf(items[n], sizeof items[n], "%17s?", hbuf);

				/* use NUMBERXXX */
				snprintf(hbuf, sizeof hbuf,
						 "NUMBER%d", var->fp->dimlen[n]);
				dim[n] = GT3_getDim(hbuf);
			}
		}
	}

	if (get_range_in_chunk(xr, yr, zr, var->fp)) {
		GT3_copyHeaderItem(hbuf, sizeof hbuf, &head, "ITEM");
		snprintf(items[3], sizeof items[3], "%17s", hbuf);
		printf("#%s%s%s%s\n", items[0], items[1], items[2], items[3]);
		GT3_decodeHeaderInt(&xoff, &head, "ASTR1");
		GT3_decodeHeaderInt(&yoff, &head, "ASTR2");
		GT3_decodeHeaderInt(&zoff, &head, "ASTR3");
		xoff--;
		yoff--;
		zoff--;

		for (z = zr[0]; z < zr[1]; z++) {
			GT3_readVarZ(var, z);

			set_dimvalue(dimv[2], sizeof dimv[2], dim[2], z + zoff);
			for (y = yr[0]; y < yr[1]; y++) {
				if (y > yr[0])
					printf("\n");
				set_dimvalue(dimv[1], sizeof dimv[1], dim[1], y + yoff);
				for (x = xr[0]; x < xr[1]; x++) {
					set_dimvalue(dimv[0], sizeof dimv[0], dim[0], x + xoff);
					ij = x + var->dimlen[0] * y;
					val = DATA(var, ij);
					missf = ISMISS(var, ij);

					vstr[0] = '-';
					vstr[1] = '\0';
					if (!missf)
						snprintf(vstr, sizeof vstr, "%17.9g", val);

					printf(" %s%s%s%17s\n", 
						   dimv[0], dimv[1], dimv[2], vstr);
				}
			}
		}
	} else
		printf("#%s\n", "No Data in specified region.\n");

	GT3_freeDim(dim[0]);
	GT3_freeDim(dim[1]);
	GT3_freeDim(dim[2]);
	return 0;
}


int
ngtdump(const char *path, struct sequence *seq)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int rval = 0;
	int stat;

	if ((fp = GT3_open(path)) == NULL
		|| (var = GT3_getVarbuf(fp)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	printf("###\n# Filename: %s\n", path);
	if (seq == NULL) {
		while (!GT3_eof(fp)) {
			dump_info(fp);
			dump_var(var);
			if (GT3_next(fp) < 0) {
				GT3_printErrorMessages(stderr);
				rval = -1;
				break;
			}
		}
	} else {
		while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
			if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
				break;

			if (stat == ITER_OUTRANGE)
				continue;

			dump_info(fp);
			dump_var(var);
		}
	}
	GT3_freeVarbuf(var);
	GT3_close(fp);
	return rval;
}


static int
set_range(int range[], const char *str)
{
	int nf;

	if ((nf = get_ints(range, 2, str, ':')) < 0)
		return -1;

	/*
	 *  XXX
	 *  transform
	 *   FROM  1-offset and closed bound    [X,Y] => do i = X, Y.
	 *   TO    0-offset and semi-open bound [X,Y) => for (i = X; i < Y; i++).
	 */
	range[0]--;
	if (range[0] < 0)
		range[0] = 0;
	if (nf == 1)
		range[1] = range[0] + 1;
	return 0;
}


void
usage(void)
{
}


int
main(int argc, char **argv)
{
	struct sequence *seq = NULL;
	int ch;
	int rval = 0;

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);
	while ((ch = getopt(argc, argv, "t:x:y:z:h")) != -1)
		switch (ch) {
		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;

		case 'x':
			if (set_range(xrange, optarg) < 0) {
				logging(LOG_ERR, "%s: Invalid argument", optarg);
				exit(1);
			}
			break;

		case 'y':
			if (set_range(yrange, optarg) < 0) {
				logging(LOG_ERR, "%s: Invalid argument", optarg);
				exit(1);
			}
			break;

		case 'z':
			if (set_range(zrange, optarg) < 0) {
				logging(LOG_ERR, "%s: Invalid argument", optarg);
				exit(1);
			}
			break;
		case 'h':
		default:
			usage();
			exit(1);
			break;
		}

	argc -= optind;
	argv += optind;
	for (; argc > 0 && *argv; argc--, argv++) {
		if (seq)
			reinitSeq(seq, 1, 0x7fffffff);

		if (ngtdump(*argv, seq) < 0) {
			rval = 1;
			break;
		}
	}
	return rval;
}
