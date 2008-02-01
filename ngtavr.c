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
#include "logging.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtavr"

/*
 * globals
 */
static void    *g_vardata;
static double  *g_timedur;

static size_t g_vdatasize;		/* in bytes */
static size_t g_dimlen[3];		/* dimension length */
static int    g_datatype;		/* GT3_TYPE_FLOAT or GT3_TYPE_DOUBLE */
static GT3_HEADER g_head;		/* gt3-header of the 1st chunk */
static GT3_HEADER g_head_last;	/* gt3-header of the last chunk */
static double g_totaltdur;

static int g_snapshot_done;

static const char *default_ofile = "gtool.out";

static int g_zrange[] = { 0, 0x7ffffff };
static int g_zsliced = 0;





int
init_global(GT3_Varbuf *var)
{
	size_t len, zlen, elem;
	void *tvar;
	double *ttime;
	size_t i;

	if (GT3_readHeader(&g_head, var->fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if (g_zsliced) {
		zlen = g_zrange[1] - g_zrange[0];

		if (zlen > var->dimlen[2])
			g_zrange[1] = var->dimlen[2];
	} else
		zlen = var->dimlen[2];


	elem = var->type == GT3_TYPE_DOUBLE ? 8 : 4;

	len = var->dimlen[0] * var->dimlen[1] * zlen;

	if ((tvar = malloc(elem * len)) == NULL
		|| (ttime = (double *)malloc(sizeof(double) * len)) == NULL) {

		logging(LOG_SYSERR, "for variable data.");
		return -1;
	}

	memset(tvar, 0, elem * len);
	for (i = 0; i < len; i++)
		ttime[i] = 0.;

	g_vardata = tvar;
	g_timedur = ttime;
	g_vdatasize = elem * len;
	g_dimlen[0] = var->dimlen[0];
	g_dimlen[1] = var->dimlen[1];
	g_dimlen[2] = zlen;
	g_datatype  = var->type;
	g_totaltdur = 0.;



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
get_zrange(int *z0, int *z1, int zend)
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
	int temp;
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
		logging(LOG_WARN, "Processing snapshot data...");
		g_snapshot_done = 1;
	}
	if (temp < 0)
		logging(LOG_WARN, "Negative time-duration: %d", temp);

	return dt;
}


int
ngtavr_file(GT3_File *fp)
{
	GT3_Varbuf *var;
	int rval = 0;
	double dt;
	int z0, z1;

	if ((var = GT3_getVarbuf(fp)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if (g_vardata == NULL && init_global(var) < 0) {
		GT3_freeVarbuf(var);
		return -1;
	}

	for (;;) {
		if (GT3_eof(fp))
			break;

		if (GT3_readHeader(&g_head_last, fp) < 0) {
			GT3_printErrorMessages(stderr);
			rval = -1;
			break;
		}

		if (check_input(&g_head_last) < 0) {
			rval = -1;
			break;
		}

		dt = get_tstepsize(&g_head_last);
		get_zrange(&z0, &z1, fp->dimlen[2]);

		if (timeintegral_f(g_vardata, g_timedur, &g_totaltdur, var,
						   dt, z0, z1) < 0) {
			rval = -1;
			break;
		}

		/* logging(LOG_INFO, "integral %d", counter); */

		if (GT3_next(fp) < 0) {
			GT3_printErrorMessages(stderr);
			rval = -1;
			break;
		}
	}

	GT3_freeVarbuf(var);
	return rval;
}



int
ngtavr(const char *path)
{
	GT3_File *fp;
	int rval;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	rval = ngtavr_file(fp);
	GT3_close(fp);
	return rval;
}


int
write_average(const char *path)
{
	FILE *fp;
	int rval;
	GT3_Date date2;


	if ((fp = fopen(path, "wb")) == NULL) {
		logging(LOG_SYSERR, "");
		return -1;
	}

	GT3_decodeHeaderDate(&date2, &g_head_last, "DATE2");

	/*
	 *  use the first GT3_HEADER(g_head) in data series.
	 */
	GT3_setHeaderInt(&g_head, "TDUR", g_totaltdur);
	GT3_setHeaderDate(&g_head, "DATE2", &date2);

	if (g_zsliced) {
		GT3_setHeaderInt(&g_head, "ASTR1", g_zrange[0] + 1);
		GT3_setHeaderInt(&g_head, "AEND1", g_zrange[1]    );
	}

	rval = GT3_write(g_vardata, g_datatype,
					 g_dimlen[0], g_dimlen[1], g_dimlen[2],
					 &g_head, "UR4", fp);
	fclose(fp);

	if (rval < 0)
		GT3_printErrorMessages(stdout);

	return rval;
}


void
average()
{
	size_t i, len;
	float *data;
	double mintdur;
	float miss = -999.0;

	len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];

	/*
	 *  mintdur: the minimum time-duration where the average
	 *  has the meanings.
	 */
	mintdur = 0.5 * g_totaltdur;

	data = (float *)g_vardata;
#pragma parallel for
	for (i = 0; i < len; i++) {
		if (g_timedur[i] < mintdur || g_timedur[i] == 0.)
			data[i] = miss;
		else
			data[i] /= g_timedur[i];
	}
}


int
main(int argc, char **argv)
{
	int ch, rval = 0;
	const char *ofile = NULL;


	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);

	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch (ch) {
		case 'o':
			ofile = optarg;
			break;
		default:
			break;
		}


	argc -= optind;
	argv += optind;

	for (;argc > 0 && *argv; argc--, argv++) {
		if (ngtavr(*argv) < 0) {
			logging(LOG_ERR, "failed to process %s.", *argv);
			exit(1);
		}
	}

	if (g_vardata) {
		average();

		if (!ofile)
			ofile = default_ofile;

		if (write_average(ofile) < 0) {
			logging(LOG_ERR, "%s", ofile);
			rval = 1;
		}
	}
	return rval;
}
