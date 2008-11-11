/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  scaling.c
 */
#include <sys/types.h>
#include <math.h>
#include "myutils.h"


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
	float val = HUGE_VALF;

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
	float val = -HUGE_VALF;

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


#ifdef TEST_MAIN
#include <assert.h>
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


int
main(int argc, char **argv)
{
	float orig[NN], restore[NN];
	unsigned scaled[NN];
	int i;
	unsigned imiss;
	double offset, scale, miss;


	imiss = (1U << 31) - 1U;

	offset = 0.;
	scale  = (imiss > 1) ? 1. / (imiss - 1) : 0.;
	miss = -999.0;

	srand(2);
	for (i = 0; i < NN; i++)
		orig[i] = (float)rand() / RAND_MAX;

	scalingf(scaled, orig, NN, offset, scale, imiss, miss);
	scalingf_rev(restore, scaled, NN, offset, scale, imiss, miss);

	printf("ave(orig):    %.16f\n", average(orig, NN));
	printf("ave(restore): %.16f\n", average(restore, NN));

	printf("RMS           %.10e\n", rms(orig, restore, NN));

	return 0;
}
#endif
