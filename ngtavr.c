/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtavr.c -- average by time
 *
 */
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "dateiter.h"
#include "logging.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtavr"

/*
 *  globals data
 */
static void    *g_vardata;
static double  *g_timedur;

static size_t g_vdatasize;		/* in bytes */
static size_t g_dimlen[3];		/* dimension length */
static int    g_datatype;		/* GT3_TYPE_FLOAT or GT3_TYPE_DOUBLE */
static GT3_HEADER g_head;		/* gt3-header of the 1st chunk */
static GT3_Date g_date1;
static GT3_Date g_date2;

static double g_miss_value;
static int g_timeunit;

static int g_counter;			/* # of chunks integrated */
static double g_totaltdur;

static int g_snapshot_done;

/*
 *  global options
 */
static const char *default_ofile = "gtool.out";
static int g_zrange[] = { 0, 0x7ffffff };
static int g_zsliced = 0;
static int calendar_type = GT3_CAL_GREGORIAN;
static int ignore_tdur = 0;


void
clear_global()
{
	int i, len;

	memset(g_vardata, 0, g_vdatasize);
	len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];
	for (i = 0; i < len; i++)
		g_timedur[i] = 0.;

	g_counter = 0;
	g_totaltdur = 0.;
}


int
init_global(GT3_Varbuf *var)
{
	size_t len, elem;
	int zrange[2];
	int zlen;
	void *tvar;
	double *ttime;

	if (g_zsliced) {
		zrange[0] = g_zrange[0];
		zrange[1] = g_zrange[1];

		if (zrange[1] > var->dimlen[2])
			zrange[1] = var->dimlen[2];

		zlen = zrange[1] - zrange[0];
		if (zlen < 0) {
			logging(LOG_ERR, "invalid z-range");
			return -1;
		}
	} else
		zlen = var->dimlen[2];


	elem = var->type == GT3_TYPE_DOUBLE ? 8 : 4;

	len = var->dimlen[0] * var->dimlen[1] * zlen;

	if ((tvar = malloc(elem * len)) == NULL
		|| (ttime = (double *)malloc(sizeof(double) * len)) == NULL) {

		logging(LOG_SYSERR, "allocation failed.");
		return -1;
	}

	g_vardata = tvar;
	g_timedur = ttime;
	g_vdatasize = elem * len;
	g_dimlen[0] = var->dimlen[0];
	g_dimlen[1] = var->dimlen[1];
	g_dimlen[2] = zlen;
	g_datatype  = var->type;

	g_miss_value = -999.0;		/* by default */
	GT3_decodeHeaderDouble(&g_miss_value, &g_head, "MISS");

	g_timeunit = GT3_decodeHeaderTunit(&g_head);
	if (g_timeunit < 0) {
		GT3_printErrorMessages(stderr);

		logging(LOG_NOTICE, "overwrite UTIM=HOUR");
		GT3_setHeaderString(&g_head, "UTIM", "HOUR");
		g_timeunit = GT3_UNIT_HOUR;
	}
	clear_global();
	return 0;
}


/*
 *  time-integral
 *
 *  'vsum':  an integrated value for each grid point.
 *  'tsum':  sum of delta-t for each grid point.
 *  'dtdum':
 */
#define TIME_INTEGRAL(TYPE, NAME)                                       \
int                                                                     \
NAME(TYPE *vsum, double *tsum, double *dtsum,                           \
     GT3_Varbuf *var, double dt, int z0, int z1)                        \
{                                                                       \
    TYPE miss = var->miss;                                              \
    int len = var->dimlen[0] * var->dimlen[1];                          \
    int rval = 0;                                                       \
    TYPE *data;                                                         \
    int i, z;                                                           \
                                                                        \
    assert(z1 < z0 || (z1 - z0) * len * sizeof(TYPE) <= g_vdatasize);   \
                                                                        \
    for (z = z0; z < z1; z++) {                                         \
        if (GT3_readVarZ(var, z) < 0) {                                 \
            GT3_printErrorMessages(stderr);                             \
            rval = -1;                                                  \
            break;                                                      \
        }                                                               \
                                                                        \
        data = (TYPE *)var->data;                                       \
        for (i = 0; i < len; i++)                                       \
            if (data[i] != miss) {                                      \
                vsum[i] += data[i] * dt;                                \
                tsum[i] += dt;                                          \
            }                                                           \
                                                                        \
        vsum += len;                                                    \
        tsum += len;                                                    \
    }                                                                   \
    *dtsum += dt;                                                       \
                                                                        \
    return rval;                                                        \
}


/*
 *  instances of time_integral.
 */
TIME_INTEGRAL(float,  time_integral_f)
TIME_INTEGRAL(double, time_integral_d)


int
check_input(const GT3_HEADER *head)
{
	return 0;
}


void
set_zrange(int *z0, int *z1, int zend)
{
	int zstr = 0;

	if (g_zsliced) {
		zstr = g_zrange[0];
		if (g_zrange[1] < zend)
			zend = g_zrange[1];
	} else
		if (zend > g_dimlen[2])
			zend = g_dimlen[2];

	*z0 = zstr;
	*z1 = zend;
}



double
get_tstepsize(const GT3_HEADER *head)
{
	int temp = 0;
	double dt;

	/*
	 *  always use HOUR in time-integration.
	 */
	GT3_decodeHeaderInt(&temp, head, "TDUR");
	if (temp > 0) {
		dt = (double)temp;
	} else
		dt = 1.;

	if (!g_snapshot_done && temp == 0) {
		logging(LOG_NOTICE, "Processing snapshot data...");
		g_snapshot_done = 1;
	}
	if (temp < 0)
		logging(LOG_WARN, "Negative time-duration: %d", temp);

	return dt;
}


int
write_average(FILE *fp)
{
	int rval;
	GT3_Date date, origin;
	GT3_HEADER head;
	double time;

	GT3_copyHeader(&head, &g_head);

	/*
	 *  set DATE1, DATE2 and TDUR.
	 */
	GT3_setHeaderDate(&head, "DATE1", &g_date1);
	GT3_setHeaderDate(&head, "DATE2", &g_date2);
	GT3_setHeaderInt(&head, "TDUR", g_totaltdur);

	/*
	 *  DATE&TIME mid-date
	 */
	GT3_midDate(&date, &g_date1, &g_date2, calendar_type);
	GT3_setHeaderDate(&head, "DATE", &date);

	GT3_setDate(&origin, 0, 1, 1, 0, 0, 0);
	time = GT3_getTime(&date, &origin, g_timeunit, calendar_type);
	GT3_setHeaderInt(&head, "TIME", (int)time);

	if (time != (int)time)
		logging(LOG_NOTICE, "TIME(=%.2f) is truncated to %d",
				time, (int)time);

	if (g_zsliced) {
		GT3_setHeaderInt(&head, "ASTR1", g_zrange[0] + 1);
		GT3_setHeaderInt(&head, "AEND1", g_zrange[1]    );
	}

	logging(LOG_INFO,
			"Output (%4d-%02d-%02d %02d:%02d:%02d ==>"
			" %4d-%02d-%02d %02d:%02d:%02d)",
			g_date1.year, g_date1.mon, g_date1.day,
			g_date1.hour, g_date1.min, g_date1.sec,
			g_date2.year, g_date2.mon, g_date2.day,
			g_date2.hour, g_date2.min, g_date2.sec);

	rval = GT3_write(g_vardata, g_datatype,
					 g_dimlen[0], g_dimlen[1], g_dimlen[2],
					 &head, "UR4", fp);
	if (rval < 0)
		GT3_printErrorMessages(stdout);

	return rval;
}


void
average()
{
	size_t i, len;
	double tdur_lowlim;

	len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];

	/*
	 *  The lower limit of time-duration where the average
	 *  has the meanings.
	 */
	tdur_lowlim = 0.5 * g_totaltdur;

	if (g_datatype == GT3_TYPE_FLOAT) {
		float *data;
		float miss = g_miss_value;

		data = (float *)g_vardata;
		for (i = 0; i < len; i++) {
			if (g_timedur[i] < tdur_lowlim || g_timedur[i] == 0.)
				data[i] = miss;
			else
				data[i] /= g_timedur[i];
		}
	} else {
		double *data;
		double miss = g_miss_value;

		data = (double *)g_vardata;
		for (i = 0; i < len; i++) {
			if (g_timedur[i] < tdur_lowlim || g_timedur[i] == 0.)
				data[i] = miss;
			else
				data[i] /= g_timedur[i];
		}
	}
}


/*
 *  integrate_chunk() integrates a current chunk.
 *
 *  This function updates some global variable.
 */
int
integrate_chunk(GT3_Varbuf *var)
{
	double dt = 1.;
	int z0, z1;
	GT3_HEADER head;

	if (GT3_readHeader(&head, var->fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

#if 0
	if (g_counter > 0) {
		GT3_Date curr;

		GT3_decodeHeaderDate(&curr, &head, "DATE1");

		if (GT3_cmpDate2(&g_date2, &curr) != 0)
			logging(LOG_WARN, "Input date is not contiguous in time");
	}
#endif


	if (check_input(&head) < 0) {

	}

	if (g_counter == 0) {
		/* update DATE1 (start of integration)  */
		if (GT3_decodeHeaderDate(&g_date1, &head, "DATE1")  < 0) {
			logging(LOG_ERR, "DATE1 is missing");
			logging(LOG_NOTICE, "It can be set by ngtick command");
			return -1;
		}
	}

	/* update DATE2 (last date) */
	if (GT3_decodeHeaderDate(&g_date2, &head, "DATE2")  < 0) {
		logging(LOG_ERR, "DATE2 is missing");
		logging(LOG_NOTICE, "It can be set by ngtick command");
		return -1;
	}
	g_counter++;

	if (!ignore_tdur)
		dt = get_tstepsize(&head);

	set_zrange(&z0, &z1, var->fp->dimlen[2]);

	return var->type == GT3_TYPE_FLOAT
		? time_integral_f(g_vardata, g_timedur,
						  &g_totaltdur, var,
						  dt, z0, z1)
		: time_integral_d(g_vardata, g_timedur,
						  &g_totaltdur, var,
						  dt, z0, z1);
}


/*
 *  ngtavr_seq() averages specifed chunks.
 */
int
ngtavr_seq(const char *path, struct sequence *seq)
{
	static int first_of_all = 1;
	static GT3_Varbuf *var = NULL;
	GT3_File *fp;
	int rval = 0;
	int stat;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	logging(LOG_INFO, "Read %s", path);

	if (first_of_all) {
		first_of_all = 0;

		calendar_type = GT3_guessCalendarFile(path);

		if (GT3_readHeader(&g_head, fp) < 0
			|| (var = GT3_getVarbuf(fp)) == NULL) {
			GT3_printErrorMessages(stderr);
			GT3_close(fp);
			return -1;
		}
		init_global(var);
	} else {
		/*
		 *  XXX
		 *  Replace file-pointer in Varbuf and invalidate variable cache.
		 */
		GT3_reattachVarbuf(var, fp);
	}

	if (!seq) {
		while (!GT3_eof(fp)) {
			if (integrate_chunk(var) < 0) {
				logging(LOG_ERR, "failed at No.%d in %s.",
						fp->curr + 1, path);
				rval = -1;
				break;
			}
			if (GT3_next(fp) < 0) {
				GT3_printErrorMessages(stderr);
				rval = -1;
				break;
			}
		}
	} else {
		while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
			if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
				rval = -1;
				break;
			}
			if (stat == ITER_OUTRANGE)
				continue;

			if (integrate_chunk(var) < 0) {
				logging(LOG_ERR, "failed at No.%d in %s.",
						fp->curr + 1, path);
				rval = -1;
				break;
			}
		}
	}
	GT3_close(fp);
	return rval;
}



/*
 *  ngtavr_eachstep() averages data for each time-duration.
 */
int
ngtavr_eachstep(const char *path, const GT3_Date *step, FILE *output)
{
	static int first_of_all = 1;
	static GT3_Varbuf *var = NULL;
	static DateIterator it;
	GT3_Date date;
	GT3_File *fp;
	int rval = 0;
	int diff;


	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	logging(LOG_INFO, "Read %s", path);

	if (first_of_all) {
		first_of_all = 0;

		calendar_type = GT3_guessCalendarFile(path);

		if (GT3_readHeader(&g_head, fp) < 0
			|| (var = GT3_getVarbuf(fp)) == NULL) {
			GT3_printErrorMessages(stderr);
			GT3_close(fp);
			return -1;
		}

		if (GT3_decodeHeaderDate(&date, &g_head, "DATE1") < 0) {
			logging(LOG_ERR, "DATE1 is missing");
			GT3_close(fp);
			return -1;
		}

		setDateIterator(&it, &date, step, calendar_type);
		init_global(var);
	} else {
		/*
		 *  XXX
		 *  Replace file-pointer in Varbuf and invalidate variable cache.
		 */
		GT3_reattachVarbuf(var, fp);
	}

	while (!GT3_eof(fp)) {
		if (integrate_chunk(var) < 0) {
			logging(LOG_ERR, "failed at No.%d in %s.",  fp->curr + 1, path);
			rval = -1;
			break;
		}

		diff = cmpDateIterator(&it, &g_date2);
		if (diff > 0)
			logging(LOG_WARN, "Too large time-duration in the input data");

		if (diff >= 0) {
			average();
			write_average(output);
			clear_global();

			nextDateIterator(&it);
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


/*
 *  parse an argument of the -m option.
 */
int
setStepsize(GT3_Date *step, const char *str)
{
	int d[] = { 0, 0, 0, 0, 0, 0 };
	struct { const char *key; int val; } tab[] = {
		{ "yr",   0   },
		{ "mo",   1   },
		{ "dy",   2   },
		{ "hr",   3   },
		{ "mn",   4   },
		{ "s",    5   },

		{ "year", 0   },
		{ "mon",  1   },
		{ "day",  2   },
		{ "hour", 3   },
		{ "min",  4   },
		{ "sec",  5   }
	};
	char *endptr;
	int i, num;

	num = strtol(str, &endptr, 10);
	if (str == endptr || *endptr == '\0')
		return -1;

	for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
		if (strcmp(endptr, tab[i].key) == 0)
			break;

	if (i == sizeof tab / sizeof tab[0])
		return -1;

	d[tab[i].val] = num;
	GT3_setDate(step, d[0], d[1], d[2], d[3], d[4], d[5]);
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
	int ch, rval = 0;
	const char *ofile = NULL;
	int each_timestep_mode = 0;
	GT3_Date step;
	FILE *ofp;
	char *mode = "wb";

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);

	while ((ch = getopt(argc, argv, "am:o:t:v")) != -1)
		switch (ch) {
		case 'a':
			mode = "ab";
			break;

		case 'm':
			if (setStepsize(&step, optarg) < 0) {
				logging(LOG_ERR,
						"%s: invalid argument for -m option",
						optarg);
				exit(1);
			}
			each_timestep_mode = 1;
			break;

		case 'o':
			ofile = optarg;
			break;

		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;

		case 'v':
			set_logging_level("verbose");
			break;
		default:
			break;
		}


	if (!ofile)
		ofile = default_ofile;

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		logging(LOG_NOTICE, "No input data");
		usage();
		exit(0);
	}


	if ((ofp = fopen(ofile, mode)) == NULL) {
		logging(LOG_SYSERR, ofile);
		exit(1);
	}

	if (each_timestep_mode) {
		for (;argc > 0 && *argv; argc--, argv++)
			if (ngtavr_eachstep(*argv, &step, ofp) < 0) {
				logging(LOG_ERR, "failed to process %s.", *argv);
				exit(1);
			}
	} else {
		for (;argc > 0 && *argv; argc--, argv++) {
			if (ngtavr_seq(*argv, seq) < 0) {
				logging(LOG_ERR, "failed to process %s.", *argv);
				exit(1);
			}
			reinitSeq(seq, 1, 0x7fffffff);
		}

		average();
		if (write_average(ofp) < 0) {
			logging(LOG_ERR, ofile);
			rval = 1;
		}
	}

	fclose(ofp);

	return rval;
}
