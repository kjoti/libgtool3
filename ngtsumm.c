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
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"

static const char *usage_messages =
	"Usage: ngtsumm [options] [files...]\n"
	"\n"
	"Print MIN,MAX,#Miss,#NaN,#Inf in gtool files.\n"
	"\n"
	"Options:\n"
	"    -h        print help message\n"
	"    -t LIST   specify a list of data numbers\n"
	"              LIST := S[,S[,S...]]\n"
	"              S    := N1[:N2[:N3]] | :[N2[:N3]] | ::[N3]\n"
	"              ex.)  :5        => 1 2 3 4 5\n"
	"                    2:10:2    => 2 4 6 8 10\n"
	"                    10:       => 10 11 ... LAST\n"
	"                    -5:-1     => LAST-4 LAST-3 ... LAST\n"
	"    -z        print for each z-plane\n";

static int slicing = 0;

struct data_profile {
	double vmin, vmax;
	int miss_cnt, nan_cnt, inf_cnt;
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
get_dataprof_float(const GT3_Varbuf *var, struct data_profile *prof)
{
	const float *data = (const float *)var->data;
	int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
	float miss;
	float minc, maxc;
	int i, len;

	len = var->dimlen[0] * var->dimlen[1];
	assert(var->bufsize >= 4 * len);

	miss = (float)var->miss;
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
get_dataprof_double(const GT3_Varbuf *var, struct data_profile *prof)
{
	const double *data = (const double *)var->data;
	int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
	double miss;
	double minc, maxc;
	int i, len;

	len = var->dimlen[0] * var->dimlen[1];
	assert(var->bufsize >= 8 * len);

	miss = var->miss;
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
	int z;
	struct data_profile prof;
	char item[17];
	void (*get_dataprof)(const GT3_Varbuf *, struct data_profile *);


	if (GT3_readVarZ(var, 0) < 0)
		return -1;

	GT3_getVarAttrStr(item, sizeof item, var, "ITEM");
	fprintf(output, " %-12s", item);

	get_dataprof = (var->type == GT3_TYPE_FLOAT)
		? get_dataprof_float
		: get_dataprof_double;

	init_profile(&prof);
	for (z = 0; z < var->dimlen[2]; z++) {
		if (GT3_readVarZ(var, z) < 0)
			return -1;

		get_dataprof(var, &prof);
		if (slicing) {
			fprintf(output, z == 0 ? " %4d" : " %22d", z + 1);
			print_profile(output, &prof);
			printf("\n");
			init_profile(&prof);
		}
	}

	if (!slicing) {
		print_profile(output, &prof);
		printf("\n");
	}
	return 0;
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


int
main(int argc, char **argv)
{
	int ch;
	struct sequence *seq = NULL;

	while ((ch = getopt(argc, argv, "t:z")) != EOF)
		switch (ch) {
		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;
		case 'z':
			slicing = 1;
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
		if (slicing)
			printf("%-5s %12s %4s %14s %14s %10s %5s %5s\n",
				   "#", "ITEM", "Z", "MIN", "MAX", "MISS", "NaN", "Inf");
		else
			printf("%-5s %12s %14s %14s %10s %5s %5s\n",
				   "#", "ITEM", "MIN", "MAX", "MISS", "NaN", "Inf");

		summ_file(*argv, seq);
	}
	return 0;
}
