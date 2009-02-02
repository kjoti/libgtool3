/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  gtdim.c -- gtool3 axes
 *
 *  $Date: 2006/12/04 06:55:36 $
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "talloc.h"
#include "debug.h"

/*
 *  'GTAX_PATH' is introduced.
 *
 *  It is a set of directories where GTAXLOC.* files are searched.
 *  Directories are separated with ':'.
 */
#define PATH_SEP ':'

/*
 *  default axis dir, e.g., '/usr/local/share/gtool3/gt3'
 */
static const char *default_dir = DEFAULT_GTAXDIR;

#define INVERT_FLAG		1U
#define MID_FLAG		2U
#define C_FLAG			4U

#ifndef M_2_PI
#  define M_2_PI	0.63661977236758134308	/* 2/pi */
#endif


static int
parse_axisname(const char *name, char *base,
			   int *len, int *idiv, unsigned *flag)
{
	const char *p = name;
	int rval = 0;
	char *endptr;
	int cnt;

	while (*p == ' ') /* skip spaces. */
		p++;

	/*
	 *  copy basename (at most 16 letters).
	 */
	cnt = 0;
	while (*p != '\0' && !isdigit(*p) && cnt++ < 16)
		*base++ = *p++;
	*base = '\0';

	/*
	 *  get size.
	 */
	if (isdigit(*p)) {
		*len = (int)strtol(p, &endptr, 10);
		p = endptr;
	} else if (cnt == 0)
		*len = 1;
	else
		*len = 0;

	if (*len < 1)
		rval = -1;

	/*
	 *  optional suffix
	 */
	*flag = 0;
	*idiv = 1;
	for (;;) {
		if (*p == '\0')
			break;

		if (*p == 'x' && isdigit(*(p + 1))) {
			p++;
			*idiv = strtol(p, &endptr, 10);
			p = endptr;
		} else if (*p == 'I') {
			*flag |= INVERT_FLAG;
			p++;
		} else if (*p == 'M') {
			*flag |= MID_FLAG;
			p++;
		} else if (*p == 'C') {
			*flag |= C_FLAG;
			p++;
		} else {
			rval = -1;
			p++;
		}
	}
	return rval;
}


static void
invert(double *grid, int len)
{
	int i;
	double temp;

	for (i = 0; i < len / 2; i++) {
		temp = grid[i];

		grid[i] = grid[len - 1 - i];
		grid[len - 1 - i] = temp;
	}
}


static int
latitude_mosaic(double *grid, const double *wght, int len, int idiv)
{
	double bnd_[1024];
	double *bnd;
	double rdiv, coef;
	int i, m;

	if ((bnd = (double *)tiny_alloc(
			 bnd_, sizeof bnd_, sizeof(double) * (len + 1))) == NULL) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	/*
	 *  cell boundaries in mu-grids [-1, 1].
	 *  mu = cos(theta), where theta is the zenith angle [0, pi]
	 *  (0: north pole, pi: south pole).
	 *
	 *  cf. ${AGCM}/src/physics/pmisc.F (MKLATM1)
	 */
	bnd[0]   = -1.; /* south pole */
	bnd[len] =  1.; /* north pole */
	for (i = 1; i < len / 2; i++) {
		bnd[i] = bnd[i-1] + wght[i-1];
		bnd[len-i] = -bnd[i];
	}
	if (len % 2 == 0)
		bnd[len / 2] = .0;

	/*
	 *  mu -> theta -> latitude(in radian) -> latitude(in degree)
	 */
	for (i = 0; i < len + 1; i++)
		bnd[i] = 90. * (1. - acos(bnd[i]) * M_2_PI);

	/*
	 *  interpolation
	 *  cf. ${AGCM}/src/physics/pmisc.F (SETLOM)
	 */
	rdiv = 1. / (2. * idiv);
	for (m = 0; m < idiv; m++) {
		coef = (2. * m + 1.) * rdiv;
		for (i = 0; i < len; i++)
			grid[i*idiv + m] = (1. - coef) * bnd[i] + coef * bnd[i+1];
	}
	tiny_free(bnd, bnd_);
	return 0;
}


static GT3_Dim *
alloc_newdim(void)
{
	GT3_Dim *dim;

	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL)
		return NULL;

	dim->name = NULL;
	dim->values = NULL;
	dim->len = dim->cyclic = 0;
	dim->range[0] = dim->range[1] = -999.0;
	dim->title = dim->unit = NULL;
	return dim;
}


static GT3_Dim *
make_glon(int len, int idiv, unsigned flag)
{
	GT3_Dim *dim = NULL;
	double *grid, bnd0, bnd1;
	int mlen;

	/*
	 *  XXX
	 *  Cyclic axes need additional one more grid point,
	 *  which is identical to the first grid (grid[0]).
	 */
	mlen = len * idiv + 1;

	if ((dim = alloc_newdim()) == NULL
		|| (grid = (double *)malloc(sizeof(double) * mlen)) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}

	if (flag & C_FLAG) {
		bnd0 = -180.;
		bnd1 =  180.;
	} else {
		bnd0 = 0.;
		bnd1 = 360.;
	}

	uniform_bnd(grid, bnd0, bnd1, mlen);
	if (idiv > 1) {
		int i;
		double offset;

		offset = (1. - 1. / idiv) * 180. / len;
		for (i = 0; i < mlen; i++)
			grid[i] -= offset;
	}

	if (flag & MID_FLAG) {
		int i;
		double delta;

		delta = 180. / (len * idiv);
		for (i = 0; i < mlen; i++)
			grid[i] += delta;
	}

	dim->values   = grid;
	dim->len      = mlen;
	dim->range[0] = bnd0;
	dim->range[1] = bnd1;
	dim->cyclic   = 1;
	dim->title    = strdup("longitude");
	dim->unit     = strdup("degrees_east"); /* UDUNITS supports */

	return dim;
}


static GT3_Dim *
make_glat(int len, int idiv, unsigned flag)
{
	double *grid;
	GT3_Dim *dim;

	if (idiv > 1)
		return NULL;

	if ((dim = alloc_newdim()) == NULL
		|| (grid = (double *)malloc(sizeof(double) * len)) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}

	if ((flag & MID_FLAG) == 0 && len % 2 == 1 && len > 2) {
		uniform_bnd(grid, 90., -90., len);
	} else {
		uniform_center(grid, 90., -90., len);
	}

	if (flag & INVERT_FLAG)
		invert(grid, len);

	dim->values   = grid;
	dim->len      = len;
	dim->range[0] = -90;
	dim->range[1] = 90.;
	dim->cyclic   = 0;
	dim->title    = strdup("latitude");
	dim->unit     = strdup("degrees_north"); /* UDUNITS supports */

	return dim;
}


static GT3_Dim *
make_ggla(int len, int idiv, unsigned flag)
{
	double wght_[1024];
	double *grid = NULL, *wght = NULL;
	GT3_Dim *dim = NULL;
	int errflag = 1;
	int i, mlen;

	/* MID_FLAG is unavailable for GGLA */
	if (flag & MID_FLAG)
		return NULL;

	mlen = len * idiv;
	if ((dim = alloc_newdim()) == NULL
		|| (grid = (double *)malloc(sizeof(double) * mlen)) == NULL
		|| (wght = (double *)tiny_alloc(
				wght_, sizeof wght_, sizeof(double) * len)) == NULL) {
		gt3_error(SYSERR, NULL);
		goto final;
	}

	/*
	 *  get Gauss-Legendre
	 */
	gauss_legendre(grid, wght, len);

	if (idiv > 1) {
		if (latitude_mosaic(grid, wght, len, idiv) < 0) {
			goto final;
		}
	} else
		/*
		 *  convert from "mu = cos(theta)" to latitude (in degree).
		 *
		 *  -1.0 => -90.0, 1.0 => +90.0
		 */
		for (i = 0; i < len; i++)
			grid[i] = 90. * (1. - acos(grid[i]) * M_2_PI);

	assert(grid[0]     > -90. && grid[0]     < 90.);
	assert(grid[len-1] > -90. && grid[len-1] < 90.);

	/*
	 *  The "GGLA" is directed from north to south.
	 *  It is opposite to the mu [-1, 1].
	 */
	if ((flag & INVERT_FLAG) == 0)
		invert(grid, mlen);

	errflag = 0;
final:
	if (!errflag) {
		dim->values   = grid;
		dim->len      = mlen;
		dim->range[0] = -90.;
		dim->range[1] = 90.;
		dim->cyclic   = 0;
		dim->title    = strdup("latitude");
		dim->unit     = strdup("degrees_north"); /* UDUNITS supports */
	} else {
		free(grid);
		free(dim);
		dim = NULL;
	}

	tiny_free(wght, wght_);
	return dim;
}


static GT3_Dim *
make_sfc1(int len, int idiv, unsigned flag)
{
	GT3_Dim *dim = NULL;
	double *grid;

	if (len != 1 || idiv != 1 || flag != 0)
		return NULL;

	if ((dim = alloc_newdim()) == NULL
		|| (grid = (double *)malloc(sizeof(double))) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}
	grid[0] = 1.;

	dim->values = grid;
	dim->len    = 1;
	return dim;
}


static GT3_Dim *
make_num(int len, int idiv, unsigned flag)
{
	GT3_Dim *dim = NULL;
	double *grid;
	int i;

	if (idiv != 1)
		return NULL;

	if ((dim = alloc_newdim()) == NULL
		|| (grid = (double *)malloc(sizeof(double) * len)) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}

	for (i = 0; i < len; i++)
		grid[i] = (double)i;

	if (flag & MID_FLAG)
		for (i = 0; i < len; i++)
			grid[i] += 0.5;

	dim->range[0] = grid[0];
	dim->range[1] = grid[len - 1];

	if (flag & INVERT_FLAG)
		invert(grid, len);

	dim->values = grid;
	dim->len = len;
	return dim;
}


/*
 *  open GTAXLOC.* or GTAXWGT.* file in pathlist (:-separated).
 */
static GT3_File *
open_axisfile2(const char *name, const char *pathlist, const char *kind)
{
	char path[PATH_MAX + 1];
	char base[32];
	const char *head, *next, *tail;
	int len, baselen;
	GT3_File *fp = NULL;

	head = pathlist;
	tail = pathlist + strlen(pathlist);

	snprintf(base, sizeof base, "/%s.%s", kind, name);
	baselen = strlen(base);

	while (head < tail) {
		next = strchr(head, PATH_SEP);
		if (!next)
			next = tail;

		len = next - head;
		if (len > sizeof path - 1 - baselen) {
			head = next + 1;	/* this element is too long. */
			continue;
		}

		memcpy(path, head, len);
		memcpy(path + len, base, baselen + 1); /* including a null */

		if ((fp = GT3_open(path)) != NULL)
			break;
		GT3_clearLastError();
		head = next + 1;
	}
	return fp;
}


/*
 *  open GTAXLOC.* or GTAXWGT.* file.
 */
static GT3_File *
open_axisfile(const char *name, const char *kind)
{
	char path[PATH_MAX + 1];
	GT3_File *fp;
	char *gtax_path, *gtax_dir;

	if (!name)
		return NULL;

	/*
	 *  1) 'GTAX_PATH'
	 */
	gtax_path = getenv("GTAX_PATH");
	if (gtax_path) {
		fp = open_axisfile2(name, gtax_path, kind);
		if (fp)
			return fp;
	}

	/*
	 *  2) current directory if GTAX_PATH is not set.
	 */
	if (!gtax_path) {
		snprintf(path, sizeof path, "%s.%s", kind, name);

		fp = GT3_open(path);
		if (fp)
			return fp;
		GT3_clearLastError();
	}

	/*
	 *  3) GTAXDIR if GTAX_PATH is not set.
	 */
	if (!gtax_path && (gtax_dir = getenv("GTAXDIR")) != NULL) {
		snprintf(path, sizeof path, "%s/%s.%s", gtax_dir, kind, name);

		fp = GT3_open(path);
		if (fp)
			return fp;
		GT3_clearLastError();
	}

	/*
	 *  4) default directory.
	 */
	snprintf(path, sizeof path, "%s/%s.%s", default_dir, kind, name);
	fp = GT3_open(path);

	return fp;
}


/*
 *  setup GT3_Dim by loading GTAXLOC.* file.
 */
GT3_Dim *
GT3_loadDim(const char *name)
{
	GT3_Dim *dim = NULL;
	GT3_File *gh = NULL;
	GT3_Varbuf *var = NULL;
	GT3_HEADER head;
	int cyclic;
	char kind[2], buf[33];
	double dmin, dmax;
	double *grid;

	if ((gh = open_axisfile(name, "GTAXLOC")) == NULL
		|| GT3_readHeader(&head, gh) < 0
		|| (var = GT3_getVarbuf(gh)) == NULL
		|| GT3_readVarZ(var, 0) < 0) {

		goto final;
	}

	/*
	 *  get properties.
	 */
	(void)GT3_copyHeaderItem(kind, 2, &head, "DSET");
	cyclic = (kind[0] == 'C') ? 1 : 0;

	dmin = dmax = var->miss;
	(void)GT3_decodeHeaderDouble(&dmin, &head, "DMIN");
	(void)GT3_decodeHeaderDouble(&dmax, &head, "DMAX");

	if ((grid = (double *)malloc(sizeof(double) * var->dimlen[0])) == NULL
		|| (dim = alloc_newdim()) == NULL) {

		gt3_error(SYSERR, NULL);
		free(grid);
		goto final;
	}

	dim->name = strdup(name);
	dim->len  = var->dimlen[0];
	dim->range[0] = dmin != var->miss ? dmin : -HUGE_VAL;
	dim->range[1] = dmax != var->miss ? dmax :  HUGE_VAL;

	(void)GT3_copyVarDouble(grid, dim->len, var, 0, 1);

	dim->cyclic = cyclic;
	dim->values = grid;

	(void)GT3_copyHeaderItem(buf, sizeof buf, &head, "TITLE");
	if (buf[0] != '\0')
		dim->title = strdup(buf);

	(void)GT3_copyHeaderItem(buf, sizeof buf, &head, "UNIT");
	if (buf[0] != '\0')
		dim->unit = strdup(buf);

final:
	GT3_freeVarbuf(var);
	GT3_close(gh);

	return dim;
}


GT3_Dim *
GT3_getBuiltinDim(const char *name)
{
	char base[16 + 1];
	int rval;
	int len, idiv;
	unsigned flag;
	struct axistab {
		const char *name;
		GT3_Dim *(*func)(int, int, unsigned);
	};
	struct axistab builtin[] = {
		{ "GLON",   make_glon },
		{ "GLAT",   make_glat },
		{ "GGLA",   make_ggla },
		{ "SFC",    make_sfc1 },
		{ "NUMBER", make_num  },
		{ "",       make_num  }
	};

	if (name == NULL)
		return NULL;

	rval = parse_axisname(name, base, &len, &idiv, &flag);

	if (rval == 0) { /* parsed successfully */
		int i;
		GT3_Dim *dim = NULL;

		for (i = 0; i < sizeof builtin / sizeof(struct axistab); i++)
			if (strcmp(base, builtin[i].name) == 0) {

				dim = builtin[i].func(len, idiv, flag);
				break;
			}
		if (dim) {
			debug1("built-in axis: %s", name);
			dim->name = strdup(name);
			return dim;
		}
	}
	return NULL;
}


int
GT3_getDimlen(const char *name)
{
	char base[16 + 1];
	int rval;
	int len, idiv;
	unsigned flag;
	GT3_Dim *dim;

	if (name == NULL)
		return -1;

	rval = parse_axisname(name, base, &len, &idiv, &flag);
	if (rval == 0)
		return len * idiv;

	dim = GT3_loadDim(name);
	rval = dim ? dim->len - dim->cyclic : -1;

	GT3_freeDim(dim);
	return rval;
}


/*
 *  GT3_getDim() constructs GT3_Dim by its name.
 *  If the name is well-known, built-in generator is called.
 */
GT3_Dim *
GT3_getDim(const char *name)
{
	GT3_Dim *dim;

	dim = GT3_getBuiltinDim(name);
	return dim ? dim : GT3_loadDim(name);
}


void
GT3_freeDim(GT3_Dim *dim)
{
	if (dim) {
		free(dim->name);
		free(dim->values);
		free(dim->title);
		free(dim->unit);
		free(dim);
	}
}


/*
 *  for GT3_DimBound ...
 */
static int
cellbnd_glon(double *bnd, int len, int idiv, unsigned flag)
{
	double bnd0, bnd1;
	double delta;

	if (flag & C_FLAG) {
		bnd0 = -180.;
		bnd1 =  180.;
	} else {
		bnd0 = 0.;
		bnd1 = 360.;
	}

	if (idiv > 1) {
		delta = (1. - 1. / idiv) * 180. / len;
		bnd0 -= delta;
		bnd1 -= delta;
	}

	delta = 180. / (len * idiv);
	if ((flag & MID_FLAG) == 0) {
		bnd0 -= delta;
		bnd1 -= delta;
	}
	uniform_bnd(bnd, bnd0, bnd1, len * idiv + 1);
	return 0;
}


/*
 *  bnd must have the space of (len * idiv + 1).
 */
static int
cellbnd_ggla(double *bnd, int len, int idiv, unsigned flag)
{
	double grid_[1024], wght_[1024];
	double *grid, *wght;
	double coef, b0, b1;
	int i, m, mlen;

	grid = (double *)tiny_alloc(grid_, sizeof grid_, sizeof(double) * len);
	wght = (double *)tiny_alloc(wght_, sizeof wght_, sizeof(double) * len);
	if (grid == NULL || wght == NULL)
		return -1;

	/*
	 *  \mu = cos \theta: [-1, +1]  from South to North.
	 */
	gauss_legendre(grid, wght, len);

	bnd[0] = -1.;
	for (i = 1; i <= (len + 1) / 2; i++)
		bnd[i*idiv] = bnd[(i-1)*idiv] + wght[i-1];

	/*
	 *  \mu -> latitude (in degree)
	 */
	bnd[0] = -90.0;				/* south pole */
	for (i = 1; i <= (len + 1) / 2; i++)
		bnd[i*idiv] = 90. * (1. - acos(bnd[i*idiv]) * M_2_PI);

	/*
	 *  mosaic (if idiv > 1)
	 */
	for (m = 1; m < idiv; m++)
		for (i = 0; i < (len + 1) / 2; i++) {
			b0 = bnd[i*idiv];
			b1 = bnd[(i+1)*idiv];
			coef = (1. * m) / idiv;
			bnd[i*idiv + m] = (1. - coef) * b0 + coef * b1;
		}

	/* north hemispehre */
	mlen = len * idiv + 1;
	for (i = 0; i < mlen / 2; i++)
		bnd[mlen - 1 - i] = -bnd[i];

	if (mlen % 2)
		bnd[mlen / 2] = 0.;		/* EQ */

	if ((flag & INVERT_FLAG) == 0)
		invert(bnd, mlen);

	tiny_free(grid, grid_);
	tiny_free(wght, wght_);
	return 0;
}


static int
cellbnd_glat(double *bnd, int len, int idiv, unsigned flag)
{
	len *= idiv;

	if ((flag & MID_FLAG) || len % 2 == 0)
		uniform_bnd(bnd, 90.0, -90., len + 1);
	else {
		if (len > 1) {
			double delta;
			delta = 90. / (len - 1);
			uniform_bnd(bnd, 90. + delta, -90. - delta, len + 1);
		}
		bnd[0] = 90.;
		bnd[len] = -90.;
	}
	if (flag & INVERT_FLAG)
		invert(bnd, len + 1);
	return 0;
}


static double *
weight_glon(double *temp, int len, int idiv, unsigned flag)
{
	double w;
	int i;

	len *= idiv;
	if (len <= 0)
		return NULL;

	if (!temp
		&& (temp = (double *)malloc(sizeof(double) * (len + 1))) == NULL)
		return NULL;

	w = 360. / len;
	for (i = 0; i < len; i++)
		temp[i] = w;
	temp[len] = 0.;
	return temp;
}

#define sindeg(x) sin(M_PI * x  / 180.)

/*
 *  get GGLA*'s weight.
 *
 *  weight_ggla(wgth, 160, 2, 0) for "GGLA160x2"
 */
static double *
weight_ggla(double *weight, int len, int idiv, unsigned flag)
{
	if (!weight)
		weight = (double *)malloc(sizeof(double) * len * idiv);

	if (!weight)
		return NULL;

	if (idiv == 1) {
		double grid_[1024], wght_[1024];
		double *grid, *wght;
		int i;

		grid = tiny_alloc(grid_, sizeof grid_, sizeof(double) * len);
		wght = tiny_alloc(wght_, sizeof wght_, sizeof(double) * len);

		if (!grid || !wght) {
			tiny_free(wght, wght_);
			tiny_free(grid, grid_);
			return NULL;
		}

		gauss_legendre(grid, wght, len);
		for (i = 0; i < len; i++)
				weight[i] = 0.5 * wght[i];

		tiny_free(wght, wght_);
		tiny_free(grid, grid_);
	} else {
		double bnd_[1024];
		double *bnd;
		int i;
		int mlen = len * idiv;

		bnd = tiny_alloc(bnd_, sizeof bnd_, sizeof(double) * (mlen + 1));
		if (!bnd || cellbnd_ggla(bnd, len, idiv, flag) < 0)
			return NULL;

		for (i = 0; i < (mlen + 1) / 2; i++)
			weight[i] = 0.5 * fabs((sindeg(bnd[i+1]) - sindeg(bnd[i])));

		for (i = (mlen + 1) / 2; i < mlen; i++)
			weight[i] = weight[mlen - 1 - i];

		tiny_free(bnd, bnd_);
	}
	return weight;
}


static int
weight_latitude(double *wght, const double *lat, int len)
{
	double bnd_[1024];
	double *bnd, fact;
	int i, len2;

	/*
	 *  we need half only.
	 */
	len2 = (len + 1) / 2;
	bnd = (double *)tiny_alloc(bnd_, sizeof bnd_,
							   sizeof(double) * (len2 + 1));
	if (bnd == NULL) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	fact = (lat[0] < lat[1]) ? -0.5 : 0.5;
	for (i = 1; i < len2 + 1; i++) {
		bnd[i] = fact * (lat[i-1] + lat[i]);
		bnd[i] = M_PI / 180.0 * (90.0 - bnd[i]);
	}
	bnd[0] = 0.; /* north pole (theta == 0) */

	for (i = 0; i < len2; i++)
		wght[i] = 0.5 * (cos(bnd[i]) - cos(bnd[i+1]));

	for (i = len2; i < len; i++)
		wght[i] = wght[len - 1 - i];

	tiny_free(bnd, bnd_);
	return 0;
}


static double *
weight_glat(double *wght, int len, int idiv, unsigned flag)
{
	GT3_Dim *dim;

	if ((dim = make_glat(len, idiv, flag)) == NULL)
		return NULL;

	if (!wght && (wght = (double *)malloc(
					  sizeof(double) * dim->len)) == NULL)
		gt3_error(SYSERR, NULL);

	if (wght)
		weight_latitude(wght, dim->values, dim->len);

	GT3_freeDim(dim);
	return wght;
}


/*
 *  get weights of a specified axis by loading GTAXWGT.* file.
 */
double *
GT3_loadDimWeight(const char *name)
{
	GT3_File *gh = NULL;
	GT3_Varbuf *var = NULL;
	double *wght = NULL;

	if ((gh = open_axisfile(name, "GTAXWGT")) == NULL
		|| (var = GT3_getVarbuf(gh)) == NULL
		|| GT3_readVarZ(var, 0) < 0)
		goto final;

	if ((wght = (double *)malloc(sizeof(double) * var->dimlen[0])) == NULL) {
		gt3_error(SYSERR, NULL);
		goto final;
	}
	(void)GT3_copyVarDouble(wght, var->dimlen[0], var, 0, 1);

final:
	GT3_freeVarbuf(var);
	GT3_close(gh);
	return wght;
}


/*
 *  get weights of a specifed axis (by name).
 */
double *
GT3_getBuiltinDimWeight(const char *name)
{
	char base[16 + 1];
	int rval;
	int len, idiv;
	unsigned flag;
	struct axistab {
		char *name;
		double *(*func)(double *, int, int, unsigned);
	};
	struct axistab builtin[] = {
		{ "GLON", weight_glon },
		{ "GLAT", weight_glat },
		{ "GGLA", weight_ggla }
	};

	if (name == NULL)
		return NULL;

	rval = parse_axisname(name, base, &len, &idiv, &flag);

	if (rval == 0) { /* parsed successfully */
		int i;
		double *temp = NULL;

		for (i = 0; i < sizeof builtin / sizeof(struct axistab); i++)
			if (strcmp(base, builtin[i].name) == 0) {
				temp = builtin[i].func(NULL, len, idiv, flag);
				break;
			}
		if (temp) {
			debug1("built-in axis: %s", name);
			return temp;
		}
	}
	return NULL;
}


/*
 *  get weights of a specifed axis (by name).
 */
double *
GT3_getDimWeight(const char *name)
{
	double *temp;

	temp = GT3_getBuiltinDimWeight(name);
	return temp ? temp : GT3_loadDimWeight(name);
}


/*
 *  write GTAXLOC.* file into a file-stream.
 */
int
GT3_writeDimFile(FILE *fp, const GT3_Dim *dim, const char *fmt)
{
	GT3_HEADER head;
	int rval;

	GT3_initHeader(&head);
	GT3_setHeaderString(&head, "DSET",
						dim->cyclic ? "CAXLOC" : "AXLOC");
	GT3_setHeaderString(&head, "ITEM", dim->name);
	GT3_setHeaderString(&head, "AITM1", dim->name);
	GT3_setHeaderDouble(&head, "DMIN", dim->range[0]);
	GT3_setHeaderDouble(&head, "DMAX", dim->range[1]);
	GT3_setHeaderString(&head, "TITLE", dim->title);
	GT3_setHeaderString(&head, "UNIT", dim->unit);

	if (dim->title
		&& (strcmp(dim->title, "longitude") == 0
			|| strcmp(dim->title, "latitude") == 0)) {
		GT3_setHeaderDouble(&head, "DIVS", 10.);
		GT3_setHeaderDouble(&head, "DIVL", 30.);
	}

	rval = GT3_write(dim->values, GT3_TYPE_DOUBLE,
					 dim->len, 1, 1,
					 &head, fmt, fp);
	return rval;
}


/*
 *  write GTAXWGT.* file into a file-stream.
 */
int
GT3_writeWeightFile(FILE *fp, const GT3_Dim *dim, const char *fmt)
{
	GT3_HEADER head;
	double *wght;
	int rval;

	if ((wght = GT3_getDimWeight(dim->name)) == NULL)
		return -1;

	GT3_initHeader(&head);
	GT3_setHeaderString(&head, "DSET",
						dim->cyclic ? "CAXWGT" : "AXWGT");
	GT3_setHeaderString(&head, "ITEM", dim->name);
	GT3_setHeaderString(&head, "AITM1", dim->name);

	rval = GT3_write(wght, GT3_TYPE_DOUBLE,
					 dim->len, 1, 1,
					 &head, fmt, fp);
	free(wght);
	return rval;
}


static GT3_DimBound *
new_dimbound(const char *name, int len_orig, int len)
{
	GT3_DimBound *dimbnd = NULL;
	double *bnd = NULL;

	if ((dimbnd = (GT3_DimBound *)malloc(sizeof(GT3_DimBound))) == NULL
		|| (bnd = (double *)malloc(sizeof(double) * len)) == NULL) {

		gt3_error(SYSERR, NULL);
		free(bnd);
		free(dimbnd);
		return NULL;
	}

	dimbnd->name = strdup(name);
	dimbnd->len  = len;
	dimbnd->bnd  = bnd;
	dimbnd->len_orig = len_orig;

	return dimbnd;
}


void
GT3_freeDimBound(GT3_DimBound *dimbnd)
{
	if (dimbnd) {
		free(dimbnd->name);
		free(dimbnd->bnd);
		free(dimbnd);
	}
}


GT3_DimBound *
GT3_getDimBound(const char *name)
{
	struct dimbndtab {
		const char *key;
		int (*bndfunc)(double *, int, int, unsigned);
	};
	struct dimbndtab tab[] = {
		{ "GLON", cellbnd_glon },
		{ "GGLA", cellbnd_ggla },
		{ "GLAT", cellbnd_glat }
	};
	char base[16 + 1];
	int i, id, rval;
	int len, idiv;
	unsigned flag;
	GT3_DimBound *dimbnd = NULL;

	if (name == NULL)
		return NULL;

	rval = parse_axisname(name, base, &len, &idiv, &flag);
	if (rval < 0)
		return NULL;

	id = -1;
	for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
		if (strcmp(base, tab[i].key) == 0) {
			id = i;
			break;
		}

	if (id == -1)
		return NULL;

	if ((dimbnd = new_dimbound(name, len * idiv, len * idiv + 1)) != NULL) {
		if ((*tab[id].bndfunc)(dimbnd->bnd, len, idiv, flag) <  0) {
			GT3_freeDimBound(dimbnd);
			return NULL;
		}
	}
	return dimbnd;
}


#ifdef TEST_MAIN
int
zero(double x)
{
	/* printf("%g\n", x); */
	return fabs(x) < 1e-10;
}

int
zero2(double x, double eps)
{
	/* printf("%g\n", x); */
	return fabs(x) < eps;
}

double
sum(double *x, int len)
{
	double s;
	int i;

	for (s = 0., i = 0; i < len; i++)
		s += x[i];
	return s;
}

void
print_dim(GT3_Dim *dim)
{
	int i;

	printf("%8d %22.16f\n", 0, dim->values[0]);
	for (i = 1; i < dim->len; i++)
		printf("%8d %22.16f %20.10g\n",
			   i, dim->values[i], dim->values[i] - dim->values[i-1]);
}


void
test_glon(void)
{
	GT3_Dim *dim;
	int i;

	/* GLON1 */
	dim = make_glon(1, 1, 0);
	assert(dim->len == 2);
	assert(dim->values[0] == .0);
	assert(dim->values[1] == 360.);
	GT3_freeDim(dim);

	/* GLON2 */
	dim = make_glon(2, 1, 0);
	assert(dim->len == 3);
	assert(dim->values[0] == .0);
	assert(dim->values[1] == 180.);
	assert(dim->values[2] == 360.);
	GT3_freeDim(dim);

	/* GLON320 */
	dim = make_glon(320, 1, 0);
	assert(dim->len == 321);
	assert(dim->values[0] == .0);
	assert(dim->values[1] == 1.125);
	for (i = 0; i < dim->len; i++) {
		assert(dim->values[i] == 1.125 * i);
	}
	GT3_freeDim(dim);

	/* GLON320x4 */
	dim = make_glon(320, 4, 0);
	assert(dim->len == 320 * 4 + 1);
	assert(zero(dim->values[0] + 0.421875));
#if 0
	for (i = 0; i < dim->len; i++)
		printf("%3d %20.8f\n", i, dim->values[i]);
#endif
	GT3_freeDim(dim);

	/* GLON144M */
	dim = make_glon(144, 1, MID_FLAG);
	assert(dim->len == 145);
	assert(zero(dim->values[0] - (180. / 144)));
	GT3_freeDim(dim);
}


void
test_glat(void)
{
	GT3_Dim *dim;

	/* GLAT1M */
	dim = make_glat(1, 1, MID_FLAG);
	assert(dim->values[0] == .0);
	GT3_freeDim(dim);

	/* GLAT180M */
	dim = make_glat(180, 1, MID_FLAG);
	assert(dim->values[0] == 89.5);
	GT3_freeDim(dim);

	/* GLAT180IM */
	dim = make_glat(180, 1, MID_FLAG | INVERT_FLAG);
	assert(dim->values[0] == -89.5);
	GT3_freeDim(dim);

	/* GLAT181 */
	dim = make_glat(181, 1, 0);
	assert(dim->values[0] == 90.0);
	assert(dim->values[1] == 89.0);
	GT3_freeDim(dim);
}


void
test_ggla(void)
{
	GT3_Dim *dim;

	/* GGLA1 */
	dim = make_ggla(1, 1, 0);
	assert(zero(dim->values[0]));
	GT3_freeDim(dim);

	/* GGLA160 */
	dim = make_ggla(160, 1, 0);
	assert(zero2(dim->values[0] - 89.1415194, 1e-6));
	GT3_freeDim(dim);

	/* GGLA160I */
	dim = make_ggla(160, 1, INVERT_FLAG);
	assert(zero2(dim->values[0] + 89.1415194, 1e-6));
	GT3_freeDim(dim);

	/* GGLA160Ix2 */
	dim = make_ggla(160, 2, INVERT_FLAG);
	assert(zero2(dim->values[0] + 89.6561821394, 1e-6));
	GT3_freeDim(dim);
}


void
test_etc(void)
{
	GT3_Dim *dim;
	int i;

	/* SFC1 */
	dim = GT3_getDim("SFC1");
	assert(dim && dim->len == 1 && dim->values[0] == 1.);
	GT3_freeDim(dim);

	/* NUMBER50 */
	dim = GT3_getDim("NUMBER50");
	assert(dim && dim->len == 50);
	for (i = 0; i < dim->len; i++) {
		assert(dim->values[i] == (double)i);
	}
	GT3_freeDim(dim);

	/* "" */
	dim = GT3_getDim("");
	assert(dim && dim->len == 1 && dim->values[0] == .0);
	GT3_freeDim(dim);
}


void
test_axisfile(void)
{
	GT3_Dim *dim1, *dim2;
	int i;

	dim1 = GT3_getDim("GLON320");
	assert(dim1);

	dim2 = GT3_loadDim("GLON320");
	assert(dim2);

	assert(dim1->len == dim2->len);
	for (i = 0; i < dim1->len; i++)
		assert(dim1->values[i] == dim2->values[i]);

	GT3_freeDim(dim1);
	GT3_freeDim(dim2);
}


void
bound_test(const char *name)
{
	GT3_Dim *dim;
	GT3_DimBound *dimbnd;
	int i;

	dim = GT3_getDim(name);
	dimbnd = GT3_getDimBound(name);
	assert(dim && dimbnd);

	if (dimbnd->bnd[0] < dimbnd->bnd[1])
		for (i = 0; i < dimbnd->len_orig; i++) {
			assert(dimbnd->bnd[i] < dimbnd->bnd[i+1]);
			assert(dimbnd->bnd[i] <= dim->values[i]);
			assert(dim->values[i] <= dimbnd->bnd[i+1]);
		}
	else
		for (i = 0; i < dimbnd->len_orig; i++) {
			assert(dimbnd->bnd[i] > dimbnd->bnd[i+1]);
			assert(dimbnd->bnd[i] >= dim->values[i]);
			assert(dim->values[i] >= dimbnd->bnd[i+1]);
		}

	GT3_freeDim(dim);
	GT3_freeDimBound(dimbnd);
}


int
main(int argc, char **argv)
{
	char buf[16 + 1];
	int rval, len, idiv;
	unsigned flag;

	rval = parse_axisname("GLON320", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "GLON") == 0
		   && len == 320 && idiv == 1 && flag == 0);

	rval = parse_axisname("GLON320x2", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "GLON") == 0
		   && len == 320 && idiv == 2 && flag == 0);

	rval = parse_axisname("GGLA160Ix2", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "GGLA") == 0
		   && len == 160 && idiv == 2 && flag == 1);

	rval = parse_axisname("GGLA160x2I", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "GGLA") == 0
		   && len == 160 && idiv == 2 && flag == 1);

	rval = parse_axisname("GGLA160x2IM", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "GGLA") == 0
		   && len == 160 && idiv == 2 && flag == 3);

	rval = parse_axisname("@EXTAX01", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "@EXTAX") == 0
		   && len == 1 && idiv == 1 && flag == 0);

	rval = parse_axisname("", buf, &len, &idiv, &flag);
	assert(rval == 0 && strcmp(buf, "") == 0
		   && len == 1 && idiv == 1 && flag == 0);

	/* rval < 0 */
	rval = parse_axisname("GLAT45I-GISS", buf, &len, &idiv, &flag);
	assert(rval == -1 && strcmp(buf, "GLAT") == 0
		   && len == 45 && idiv == 1 && flag == 1);

	rval = parse_axisname("CSIG20.M", buf, &len, &idiv, &flag);
	assert(rval == -1 && strcmp(buf, "CSIG") == 0
		   && len == 20 && idiv == 1);

	rval = parse_axisname("   abcdefghijklmnopq",
						  buf, &len, &idiv, &flag);
	assert(rval == -1 && strcmp(buf, "abcdefghijklmnop") == 0
		   && len == 0 && idiv == 1 && flag == 0);

	test_glon();
	test_glat();
	test_ggla();
	test_etc();

	/* test_axisfile(); */

	/* weight test */
	{
		double wght[640];

		/* GLON320 */
		weight_glon(wght, 320, 1, 0);
		assert(zero(wght[0] - 360./320));
		assert(zero(wght[320]));

		/* GGLA2 */
		weight_ggla(wght, 2, 1, 0);
		assert(zero(wght[0] - 0.5));
		assert(zero(wght[1] - 0.5));

		/* GGLA3 */
		weight_ggla(wght, 3, 1, 0);
		assert(zero(sum(wght, 3) - 1.));

		/* GGLA320 */
		weight_ggla(wght, 320, 1, 0);
		assert(zero(sum(wght, 320) - 1.));

		/* GGLA160x2 */
		weight_ggla(wght, 160, 2, 0);
		assert(zero(sum(wght, 320) - 1.));

		/* GGLA321 */
		weight_ggla(wght, 321, 1, 0);
		assert(zero(sum(wght, 321) - 1.));

		/* GLAT2 */
		weight_glat(wght, 2, 1, 0);
		assert(zero(wght[0] - .5));
		assert(zero(wght[1] - .5));

		/* GLAT3 */
		weight_glat(wght, 3, 1, 0);
		assert(zero(sum(wght, 3) - 1.));

		/* GLAT160M */
		weight_glat(wght, 160, 1, MID_FLAG);
		assert(zero(sum(wght, 160) - 1.));

		/* GLAT161 */
		weight_glat(wght, 161, 1, 0);
		assert(zero(sum(wght, 161) - 1.));
	}

	/* cell boundary */
	{
		double bnd[641];

		cellbnd_glon(bnd, 360, 1, MID_FLAG);
		assert(bnd[0] == 0. && bnd[360] == 360.);

		cellbnd_glon(bnd, 360, 1, 0);
		assert(bnd[0] == -.5 && bnd[360] == 359.5);

		cellbnd_glon(bnd, 360, 1, MID_FLAG | C_FLAG);
		assert(bnd[0] == -180. && bnd[360] == 180.);

		cellbnd_ggla(bnd, 2, 1, 0);
		assert(bnd[0] == 90. && bnd[1] == 0. && bnd[2] == -90.);

		cellbnd_ggla(bnd, 2, 1, INVERT_FLAG);
		assert(bnd[0] == -90. && bnd[1] == 0. && bnd[2] == 90.);

		cellbnd_glat(bnd, 2, 1, 0);
		assert(bnd[0] == 90. && bnd[1] == 0. && bnd[2] == -90.);

		cellbnd_ggla(bnd, 1, 1, 0);
		assert(bnd[0] == 90. && bnd[1] == -90);

		cellbnd_ggla(bnd, 1, 4, 0);
		assert(bnd[0] == 90. && bnd[1] == 45.
			   && bnd[2] == 0. && bnd[3] == -45. && bnd[4] == -90.);
	}

	bound_test("GLON320");
	bound_test("GLON320M");
	bound_test("GLON320MC");
	bound_test("GLON320x2");
	bound_test("GGLA2");
	bound_test("GGLA3");
	bound_test("GGLA64");
	bound_test("GGLA320");
	bound_test("GGLA320I");
	bound_test("GGLA320x2");
	bound_test("GLAT2");
	bound_test("GLAT2M");
	bound_test("GLAT3");
	bound_test("GLAT3M");
	bound_test("GLAT160");
	bound_test("GLAT161");
	bound_test("GLAT161M");

	/* GT3_getDimlen() */
	assert(GT3_getDimlen("") == 1);
	assert(GT3_getDimlen("SFC1") == 1);
	assert(GT3_getDimlen("GLON320") == 320);
	assert(GT3_getDimlen("GGLA160x2") == 320);
	assert(GT3_getDimlen("OCLONT1280") == 1280);

	return 0;
}
#endif
