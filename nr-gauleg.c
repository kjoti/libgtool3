/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  Numerical Recipes in C 2nd Edition
 *  4.5 Gaussian Quadratures and Orthogonal Polynomials
 */
#include <math.h>

#define EPS 2.2204460492503131e-14

void
gauleg(double x1, double x2, double x[], double w[], int n)
{
	int m, j, i, ir;
	double z1, z, xm, xl, pp, p3, p2, p1;

	m = (n + 1) / 2;
	xm = .5 * (x2 + x1);
	xl = .5 * (x2 - x1);

	for (i = 0; i < m; i++) {
		z = cos(M_PI * (i + 0.75) / (n + 0.5));

		do {
			p1 = 1.;
			p2 = 0.;

			for (j = 1; j <= n; j++) {
				p3 = p2;
				p2 = p1;
				p1 = ((2.*j - 1.) * z * p2 - (j - 1.) * p3) / j;
			}
			pp = n * (z * p1 - p2) / (z * z - 1.);
			z1 = z;
			z  = z1 - p1 / pp;
		} while (fabs(z - z1) > EPS);

		ir = n - 1 - i;
		x[i]  = xm - xl * z;
		x[ir] = xm + xl * z;
		w[i]  = 2. * xl / ((1. - z * z) * pp * pp);
		w[ir] = w[i];
	}
}


#ifdef TEST_MAIN
#include <assert.h>
#include <stdio.h>

#ifndef min
#  define min(a,b) ((a) > (b) ? (b) : (a))
#endif


double
plgndr(int l, int m, double x)
{
	double fact, pll, pmm, pmmp1, somx2;
	int i, ll;

	pmm = 1.0;
	if (m > 0) {
		somx2 = sqrt((1. - x) * (1. + x));
		fact = 1.;
		for (i = 0; i < m; i++) {
			pmm *= -fact * somx2;
			fact += 2.;
		}
	}

	if (l == m)
		return pmm;

	pmmp1 = x * (2*m + 1) * pmm;
	for (ll = m + 2; ll <= l; ll++) {
		pll = (x * (2*ll - 1) * pmmp1 - (ll + m -1) * pmm) / (ll - m);
		pmm = pmmp1;
		pmmp1 = pll;
	}
	return pmmp1;
}


int
zero(double x)
{
	/* printf("%g\n", x); */
	return fabs(x) < 1e-10;
}


int
order(double v[], int len)
{
	int i;

	if (len < 2)
		return 0;

	for (i = 1; i < len; i++)
		if (v[i] <= v[i-1])
			return 0;

	return 1;
}


int
main(int argc, char **argv)
{
	double x[1800], w[1800], sum;
	int num;
	int i;

	gauleg(-1., 1, x, w, 1);
	assert(zero(x[0]));

	gauleg(-1., 1, x, w, 2);
	assert(zero(3.*x[0]*x[0] - 1));

	gauleg(-1., 1, x, w, 3);
	assert(zero(x[1]));

	num = 360;
	gauleg(-1., 1, x, w, num);
	assert(x[0] > -1. && x[num-1] < 1.);
	assert(order(x, num));
	sum = 0.;

	/* weight check */
	for (i = 0; i < num; i++)
		sum += w[i];
	assert(zero(sum - 2.));

	/* symmetry check */
	for (i = 0; i < num / 2; i++) {
		assert(zero(x[i] + x[num - 1 - i]));
	}
	for (i = 0; i < num; i++) {
		assert(zero(plgndr(num, 0, x[i])));
	}

#if 0
	for (i = 0; i < min(16, num); i++)
		printf("%3d %20.16f %20.16f %20.12g\n",
			   i, 90. * (1 - acos(x[i]) * M_2_PI), x[i], w[i]);
#endif

	return 0;
}
#endif
