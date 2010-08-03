/*
 * grid.c
 */
#include "internal.h"


static void
center(double *grid, double x0, double x1, int len)
{
    double dxh;
    int i, n;

    if (len < 1)
        return;

    dxh = .5 / len;
    for (i = 0; i < len; i++) {
        n = 2 * i + 1;
        grid[i] = ((2 * len - n) * x0 + n * x1) * dxh;
    }
}


static void
bound(double *grid, double x0, double x1, int len)
{
    double dx;
    int i;

    if (len < 2)
        return;

    dx = 1. / (len - 1);
    for (i = 1; i < len - 1; i++)
        grid[i] = ((len - 1 - i) * x0 + i * x1) * dx;
    grid[0] = x0;
    grid[len - 1] = x1;
}


int
uniform_center(double *grid, double x0, double x1, int len)
{
    int i, half;

    if (len < 1)
        return -1;

    if (x0 == -x1) {
        half = len / 2;

        if (len % 2 == 0)
            center(grid + half, 0., x1, half);
        else {
            center(grid + half + 1, x1 / len, x1, half);
            grid[half] = 0.;
        }
        for (i = 0; i < half; i++)
            grid[i] = -grid[len - 1 - i];
        return 0;
    } else
        center(grid, x0, x1, len);

    return 0;
}


int
uniform_bnd(double *grid, double x0, double x1, int len)
{
    int i, half;

    if (len < 2)
        return -1;

    if (x0 == -x1) {
        half = len / 2;

        if (len % 2 == 1)
            bound(grid + half, 0., x1, half + 1);
        else
            bound(grid + half, x1 / (len - 1), x1, half);

        for (i = 0; i < half; i++)
            grid[i] = -grid[len - 1 - i];
        return 0;
    } else
        bound(grid, x0, x1, len);

    return 0;
}


#ifdef TEST_MAIN
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define EPS  2.2204460492503131e-16 /* machine epsilon in double */
#define PRINT(x, n) \
    { int i; for (i = 0; i < (n); i++) printf("%4d %20.16f\n", i, (x)[i]); }

static int
equal(double x, double ref)
{
    if (x == ref)
        return 255;

    return ref == 0.
        ? fabs(x) < 8 * EPS
        : fabs(1. - x / ref) < 8 * EPS;
}


static void
test1(void)
{
    double x[4];

    uniform_center(x, 0., 1., 4);
    assert(equal(x[0], 0.125));
    assert(equal(x[1], 0.25 + 0.125));
    assert(equal(x[2], 0.50 + 0.125));
    assert(equal(x[3], 0.75 + 0.125));

    uniform_center(x, -1., 1., 4);
    assert(equal(x[2], 0.25));
    assert(equal(x[3], 0.75));
    assert(x[0] == -x[3]);
    assert(x[1] == -x[2]);

    uniform_center(x, 1., -1., 4);
    assert(equal(x[2], -0.25));
    assert(equal(x[3], -0.75));
    assert(x[0] == -x[3]);
    assert(x[1] == -x[2]);

    uniform_center(x, 0., 1., 3);
    assert(equal(x[0], 1. / 6));
    assert(equal(x[1], .5));
    assert(equal(x[2], 5. / 6));

    uniform_center(x, 1., 0., 3);
    assert(equal(x[0], 5. / 6));
    assert(equal(x[1], .5));
    assert(equal(x[2], 1. / 6));

    uniform_center(x, -1., 1., 3);
    assert(x[1] == 0.);
    assert(equal(x[2], 2. / 3));
    assert(x[0] == -x[2]);
}


static void
test2(void)
{
    double x[19];

    uniform_bnd(x, 0., 1., 5);
    assert(x[0] == 0.);
    assert(equal(x[1], 0.25));
    assert(equal(x[2], 0.5));
    assert(equal(x[3], 0.75));
    assert(x[4] == 1.);

    uniform_bnd(x, 0., 1., 4);
    assert(x[0] == 0.);
    assert(equal(x[1], 1. / 3));
    assert(equal(x[2], 2. / 3));
    assert(x[3] == 1.);

    uniform_bnd(x, 1., 0., 5);
    assert(x[0] == 1.);
    assert(equal(x[1], 0.75));
    assert(equal(x[2], 0.5));
    assert(equal(x[3], 0.25));
    assert(x[4] == 0.);

    uniform_bnd(x, -1., 1., 5);
    assert(x[2] == 0.);
    assert(equal(x[3], 0.5));
    assert(x[4] == 1.);
    assert(x[0] = -x[4]);
    assert(x[1] = -x[3]);

    uniform_bnd(x, 1., -1., 5);
    assert(x[2] == 0.);
    assert(equal(x[3], -0.5));
    assert(x[4] == -1.);
    assert(x[0] = -x[4]);
    assert(x[1] = -x[3]);

    uniform_bnd(x, -1., 1., 4);
    assert(equal(x[2], 1. / 3));
    assert(x[3] == 1.);
    assert(x[0] == -x[3]);
    assert(x[1] == -x[2]);

    uniform_bnd(x, 90., -90., 19);
    assert(x[0] == 90.);
    assert(equal(x[1], 80.));
    assert(equal(x[8], 10.));
    assert(x[9] == 0.);
    assert(equal(x[10], -10.));
    assert(x[18] == -90.);
    /* PRINT(x, 19); */
}


int
main(int argc, char **argv)
{
    test1();
    test2();
    return 0;
}
#endif /* TEST_MAIN */
