/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  internal.h -- a header file for using libgtool3
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#ifndef GT3_INTERNAL__H
#define GT3_INTERNAL__H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_INTTYPES_H
#  include <inttypes.h>			/* for uint32_t */
#endif

#ifndef HAVE_UINT32_T
typedef unsigned uint32_t;
#endif

#include "gtool3.h"

#ifndef DEFAULT_GTAXDIR
#  define DEFAULT_GTAXDIR "/usr/local/share/gtool/gt3"
#endif

#ifndef IO_BUF_SIZE
#  define IO_BUF_SIZE  (16*1024)
#endif

/*
 *  WORDS_BIGENDIAN might be defined by 'autoconf'.
 */
#ifdef WORDS_BIGENDIAN
#  define IS_LITTLE_ENDIAN 0
#else
#  define IS_LITTLE_ENDIAN 1
#endif

#ifndef HAVE_FSEEKO
#  define fseeko fseek
#endif

/*
 *  FTN_HEAD:
 *  A type for the header/trailer of Fortran unformatted data.
 *  sizeof(FTN_HEAD) must be 4.
 */
typedef uint32_t FTN_HEAD;

/*
 *  urc_pack.c
 */
void calc_urc_param(const float *data, int len, double miss,
					double *prmin, double *pfac_e, double *pfac_d,
					int *pne, int *pnd);
void urc1_packing(uint32_t *packed,
				  const float *data, int len, double miss,
				  double rmin, double fac_e, double fac_d);
void urc2_packing(uint32_t *packed,
				  const float *data, int len, double miss,
				  double rmin, double fac_e, double fac_d);
void urc1_unpack(const uint32_t *packed, int packed_len,
				 double ref, int ne, int nd,
				 double miss, float *data);
void urc2_unpack(const uint32_t *packed, int packed_len,
				 double ref, int ne, int nd,
				 double miss, float *data);

/* reverse.c */
void *reverse_words(void *vptr, int nwords);
void *reverse_dwords(void *vptr, int nwords);

/* grid.c */
int uniform_center(double *grid, double x0, double x1, int len);
int uniform_bnd(double *grid, double x0, double x1, int len);

/* nr-gauleg.c */
void gauleg(double x1, double x2, double x[], double w[], int n);

/*
 * error.c
 */
#define SYSERR GT3_ERR_SYS
void gt3_error(int code, const char *fmt, ...);

#endif /* GT3_INTERNAL__H */
