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
	double dxh;
	int i, n;

	if (len < 1)
		return -1;

	dxh = .5 / len;
	for (i = 0; i < len; i++) {
		n = 2 * i + 1;
		grid[i] = ((2 * len - n) * x0 + n * x1) * dxh;
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
