/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtsumm.c -- print data summary.
 *
 *  $Date: 2006/11/16 02:16:25 $
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif


static const char *usage_messages =
	"Usage: ngtsumm [options] [files...]\n"
	"\n"
	"Print MIN,MAX,#Miss,#NaN,#Inf in gtool files.\n"
	"\n"
	"Options:\n"
	"    -h        print help message\n"
	"    -l        print for each z-plane\n"
	"    -t LIST   specify a list of data numbers\n"
	"    -x RANGE  specify x-range\n"
	"    -y RANGE  specify y-range\n"
	"    -z RANGE  specify z-range\n"
	"\n"
	"    RANGE  := start[:[end]] | :[end]\n"
	"    LIST   := RANGE[,RANGE]*\n";

static int each_plane = 0;
static int xrange[] = { 0, 0x7ffffff };
static int yrange[] = { 0, 0x7ffffff };
static int zrange[] = { 0, 0x7ffffff };
static int slicing = 0;


struct data_profile {
	double vmin, vmax;
	int miss_cnt, nan_cnt, inf_cnt;

	double miss; /* used as input data */
};


void
init_profile(struct data_profile *prof)
{
	prof->vmin = HUGE_VAL;
	prof->vmax = -HUGE_VAL;
	prof->miss_cnt = 0;
	prof->nan_cnt  = 0;
	prof->inf_cnt  = 0;
}


void
get_dataprof_float(const void *ptr, int len, struct data_profile *prof)
{
	const float *data = (const float *)ptr;
	int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
	float miss;
	float minc, maxc;
	int i;

	miss = (float)prof->miss;
	minc = (float)prof->vmin;
	maxc = (float)prof->vmax;

	for (i = 0; i < len; i++) {
		if (data[i] == miss) {
			mcnt++;
			continue;
		}
		if (isnan(data[i])) {	/* NaN */
			nan_cnt++;
			continue;
		}
		if (data[i] <= -HUGE_VAL) { /* -Inf. */
			minf++;
			continue;
		}
		if (data[i] >= HUGE_VAL) { /* +Inf. */
			pinf++;
			continue;
		}

		if (data[i] < minc)
			minc = data[i];
		if (data[i] > maxc)
			maxc = data[i];
	}

	prof->vmin = minc;
	prof->vmax = maxc;

	prof->miss_cnt += mcnt;
	prof->nan_cnt  += nan_cnt;
	prof->inf_cnt  += pinf + minf;
}


void
get_dataprof_double(const void *ptr, int len, struct data_profile *prof)
{
	const double *data = (const double *)ptr;
	int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
	double miss;
	double minc, maxc;
	int i;

	miss = prof->miss;
	minc = prof->vmin;
	maxc = prof->vmax;

	for (i = 0; i < len; i++) {
		if (data[i] == miss) {
			mcnt++;
			continue;
		}
		if (isnan(data[i])) {	/* NaN */
			nan_cnt++;
			continue;
		}
		if (data[i] <= -HUGE_VAL) { /* -Inf. */
			minf++;
			continue;
		}
		if (data[i] >= HUGE_VAL) { /* +Inf. */
			pinf++;
			continue;
		}

		if (data[i] < minc)
			minc = data[i];
		if (data[i] > maxc)
			maxc = data[i];
	}

	prof->vmin = minc;
	prof->vmax = maxc;

	prof->miss_cnt += mcnt;
	prof->nan_cnt  += nan_cnt;
	prof->inf_cnt  += pinf + minf;
}


int
pack_slice_float(void *ptr, const GT3_Varbuf *var)
{
	const float *data = (const float *)var->data;
	float *output = (float *)ptr;
	int cnt = 0;
	int i, j;
	int xmax, ymax;

	xmax = min(xrange[1], var->dimlen[0]);
	ymax = min(yrange[1], var->dimlen[1]);
	for (j = yrange[0]; j < ymax; j++)
		for (i = xrange[0]; i < xmax; i++)
			output[cnt++] = data[j * var->dimlen[0] + i];

	return cnt;
}


int
pack_slice_double(void *ptr, const GT3_Varbuf *var)
{
	const double *data = (const double *)var->data;
	double *output = (double *)ptr;
	int cnt = 0;
	int i, j;
	int xmax, ymax;

	xmax = min(xrange[1], var->dimlen[0]);
	ymax = min(yrange[1], var->dimlen[1]);
	for (j = yrange[0]; j < ymax; j++)
		for (i = xrange[0]; i < xmax; i++)
			output[cnt++] = data[j * var->dimlen[0] + i];

	return cnt;
}


void
print_profile(FILE *output, struct data_profile *prof)
{
	fprintf(output,
			" %14.5g %14.5g %10d %5d %5d",
			prof->vmin, prof->vmax,
			prof->miss_cnt, prof->nan_cnt, prof->inf_cnt);
}


int
print_summary(FILE *output, GT3_Varbuf *var)
{
	int z, zmax;
	struct data_profile prof;
	char item[17];
	void (*get_dataprof)(const void *, int, struct data_profile *);
	int (*pack_slice)(void *, const GT3_Varbuf *);
	int rval, len = 0, elem_size;
	void *data;

	if (GT3_readVarZ(var, 0) < 0)
		return -1;

	GT3_getVarAttrStr(item, sizeof item, var, "ITEM");
	fprintf(output, " %-12s", item);

	if (var->type == GT3_TYPE_FLOAT) {
		get_dataprof = get_dataprof_float;
		pack_slice   = pack_slice_float;
		elem_size    = 4;
	} else {
		get_dataprof = get_dataprof_double;
		pack_slice   = pack_slice_double;
		elem_size    = 8;
	}

	if (slicing) {
		int xlen, ylen;

		xlen = min(var->dimlen[0], xrange[1] - xrange[0]);
		ylen = min(var->dimlen[1], yrange[1] - yrange[0]);
		if (xlen <= 0 || ylen <= 0
			|| (data = malloc(xlen * ylen * elem_size)) == NULL)
			return -1;
	} else {
		data = var->data;
		len  = var->dimlen[0] * var->dimlen[1];
	}

	init_profile(&prof);
	prof.miss = var->miss;

	rval = -1;
	zmax = min(zrange[1], var->dimlen[2]);
	for (z = zrange[0]; z < zmax; z++) {
		if ((rval = GT3_readVarZ(var, z)) < 0)
			break;

		if (slicing)
			len = pack_slice(data, var);

		get_dataprof(data, len, &prof);
		if (each_plane) {
			fprintf(output, z == zrange[0] ? " %4d" : " %22d", z + 1);
			print_profile(output, &prof);
			printf("\n");
			init_profile(&prof);
		}
	}

	if (slicing)
		free(data);

	if (rval == 0 && !each_plane) {
		print_profile(output, &prof);
		printf("\n");
	}
	return rval;
}


int
summ_file(const char *path, struct sequence *seq)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int stat;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if ((var = GT3_getVarbuf(fp)) == NULL) {
		GT3_close(fp);
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if (seq == NULL)
		while (!GT3_eof(fp)) {
			printf("%5d", fp->curr + 1);

			if (print_summary(stdout, var) < 0 || GT3_next(fp) < 0) {
				GT3_printErrorMessages(stderr);
				break;
			}
		}
	else
		while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
			if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
				break;

			if (stat == ITER_OUTRANGE)
				continue;

			printf("%5d", fp->curr + 1);
			print_summary(stdout, var);
		}

	GT3_freeVarbuf(var);
	GT3_close(fp);
	return 0;
}


static int
get_ints(int vals[], int numval, const char *str, char delim)
{
	const char *tail = str + strlen(str);
	int cnt, num;
	char *endptr;

	cnt = 0;
	while (str < tail && cnt < numval) {
		num = (int)strtol(str, &endptr, 0);

		if (*endptr != delim && *endptr != '\0')
			return -1;			/* invalid char  */

		if (*str != delim)
			vals[cnt] = num;
		str = endptr + 1;
		cnt++;
	}
	return cnt;
}


static void
set_range(int range[], const char *str)
{
	get_ints(range, 2, str, ':');

	/*
	 *  XXX
	 *  transform
	 *   FROM  1-offset and closed bound    [X,Y] => do i = X, Y.
	 *   TO    0-offset and semi-open bound [X,Y) => for (i = X; i < Y; i++).
	 */
	range[0]--;
	if (range[0] < 0)
		range[0] = 0;

	if (!strchr(str, ':'))
		range[1] = range[0] + 1;
}


int
main(int argc, char **argv)
{
	int ch;
	struct sequence *seq = NULL;

	while ((ch = getopt(argc, argv, "hlt:x:y:z:")) != EOF)
		switch (ch) {
		case 'l':
			each_plane = 1;
			break;

		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;

		case 'x':
			slicing = 1;
			set_range(xrange, optarg);
			break;

		case 'y':
			slicing = 1;
			set_range(yrange, optarg);
			break;

		case 'z':
			set_range(zrange, optarg);
			break;

		case 'h':
		default:
			fprintf(stderr, usage_messages);
			exit(1);
			break;
		}

	argc -= optind;
	argv += optind;

	for (; argc > 0 && argv; --argc, ++argv) {
		printf("# Filename: %s\n", *argv);
		if (each_plane)
			printf("%-5s %12s %4s %14s %14s %10s %5s %5s\n",
				   "#", "ITEM", "Z", "MIN", "MAX", "MISS", "NaN", "Inf");
		else
			printf("%-5s %12s %14s %14s %10s %5s %5s\n",
				   "#", "ITEM", "MIN", "MAX", "MISS", "NaN", "Inf");

		if (seq)
			reinitSeq(seq, 1, 0x7fffffff);
		summ_file(*argv, seq);
	}
	return 0;
}
