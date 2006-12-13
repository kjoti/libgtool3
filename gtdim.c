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
#include "debug.h"

/*
 *  'GTAX_PATH' is newly introduced.
 *
 *  It is a set of directories where GTAXLOC.* files are searched.
 *  Directories are separated with ':'.
 */
#define PATH_SEP ':'

/*
 *  default axis dir, e.g., '/usr/local/share/gtool3/gt3'
 */
static const char *default_dir = DEFAULT_GTAXDIR;

/*
 *  axis type
 */
enum {
	TYPE_GLON,
	TYPE_GLAT,
	TYPE_GGLA,
	TYPE_ETC
};


#define INVERT_FLAG		1
#define MID_FLAG		2

#ifndef M_2_PI
#  define M_2_PI	0.63661977236758134308	/* 2/pi */
#endif



static int
parse_axisname(const char *name, char *base,
			   int *len, int *idiv, unsigned *flag)
{
	const char *p = name;
	char *q = base;
	int rval = 0;
	char *endptr;
	int cnt;

	while (*p == ' ') /* skip spaces. */
		p++;

	if (isdigit(*p)) /* Starting with a digit, it's invalid */
		rval = -1;

	/*
	 *  copy basename (at most 16 letters).
	 */
	cnt = 0;
	while (*p != '\0' && !isdigit(*p) && cnt++ < 16)
		*q++ = *p++;
	*q = '\0';

	/*
	 *  get size.
	 */
	if (isdigit(*p)) {
		*len = (int)strtol(p, &endptr, 10);
		p = endptr;
	} else
		*len = 1;

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
	double *bnd = NULL;
	double rdiv, coef;
	int i, m;

	if ((bnd = (double *)malloc(sizeof(double) * (len+1))) == NULL) {
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
	bnd[0]   = -1.;
	bnd[len] =  1.;
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

	free(bnd);
	return 0;
}


static GT3_Dim *
make_glon(int len, int idiv, unsigned flag)
{
	GT3_Dim *dim = NULL;
	double *grid;
	int mlen;

	/*
	 *  XXX
	 *  Cyclic axes need additional one more grid point,
	 *  which is identical to the first grid (grid[0]).
	 */
	mlen = len * idiv + 1;

	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL
		|| (grid = (double *)malloc(sizeof(double) * mlen)) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}

	uniform_bnd(grid, 0., 360., mlen);
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
	dim->range[0] = 0.;
	dim->range[1] = 360.;
	dim->cyclic   = 1;

	return dim;
}


static GT3_Dim *
make_glat(int len, int idiv, unsigned flag)
{
	double *grid;
	GT3_Dim *dim;

	if (idiv > 1)
		return NULL;

	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL
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
	dim->cyclic   = 1;

	return dim;
}


static GT3_Dim *
make_ggla(int len, int idiv, unsigned flag)
{
	double *grid = NULL, *wght = NULL;
	GT3_Dim *dim = NULL;
	int errflag = 0;
	int i, mlen;


	if (flag & MID_FLAG)
		return NULL;

	mlen = len * idiv;
	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL
		|| (grid = (double *)malloc(sizeof(double) * mlen)) == NULL
		|| (wght = (double *)malloc(sizeof(double) *  len)) == NULL) {
		gt3_error(SYSERR, NULL);
		goto final;
	}

	/*
	 *  get Gauss-Legendre
	 */
	gauss_legendre(grid, wght, len);

	if (idiv > 1) {
		if (latitude_mosaic(grid, wght, len, idiv) < 0) {
			errflag = 1;
			goto final;
		}
	} else {
		/*
		 *  convert from "mu = cos(theta)" to latitude (in degree).
		 *
		 *  -1.0 => -90.0, 1.0 => +90.0
		 */
		for (i = 0; i < len; i++)
			grid[i] = 90. * (1. - acos(grid[i]) * M_2_PI);
	}

	assert(grid[0]     > -90. && grid[0]     < 90.);
	assert(grid[len-1] > -90. && grid[len-1] < 90.);

	/*
	 *  The "GGLA" is directed from north to south.
	 *  It is opposite to the mu [-1, 1].
	 */
	if ((flag & INVERT_FLAG) == 0)
		invert(grid, mlen);

final:
	if (!errflag) {
		dim->values   = grid;
		dim->len      = mlen;
		dim->range[0] = -90.;
		dim->range[1] = 90.;
		dim->cyclic   = 0;
	} else {
		free(grid);
		free(dim);
		dim = NULL;
	}
	free(wght);
	return dim;
}


static GT3_Dim *
make_sfc1(int len, int idiv, unsigned flag)
{
	GT3_Dim *dim = NULL;
	double *grid;

	if (len != 1 || idiv != 1 || flag != 0)
		return NULL;

	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL
		|| (grid = (double *)malloc(sizeof(double))) == NULL) {
		gt3_error(SYSERR, NULL);
		free(dim);
		return NULL;
	}
	grid[0] = 1.;

	dim->values = grid;
	dim->len    = 1;
	dim->range[0] = 1.;
	dim->range[1] = 1.;
	dim->cyclic  = 0;
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

	if ((dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL
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
	dim->cyclic = 0;
	return dim;
}


/*
 *  open GTAXLOC.* file in pathlist (:-separated).
 */
static GT3_File *
open_locfile2(const char *name, const char *pathlist)
{
	char path[PATH_MAX + 1];
	const char *head, *next, *tail;
	int len, baselen;
	GT3_File *fp = NULL;


	head = pathlist;
	tail = strchr(pathlist, '\0');

	baselen = strlen("/GTAXLOC.") + strlen(name);

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
		snprintf(path + len, sizeof path - len, "/GTAXLOC.%s", name);

		if ((fp = GT3_open(path)) != NULL)
			break;

		head = next + 1;
	}
	return fp;
}


/*
 *  open GTAXLOC.* file.
 */
static GT3_File *
open_locfile(const char *name)
{
	char path[PATH_MAX + 1];
	GT3_File *fp;
	char *gtax_path, *gtax_dir;

	/*
	 *  1) 'GTAX_PATH'
	 */
	gtax_path = getenv("GTAX_PATH");
	if (gtax_path) {
		fp = open_locfile2(name, gtax_path);
		if (fp)
			return fp;
	}

	/*
	 *  2) current directory if GTAX_PATH is not set.
	 */
	if (!gtax_path) {
		snprintf(path, sizeof path, "GTAXLOC.%s", name);

		fp = GT3_open(path);
		if (fp)
			return fp;
	}

	/*
	 *  3) GTAXDIR if GTAX_PATH is not set.
	 */
	if (!gtax_path && (gtax_dir = getenv("GTAXDIR")) != NULL) {
		snprintf(path, sizeof path, "%s/GTAXLOC.%s", gtax_dir, name);

		fp = GT3_open(path);
		if (fp)
			return fp;
	}

	/*
	 *  4) default directory.
	 */
	snprintf(path, sizeof path, "%s/GTAXLOC.%s", default_dir, name);
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
	char kind[2];
	double dmin, dmax;
	double *grid;


	if ((gh = open_locfile(name)) == NULL
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
		|| (dim = (GT3_Dim *)malloc(sizeof(GT3_Dim))) == NULL) {

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

final:
	GT3_freeVarbuf(var);
	GT3_close(gh);

	return dim;
}


/*
 *  GT3_getDim() constructs GT3_Dim by its name.
 *  If the name is well-known, built-in generator is called.
 */
GT3_Dim *
GT3_getDim(const char *name)
{
	char base[16 + 1];
	int rval;
	int len, idiv;
	unsigned flag;
	struct axistab {
		char *name;
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
	return GT3_loadDim(name);
}


void
GT3_freeDim(GT3_Dim *dim)
{
	if (dim) {
		free(dim->name);
		free(dim->values);
		free(dim);
	}
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

	rval = parse_axisname("   abcdefghijklmnopq",
						  buf, &len, &idiv, &flag);
	assert(rval == -1 && strcmp(buf, "abcdefghijklmnop") == 0
		   && len == 1 && idiv == 1 && flag == 0);

	test_glon();
	test_glat();
	test_ggla();
	test_etc();

	/* test_axisfile(); */
	return 0;
}
#endif
