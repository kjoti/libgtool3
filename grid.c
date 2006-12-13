/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  grid.c
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#include "internal.h"


int
uniform_center(double *grid, double x0, double x1, int len)
{
	double bnd0, bnd1, dx;
	int i;

	if (len < 1)
		return -1;

	dx = .5 / len;
	for (i = 0; i < len; i++) {
		bnd0 = (len - i)     * x0 + i       * x1;
		bnd1 = (len - i - 1) * x0 + (i + 1) * x1;

		grid[i] = (bnd0 + bnd1) * dx;
	}
	return 0;
}


int
uniform_bnd(double *grid, double x0, double x1, int len)
{
	double dx;
	int i;

	if (len < 2)
		return -1;

	dx = 1. / (len - 1);
	for (i = 0; i < len; i++)
		grid[i] = ((len - 1 - i) * x0 + i * x1) * dx;

	return 0;
}
