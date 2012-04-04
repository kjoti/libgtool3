/*
 * gauss-legendre.c -- Gaussian Quadratures
 */
#include <math.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#define EPS 2.2204460492503131e-16 /* machine epsilon */


void
gauss_legendre(double sol[], double wght[], int nth)
{
    double x, p[3], dpdx, dx;
    int hnum, i, j, n;

    hnum = (nth + 1) / 2;

    for (i = 0; i < hnum; i++) {
        x = cos(M_PI * (i + 0.75) / (nth + 0.5));

        do {
            p[1] = 1.;
            p[2] = x;

            for (n = 2; n <= nth; n++) {
                p[0] = p[1];
                p[1] = p[2];

                p[2] = 2. * x * p[1] - p[0] - (x * p[1] - p[0]) / n;
            }
            dpdx = nth * (p[1] - x * p[2]) / (1. - x * x);

            dx = -p[2] / dpdx;
            x += dx;
        } while (fabs(dx) > 4 * EPS);

        j = nth - 1 - i;
        sol[i]  = -x;
        sol[j]  = x;
        wght[i] = wght[j] = 2. / ((1. - x * x) * dpdx * dpdx);
    }
}


#ifdef TEST_MAIN
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef min
#  define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a, b) ((a) > (b) ? (a) : (b))
#endif


/*
 * P_n(x): n-th Legendre polynomial function.
 *
 * P_n(x) = 2 x P_{n-1}(x) - P_{n-2}(x) - (x P_{n-1}(x) - P_{n-2}(x)) / n
 *
 * x   range: -1 <= x <= 1
 * nth range: 0, 1, 2, ...
 */
double
legendre_poly(double x, int nth)
{
    double p[3];
    int n;

    if (nth <= 0)
        return 1.;

    p[0] = 1.;
    p[1] = x;
    for (n = 2; n <= nth; n++) {
        p[2] = 2. * x * p[1] - p[0] - (x * p[1] - p[0]) / n;

        p[0] = p[1];
        p[1] = p[2];
    }
    return p[1];
}


int
zero(double x, double eps)
{
    return fabs(x) <= eps;
}


double
P4(double x)
{
    x *= x;
    return 0.125 * (3. + x * (-30. + x * 35.));
}


double
P5(double x)
{
    double x2 = x * x;
    return 0.125 * x * (15. + x2 * (-70. + x2 * 63.));
}


void
check_P4(double x0, double x1, int nsamp)
{
    double x, y1, y2;
    int i;

    for (i = 0; i < nsamp; i++) {
        x = ((nsamp - 1 - i) * x0 + i * x1) / (nsamp - 1);
        y1 = legendre_poly(x, 4);
        y2 = P4(x);

        assert(zero(y1 - y2, 1e-12));
    }
}


void
check_P5(double x0, double x1, int nsamp)
{
    double x, y1, y2;
    int i;

    for (i = 0; i < nsamp; i++) {
        x = ((nsamp - 1 - i) * x0 + i * x1) / (nsamp - 1);
        y1 = legendre_poly(x, 5);
        y2 = P5(x);

        assert(zero(y1 - y2, 1e-12));
    }
}


double
sum(const double *values, size_t nelems)
{
    double sval = 0.;
    int i;

    for (i = 0; i < nelems; i++)
        sval += values[i];
    return sval;
}


void
check_root(int nth)
{
    double xzero[1280], wght[1280];
    double x1, x2, y1, y2;
    int i;

    gauss_legendre(xzero, wght, nth);
    assert(zero(sum(wght, nth) - 2., 1e-12));

    for (i = 1; i < nth; i++)
        assert(xzero[i] - xzero[i-1] > 8 * EPS);

    for (i = 0; i < nth; i++) {
        x1 = max(-1., xzero[i] - 2 * EPS);
        x2 = min(1., xzero[i] + 2 * EPS);
        y1 = legendre_poly(x1, nth);
        y2 = legendre_poly(x2, nth);
        assert(y1 * y2 < 0.);
    }
}


int
main(int argc, char **argv)
{
    int i;

    check_P4(-1., 1., 1001);
    check_P5(-1., 1., 1001);

    for (i = 1; i < 20; i++)
        check_root(i);
    check_root(160);
    check_root(161);
    check_root(320);
    check_root(321);
    check_root(640);
    check_root(641);
    check_root(1280);
    return 0;
}
#endif
