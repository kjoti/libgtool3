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
int
timeintegral_f(float *vsum, double *tsum, double *dtsum,
			   GT3_Varbuf *var, double dt, int z0, int z1)
{
	float miss = var->miss;
	int len = var->dimlen[0] * var->dimlen[1];
	int rval = 0;
	float *data;
	int i, z;


	logging(LOG_NOTICE, "file=%s, No=%d, z=%d..%d",
			var->fp->path, var->fp->curr + 1, z0 + 1, z1);


	assert(z1 < z0 || (z1 - z0) * len * sizeof(float) <= g_vdatasize);

	for (z = z0; z < z1; z++) {
		if (GT3_readVarZ(var, z) < 0) {
			GT3_printErrorMessages(stderr);
			rval = -1;
			break;
		}

		data = (float *)var->data;
#pragma parallel for
		for (i = 0; i < len; i++)
			if (data[i] != miss) {
				vsum[i] += data[i] * dt;
				tsum[i] += dt;
			}

		vsum += len;
		tsum += len;
	}
	*dtsum += dt;

	return rval;
}


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
	GT3_Date date;
	GT3_HEADER head;

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

	if (g_zsliced) {
		GT3_setHeaderInt(&head, "ASTR1", g_zrange[0] + 1);
		GT3_setHeaderInt(&head, "AEND1", g_zrange[1]    );
	}

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
	float *data;
	double tdur_lowlim;
	float miss = -999.0;

	len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];

	/*
	 *  The lower limit of time-duration where the average
	 *  has the meanings.
	 */
	tdur_lowlim = 0.5 * g_totaltdur;

	data = (float *)g_vardata;
#pragma parallel for
	for (i = 0; i < len; i++) {
		if (g_timedur[i] < tdur_lowlim || g_timedur[i] == 0.)
			data[i] = miss;
		else
			data[i] /= g_timedur[i];
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
	GT3_Date curr;


	if (GT3_readHeader(&head, var->fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if (g_counter > 0) {
		GT3_decodeHeaderDate(&curr, &head, "DATE1");

		if (GT3_cmpDate2(&g_date2, &curr) != 0)
			logging(LOG_WARN, "Input date is not contiguous in time");
	}


	if (check_input(&head) < 0) {

	}

	if (g_counter == 0)
		/* update DATE1 (start of integration)  */
		if (GT3_decodeHeaderDate(&g_date1, &head, "DATE1")  < 0) {
			logging(LOG_ERR, "DATE1 is missing");
			logging(LOG_NOTICE, "It can be set by ngtick command");
			return -1;
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

	return timeintegral_f(g_vardata, g_timedur,
						  &g_totaltdur, var,
						  dt, z0, z1);
}


/*
 *  ngtavr_seq() averages specifed chunks.
 */
int
ngtavr_seq(const char *path)
{
	static int first_of_all = 1;
	static GT3_Varbuf *var = NULL;
	GT3_File *fp;
	int rval = 0;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

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

	for (;;) {
		if (GT3_eof(fp))
			break;

		if (integrate_chunk(var) < 0) {
			logging(LOG_ERR, "failed at No.%d in %s.",  fp->curr + 1, path);
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


	for (;;) {
		if (GT3_eof(fp))
			break;

		if (integrate_chunk(var) < 0) {
			logging(LOG_ERR, "failed at No.%d in %s.",  fp->curr + 1, path);
			rval = -1;
			break;
		}

		diff = cmpDateIterator(&it, &g_date2);
		if (diff > 0) {
			logging(LOG_WARN, "Time-duration in the input data is greater"
					" than specified mean-duration");
		}

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


int
setStepsize(GT3_Date *step, const char *str)
{
	GT3_setDate(step, 1, 0, 0, 0, 0, 0);
	return 0;
}


void
usage(void)
{
}


int
main(int argc, char **argv)
{
	int ch, rval = 0;
	const char *ofile = NULL;
	int each_timestep_mode = 0;
	GT3_Date step;
	FILE *ofp;
	char *mode = "wb";

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);

	while ((ch = getopt(argc, argv, "am:o:")) != -1)
		switch (ch) {
		case 'a':
			mode = "ab";
			break;
		case 'm':
			setStepsize(&step, optarg);
			each_timestep_mode = 1;
			break;
		case 'o':
			ofile = optarg;
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
			if (ngtavr_seq(*argv) < 0) {
				logging(LOG_ERR, "failed to process %s.", *argv);
				exit(1);
			}
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
