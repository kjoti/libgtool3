/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  scaling.c
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


int
idx_min_double(const double *data, size_t nelem, const double *maskval)
{
	int i, idx;
	double val = HUGE_VAL;

	idx = -1;
	if (maskval) {
		for (i = 0; i < nelem; i++)
			if (data[i] != *maskval && data[i] < val) {
				val = data[i];
				idx = i;
			}
	} else {
		for (i = 0; i < nelem; i++)
			if (data[i] < val) {
				val = data[i];
				idx = i;
			}
	}
	return idx;
}


int
idx_max_double(const double *data, size_t nelem, const double *maskval)
{
	int i, idx;
	double val = -HUGE_VAL;

	idx = -1;
	if (maskval) {
		for (i = 0; i < nelem; i++)
			if (data[i] != *maskval && data[i] > val) {
				val = data[i];
				idx = i;
			}
	} else {
		for (i = 0; i < nelem; i++)
			if (data[i] > val) {
				val = data[i];
				idx = i;
			}
	}
	return idx;
}


int
idx_min_float(const float *data, size_t nelem, const float *maskval)
{
	int i, idx;
	float val = HUGE_VAL;

	idx = -1;
	if (maskval) {
		for (i = 0; i < nelem; i++)
			if (data[i] != *maskval && data[i] < val) {
				val = data[i];
				idx = i;
			}
	} else {
		for (i = 0; i < nelem; i++)
			if (data[i] < val) {
				val = data[i];
				idx = i;
			}
	}
	return idx;
}


int
idx_max_float(const float *data, size_t nelem, const float *maskval)
{
	int i, idx;
	float val = -HUGE_VAL;

	idx = -1;
	if (maskval) {
		for (i = 0; i < nelem; i++)
			if (data[i] != *maskval && data[i] > val) {
				val = data[i];
				idx = i;
			}
	} else {
		for (i = 0; i < nelem; i++)
			if (data[i] > val) {
				val = data[i];
				idx = i;
			}
	}
	return idx;
}


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
step_size(double minv, double maxv, int num)
{
	double dx0, step;

	assert(num >= 1);
	dx0 = 1. / num;
	step = maxv * dx0 - minv * dx0;
	step = max(step, SCALE_MIN);
	step = min(step, SCALE_MAX * dx0);
	return step;
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


double
average(const float *data, size_t nelem)
{
	double sum;
	int i;

	for (sum = 0, i = 0; i < nelem; i++)
		sum += data[i];

	return sum / (nelem != 0 ? nelem : 1);
}


double
rms(const float *a, const float *b, size_t nelem)
{
	double d;
	double r = 0.;
	int i;

	for (i = 0; i < nelem; i++) {
		d = a[i] - b[i];
		r += d * d;
	}
	return sqrt(r) / (nelem != 0 ? nelem : 1);
}


void
test(void)
{
	float orig[NN], restore[NN];
	unsigned scaled[NN];
	int i;
	unsigned imiss;
	double offset, scale, miss;
	double rmsval;
	unsigned nbits = 16;


	imiss = (1U << nbits) - 1U;

	offset = 0.;
	scale  = (imiss > 1) ? 1. / (imiss - 1) : 0.;
	miss = -999.0;

	srand(2);
	for (i = 0; i < NN; i++)
		orig[i] = (float)rand() / RAND_MAX;

	scalingf(scaled, orig, NN, offset, scale, imiss, miss);
	scalingf_rev(restore, scaled, NN, offset, scale, imiss, miss);

	rmsval = rms(orig, restore, NN);
	printf("RMS(nbits = %u): %.10e\n", nbits, rmsval);
	assert(rmsval < 8e-6);
}


void
test2(void)
{
	int n, num = 100;
	double step;

	for (n = 2; n < 32; n++) {
		num = (1U << n) - 2;
		step = step_size(0., DBL_MIN, num);
		printf("%30.15e %30.15e\n", step, num * step);
	}
}


int
main(int argc, char **argv)
{
	test2();
	return 0;
}
#endif
