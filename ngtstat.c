#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "functmpl.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif

static const char *usage_messages =
	"Usage: ngtstat [options] [files...]\n"
	"\n"
	"Print AVE, SD, MIN, MAX in gtool files.\n"
	"\n"
	"Options:\n"
	"    -h        print help message\n"
	"    -t LIST   specify a list of data numbers\n"
	"    -x RANGE  specify x-range\n"
	"    -y RANGE  specify y-range\n"
	"    -z RANGE  specify z-range\n"
	"\n"
	"    RANGE  := start[:[end]] | :[end]\n"
	"    LIST   := RANGE[,RANGE]*\n";

static int xrange[] = { 0, 0x7ffffff };
static int yrange[] = { 0, 0x7ffffff };
static int zrange[] = { 0, 0x7ffffff };
static int slicing = 0;

struct statics {
	int count;					/* the # of samples */
	double avr;					/* average */
	double sd;					/* standard deviation */
	double min, max;			/* min & max */
};


#define FUNCTMPL_PACK(TYPE, NAME) \
int \
NAME(void *ptr, const GT3_Varbuf *var) \
{ \
	const TYPE *data = (const TYPE *)var->data; \
	TYPE *output = (TYPE *)ptr; \
	TYPE miss = (TYPE)var->miss; \
	int cnt = 0; \
	int i; \
 \
	if (!slicing) \
		for (i = 0; i < var->dimlen[0] * var->dimlen[1]; i++) { \
			if (data[i] == miss) \
				continue; \
 \
			output[cnt++] = data[i]; \
		} \
	else { \
		int j, xmax, ymax; \
 \
		xmax = min(xrange[1], var->dimlen[0]); \
		ymax = min(yrange[1], var->dimlen[1]); \
		for (j = yrange[0]; j < ymax; j++) \
			for (i = xrange[0]; i < xmax; i++) { \
				if (data[j * var->dimlen[0] + i] == miss) \
					continue; \
 \
				output[cnt++] = data[j * var->dimlen[0] + i]; \
			} \
	} \
	return cnt; \
}


/*
 *   for float
 */
FUNCTMPL_PACK(float, pack_float)
FUNCTMPL_MINVAL(double, float, minvalf)
FUNCTMPL_MAXVAL(double, float, maxvalf)
FUNCTMPL_AVR(double, float, averagef)
FUNCTMPL_SDEVIATION(double, float, std_deviationf)

/*
 *  for double
 */
FUNCTMPL_PACK(double, pack_double)
FUNCTMPL_MINVAL(double, double, minval)
FUNCTMPL_MAXVAL(double, double, maxval)
FUNCTMPL_AVR(double, double, average)
FUNCTMPL_SDEVIATION(double, double, std_deviation)


static int
ngtstat_plane(struct statics *stat, const GT3_Varbuf *varbuf, void *work)
{
	int len;
	double avr, sd;
	int (*avr_func)(double *, const void *, int);
	int (*sd_func)(double *, const void *, double, int);
	int (*pack_func)(void *, const GT3_Varbuf *);
	double (*min_func)(const void *, int);
	double (*max_func)(const void *, int);

	if (varbuf->type == GT3_TYPE_FLOAT) {
		pack_func = pack_float;
		min_func = minvalf;
		max_func = maxvalf;
		avr_func = averagef;
		sd_func  = std_deviationf;
	} else {
		pack_func = pack_double;
		min_func = minval;
		max_func = maxval;
		avr_func = average;
		sd_func  = std_deviation;
	}

	len = pack_func(work, varbuf);
	if (len <= 0)
		return -1;

	avr_func(&avr, work, len);
	sd_func(&sd, work, avr, len);

	stat->count = len;
	stat->avr   = avr;
	stat->sd    = sd;
	stat->min   = min_func(work, len);
	stat->max   = max_func(work, len);

	return 0;
}


int
ngtstat_var(GT3_Varbuf *varbuf)
{
	char prefix[32], item[32];
	int i;
	void *work;
	struct statics stat;
	int rval = 0, zmax;

	/*
	 *  Trial read to fill varbuf.
	 *  Required before GT3_getVarAttrStr().
	 */
	if (GT3_readVarZ(varbuf, zrange[0]) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if ((work = malloc(varbuf->bufsize)) == NULL) {
		perror(NULL);
		return -1;
	}

	GT3_getVarAttrStr(item, sizeof item, varbuf, "ITEM");
	snprintf(prefix, sizeof prefix, "%5d %-8s",
			 varbuf->fp->curr + 1, item);

	zmax = min(zrange[1], varbuf->dimlen[2]);
	for (i = zrange[0]; i < zmax; i++) {
		GT3_readVarZ(varbuf, i);

		if (ngtstat_plane(&stat, varbuf, work) < 0) {
			rval = -1;
			break;
		}
		printf("%14s%3d %10.4g %10.4g %10.4g %10.4g %10d\n",
			   prefix,
			   i + 1,
			   stat.avr,
			   stat.sd,
			   stat.min,
			   stat.max,
			   stat.count);
		prefix[0] = '\0';
	}
	free(work);
	return rval;
}


int
ngtstat(const char *path, struct sequence *seq)
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
			if (ngtstat_var(var) < 0 || GT3_next(fp) < 0) {
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
			ngtstat_var(var);
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
	struct sequence *seq = NULL;
	int rval = 0;
	int ch;

	GT3_setProgname("ngtstat");
	while ((ch = getopt(argc, argv, "ht:x:y:z:")) != -1)
		switch (ch) {
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

	for (; argc > 0 && *argv; argc--, argv++) {
		if (seq)
			reinitSeq(seq, 1, 0x7fffffff);

		printf("# Filename: %s\n", *argv);
		printf("# %3s %-8s%3s %10s %10s %10s %10s %10s\n",
			   "No.", "ITEM", "Z", "AVE", "SD", "MIN", "MAX", "NUM");
		if (ngtstat(*argv, seq) < 0)
			rval = 1;
	}

	return rval;
}
