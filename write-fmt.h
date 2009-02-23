#ifndef GT3_WRITE_FMT__H
#define GT3_WRITE_FMT__H

#include <sys/types.h>
#include <stdio.h>


/* write-urx.c */
int write_urx_via_double(const void *ptr,
						 size_t zelem, size_t nz,
						 int nbits, double miss, FILE *fp);
int write_urx_via_float(const void *ptr,
						size_t zelem, size_t nz,
						int nbits, double miss, FILE *fp);

int write_mrx_via_double(const void *ptr,
						 size_t zelems, size_t nz,
						 int nbits, double miss,
						 FILE *fp);
int write_mrx_via_float(const void *ptr,
						size_t zelems, size_t nz,
						int nbits, double miss,
						FILE *fp);

/* write-mask.c */
unsigned masked_count(const void *ptr, size_t size,
					  size_t nelems, double miss);
int write_mask(const void *ptr2,
			   size_t size,
			   size_t nelems, size_t nsets,
			   double miss, FILE *fp);

int write_mr4_via_double(const double *data, size_t nelems,
						 double miss, FILE *fp);

int write_mr4_via_float(const float *data, size_t nelems,
						double miss, FILE *fp);

int write_mr8_via_double(const double *data, size_t nelems,
						 double miss, FILE *fp);

int write_mr8_via_float(const float *data, size_t nelems,
						double miss, FILE *fp);

/* write-ury.c */
int write_ury_via_double(const void *ptr,
						 size_t zelem, size_t nz,
						 int nbits, double miss, FILE *fp);
int write_ury_via_float(const void *ptr,
						size_t zelem, size_t nz,
						int nbits, double miss, FILE *fp);

int write_mry_via_double(const void *ptr,
						 size_t zelems, size_t nz,
						 int nbits, double miss,
						 FILE *fp);
int write_mry_via_float(const void *ptr,
						size_t zelems, size_t nz,
						int nbits, double miss,
						FILE *fp);

#endif
