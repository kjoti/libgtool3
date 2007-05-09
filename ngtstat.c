/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtstat.c -- print statistical info in gtool-files.
 *
 */
#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "functmpl.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtstat"

static const char *usage_messages =
	"Usage: ngtstat [options] [files...]\n"
	"\n"
	"Print AVE, SD, MIN, MAX in gtool files.\n"
	"\n"
	"Options:\n"
	"    -h        print help message\n"
	"    -a        display info for all z-planes\n"
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
static int each_plane = 1;

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
	int i, j, xmax, ymax; \
 \
	if (!slicing) \
		for (i = 0; i < var->dimlen[0] * var->dimlen[1]; i++) { \
			if (data[i] == miss) \
				continue; \
 \
			*output++ = data[i]; \
		} \
	else { \
		xmax = min(xrange[1], var->dimlen[0]); \
		ymax = min(yrange[1], var->dimlen[1]); \
		for (j = yrange[0]; j < ymax; j++) \
			for (i = xrange[0]; i < xmax; i++) { \
				if (data[j * var->dimlen[0] + i] == miss) \
					continue; \
 \
				*output++ = data[j * var->dimlen[0] + i]; \
			} \
	} \
	return output - (TYPE *)ptr;								\
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


static void
myperror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (errno != 0) {
		fprintf(stderr, "%s:", PROGNAME);
		if (fmt) {
			vfprintf(stderr, fmt, ap);
			fprintf(stderr, ":");
		}
		fprintf(stderr, " %s\n", strerror(errno));
	}
	va_end(ap);
}


static void
print_caption(const char *name)
{
	const char *z = each_plane ? "Z" : "";

	printf("# Filename: %s\n", name);
	printf("# %3s %-8s%3s %11s %11s %11s %11s %10s\n",
		   "No.", "ITEM", z, "AVE", "SD", "MIN", "MAX", "NUM");
}


/*
 *  sumup statistical data in z-planes.
 */
static void
sumup_stat(struct statics *stat, const struct statics sz[], int len)
{
	int i;

	stat->min = HUGE_VAL;
	stat->max = -HUGE_VAL;
	for (i = 0; i < len; i++) {
		stat->count += sz[i].count;
		stat->avr   += sz[i].count * sz[i].avr;
		stat->min   =  min(stat->min, sz[i].min);
		stat->max   =  max(stat->max, sz[i].max);
	}

	if (stat->count > 0) {
		double var = 0.;
		double adiff;

		stat->avr /= stat->count;
		for (i = 0; i < len; i++) {
			adiff = stat->avr - sz[i].avr;
			var += sz[i].count * (sz[i].sd * sz[i].sd + adiff * adiff);
		}
		var /= stat->count;
		stat->sd = sqrt(var);
	}
}


static void
ngtstat_plane(struct statics *stat, const GT3_Varbuf *varbuf, void *work)
{
	double avr = 0., sd = 0.;
	int len;
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

	avr_func(&avr, work, len);
	sd_func(&sd, work, avr, len);

	stat->count = len;
	stat->avr   = avr;
	stat->sd    = sd;
	stat->min   = min_func(work, len);
	stat->max   = max_func(work, len);
}


int
ngtstat_var(GT3_Varbuf *varbuf)
{
	static void *work = NULL;
	static size_t worksize = 0;
	static struct statics *stat = NULL;
	static int max_num_plane = 0;
	char prefix[32], item[32];
	int i, znum;


	/*
	 *  Trial read to fill varbuf.
	 *  Required before GT3_getVarAttrStr().
	 */
	if (GT3_readVarZ(varbuf, zrange[0]) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	/*
	 *  (re)allocate work buffer.
	 */
	if (varbuf->bufsize > worksize) {
		free(work);
		if ((work = malloc(varbuf->bufsize)) == NULL) {
			myperror(NULL);
			return -1;
		}
		worksize = varbuf->bufsize;
	}

	znum = min(zrange[1], varbuf->dimlen[2]) - zrange[0];

	if (znum > max_num_plane) {
		free(stat);
		if ((stat = (struct statics *)
			 malloc(sizeof(struct statics) * znum)) == NULL) {
			myperror(NULL);
			return -1;
		}
		max_num_plane = znum;
	}

	GT3_getVarAttrStr(item, sizeof item, varbuf, "ITEM");


	snprintf(prefix, sizeof prefix, "%5d %-8s",
			 varbuf->fp->curr + 1, item);

	for (i = 0; i < znum; i++) {
		if (GT3_readVarZ(varbuf, zrange[0] + i) < 0) {
			GT3_printErrorMessages(stderr);
			return -1;
		}
		ngtstat_plane(stat + i, varbuf, work);

		if (each_plane) {
			printf("%14s%3d %11.5g %11.5g %11.5g %11.5g %10d\n",
				   prefix,
				   1 + zrange[0] + i,
				   stat[i].avr,
				   stat[i].sd,
				   stat[i].min,
				   stat[i].max,
				   stat[i].count);
			/* prefix[0] = '\0'; */
		}
	}

	if (!each_plane) {
		struct statics stat_all;

		memset(&stat_all, 0, sizeof(struct statics));
		sumup_stat(&stat_all, stat, znum);
		printf("%14s    %11.5g %11.5g %11.5g %11.5g %10d\n",
			   prefix,
			   stat_all.avr,
			   stat_all.sd,
			   stat_all.min,
			   stat_all.max,
			   stat_all.count);
	}
	return 0;
}


int
ngtstat(const char *path, struct sequence *seq)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int stat, rval = 0;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if ((var = GT3_getVarbuf(fp)) == NULL) {
		GT3_close(fp);
		GT3_printErrorMessages(stderr);
		return -1;
	}

	print_caption(path);
	if (seq == NULL)
		while (!GT3_eof(fp)) {
			if (ngtstat_var(var) < 0)
				rval = -1;

			if (GT3_next(fp) < 0) {
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

			if (ngtstat_var(var) < 0)
				rval = -1;
		}

	GT3_freeVarbuf(var);
	GT3_close(fp);

	return rval;
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


static int
set_range(int range[], const char *str)
{
	if (get_ints(range, 2, str, ':') < 0)
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
	if (!strchr(str, ':'))
		range[1] = range[0] + 1;
	return 0;
}


int
main(int argc, char **argv)
{
	struct sequence *seq = NULL;
	int rval = 0;
	int ch;

	GT3_setProgname(PROGNAME);

	while ((ch = getopt(argc, argv, "hat:x:y:z:")) != -1)
		switch (ch) {
		case 'a':
			each_plane = 0;
			break;

		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;

		case 'x':
			slicing = 1;
			if (set_range(xrange, optarg) < 0) {
				fprintf(stderr, "%s: invalid argument of -x : %s",
						PROGNAME, optarg);
				exit(1);
			}
			break;

		case 'y':
			slicing = 1;
			if (set_range(yrange, optarg) < 0) {
				fprintf(stderr, "%s: invalid argument of -y : %s",
						PROGNAME, optarg);
				exit(1);
			}
			break;

		case 'z':
			if (set_range(zrange, optarg) < 0) {
				fprintf(stderr, "%s: invalid argument of -z : %s",
						PROGNAME, optarg);
				exit(1);
			}
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

		if (ngtstat(*argv, seq) < 0)
			rval = 1;
	}
	return rval;
}
