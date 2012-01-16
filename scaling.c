/*
 * scaling.c
 */
#include <sys/types.h>

#include <assert.h>
#include <math.h>
#include "myutils.h"

#ifndef min
#  define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define SCALE_MAX 1.7e308
#define SCALE_MIN 2.3e-308


size_t
masked_scalingf(unsigned *dest,
                const float *src,
                size_t nelem,
                double offset, double scale,
                unsigned imiss, double miss)
{
    int i;
    double v;
    double iscale;
    size_t cnt;

    iscale = (scale == 0.) ? 0. : 1. / scale;
    for (cnt = 0, i = 0; i < nelem; i++)
        if (src[i] != miss) {
            v = ((src[i] - offset) * iscale + 0.5);

            if (v < 0.)
                dest[cnt] = 0;
            else if (v > (double)(imiss - 1))
                dest[cnt] = imiss - 1;
            else
                dest[cnt] = (unsigned)v;

            cnt++;
        }
    return cnt;
}


size_t
masked_scaling(unsigned *dest,
               const double *src,
               size_t nelem,
               double offset, double scale,
               unsigned imiss, double miss)
{
    int i;
    double v;
    double iscale;
    size_t cnt;

    iscale = (scale == 0.) ? 0. : 1. / scale;
    for (cnt = 0, i = 0; i < nelem; i++)
        if (src[i] != miss) {
            v = ((src[i] - offset) * iscale + 0.5);

            if (v < 0.)
                dest[cnt] = 0;
            else if (v > (double)(imiss - 1))
                dest[cnt] = imiss - 1;
            else
                dest[cnt] = (unsigned)v;

            cnt++;
        }
    return cnt;
}


void
scaling(unsigned *dest,
        const double *src,
        size_t nelem,
        double offset, double scale,
        unsigned imiss, double miss)
{
    int i;
    double v;
    double iscale;

    iscale = (scale == 0.) ? 0. : 1. / scale;
    for (i = 0; i < nelem; i++)
        if (src[i] != miss) {
            v = ((src[i] - offset) * iscale + 0.5);

            if (v < 0.)
                dest[i] = 0;
            else if (v > (double)(imiss - 1))
                dest[i] = imiss - 1;
            else
                dest[i] = (unsigned)v;
        } else
            dest[i] = imiss;
}


void
scalingf(unsigned *dest,
         const float *src,
         size_t nelem,
         double offset, double scale,
         unsigned imiss, double miss)
{
    int i;
    double v;
    double iscale;
    float missf = (float)miss;

    iscale = (scale == 0.) ? 0. : 1. / scale;
    for (i = 0; i < nelem; i++)
        if (src[i] != missf) {
            v = ((src[i] - offset) * iscale + 0.5);

            if (v < 0.)
                dest[i] = 0;
            else if (v > (double)(imiss - 1))
                dest[i] = imiss - 1;
            else
                dest[i] = (unsigned)v;
        } else
            dest[i] = imiss;
}


double
step_size(double minv, double maxv, unsigned num)
{
    double dx0, step;

    assert(num >= 1);
    dx0 = 1. / num;
    step = maxv * dx0 - minv * dx0;
    step = max(step, SCALE_MIN);
    step = min(step, SCALE_MAX * dx0);
    return step;
}


/*
 * offset & scale.
 */
void
scaling_parameters(double *dma, double dmin, double dmax, int num)
{
    double amin, amax, xi;
    int i0;

    if (dmin >= 0. || dmax < 0.)
        goto default_param;

    if (dmax == 0.) {
        dma[0] = dmax;
        dma[1] = -step_size(dmin, dmax, num);
        return;
    }

    amin = fabs(dmin);
    amax = fabs(dmax);

    /* XXX: to avoid overflow */
    xi = (amin < amax) ? amin / amax : amax / amin;
    if (xi < 1e-10)
        goto default_param;

    i0 = (int)(num / (1. + amax / amin));
    if (i0 == 0 || i0 == num)
        goto default_param;

    dma[1] = amin / i0;
#if 0
    {
        /*
         * XXX: To pass test3() in certain environments.
         */
        float dx;

        dx = (float)(amin / i0);
        if (dx < 1e-38f || dx > 1e38f)
            dma[1] = amin / i0;
        else
            dma[1] = dx;
    }
#endif
    dma[0] = -dma[1]  * i0;
    return;

default_param:
    dma[0] = dmin;
    dma[1] = step_size(dmin, dmax, num);
}



#ifdef TEST_MAIN
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#define NN 128


void
scalingf_rev(float *dest, const unsigned *src,
             size_t nelem,
             double offset, double scale,
             unsigned imiss, double miss)
{
    float missf = (float)miss;
    int i;

    for (i = 0; i < nelem; i++)
        dest[i] = (src[i] != imiss)
            ? offset + src[i] * scale
            : missf;
}


void
test1(unsigned nbits)
{
    float orig[NN], restore[NN];
    unsigned scaled[NN], scaled2[NN];
    int i;
    unsigned imiss;
    double offset, scale, miss;

    imiss = (1U << nbits) - 1U;

    offset = 0.;
    scale  = (imiss > 1) ? 1. / (imiss - 1) : 1.;
    miss = -999.0;

    srand(2);
    for (i = 0; i < NN; i++)
        orig[i] = (float)rand() / RAND_MAX;
    orig[NN-1] = miss;

    scalingf(scaled, orig, NN, offset, scale, imiss, miss);
    scalingf_rev(restore, scaled, NN, offset, scale, imiss, miss);
    scalingf(scaled2, restore, NN, offset, scale, imiss, miss);

    for (i = 0; i < NN; i++) {
        if (fabs(restore[i] - orig[i]) > 0.5 * scale) {
            printf("N=%d %.10f %.10f\n", nbits, orig[i], restore[i]);
            assert(!"scaling error");
        }
        if (scaled[i] != scaled2[i]) {
            printf("N=%d %d %d\n", nbits, scaled[i], scaled2[i]);
            assert(!"scaling error");
        }
    }
}


void
test2(unsigned nbits)
{
    double dmin;
    double dmax;
    double dma[2];
    double f = 1. + 1e-7;
    int num;

    num = (1U << nbits) - 2;
    if (num < 1)
        num = 1;

    dmin = -1.;
    dmax = 1.;
    scaling_parameters(dma, dmin, dmax, num);
    assert(dma[0] <= dmin / f);
    assert(dma[0] + f * num * dma[1] >= dmax);

    dmin = -1.;
    dmax = 10.;
    scaling_parameters(dma, dmin, dmax, num);
    assert(dma[0] <= dmin / f);
    assert(dma[0] + f * num * dma[1] >= dmax);

    dmin = -1.;
    dmax = 1000.;
    scaling_parameters(dma, dmin, dmax, num);
    assert(dma[0] <= dmin / f);
    assert(dma[0] + f * num * dma[1] >= dmax);

    dmin = -1e-100;
    dmax = 4e-50;
    scaling_parameters(dma, dmin, dmax, num);
    assert(dma[0] <= dmin / f);
    assert(dma[0] + f * num * dma[1] >= dmax);

    dmin = -1e-50;
    dmax = 4e-100;
    scaling_parameters(dma, dmin, dmax, num);
    assert(dma[0] <= dmin / f);
    assert(dma[0] + f * num * dma[1] >= dmax);

/*  dmin = -1e-13; */
/*  dmax = 1.; */
/*  scaling_parameters(dma, dmin, dmax, num); */
/*  assert(dma[0] == -1e-13); */

/*  dmin = 1e-13; */
/*  dmax = 1.; */
/*  scaling_parameters(dma, dmin, dmax, num); */
/*  assert(dma[0] == 1e-13); */
}


int
main(int argc, char **argv)
{
    unsigned n;

    for (n = 2; n < 32; n++)
        test1(n);

    for (n = 1; n < 32; n++)
        test2(n);
    return 0;
}
#endif
