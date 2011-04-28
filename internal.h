/*
 * internal.h -- a header file for using libgtool3
 */
#ifndef GT3_INTERNAL__H
#define GT3_INTERNAL__H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/stat.h>
#include "gtool3.h"

#ifndef DEFAULT_GTAXDIR
#  define DEFAULT_GTAXDIR "/usr/local/share/gtool/gt3"
#endif

#ifndef IO_BUF_SIZE
#  define IO_BUF_SIZE  (64*1024)
#endif

/*
 * WORDS_BIGENDIAN might be defined by 'autoconf'.
 */
#ifdef WORDS_BIGENDIAN
#  define IS_LITTLE_ENDIAN 0
#else
#  define IS_LITTLE_ENDIAN 1
#endif

/*
 * fseeko and ftello
 */
#ifdef __MINGW32__
#  define fseeko fseeko64
#  define ftello ftello64
#else /* !__MINGW32__ */
#  ifndef HAVE_FSEEKO
#    define fseeko fseek
#    define ftello ftell
#  endif
#endif /* !__MINGW32__ */

/*
 * wrapper for stat(2).
 */
#ifdef __MINGW32__
typedef struct _stati64 file_stat_t;
#define file_stat(path, sb) _stati64(path, sb)
#else
typedef struct stat file_stat_t;
#define file_stat(path, sb) stat(path, sb)
#endif

/*
 * fort_size_t:
 * A type for the header/trailer of Fortran unformatted data.
 * sizeof(fort_size_t) must be 4.
 */
typedef uint32_t fort_size_t;

/*
 * Macros for performance.
 * These are identical to functions defined in mask.c.
 */
#define getbit_m(m,i) (((m)[(i) >> 5U] >> (31U - ((i) & 0x1fU))) & 1U)
#define getMaskValue(m, i) getbit_m((m)->mask, i)

/*
 * urc_pack.c
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

/* gauss-legendre.c */
void gauss_legendre(double sol[], double wght[], int nth);

/* error.c */
#define SYSERR GT3_ERR_SYS
void gt3_error(int code, const char *fmt, ...);

/* scaling.c */
void scaling(unsigned *dest,
             const double *src,
             size_t nelem,
             double offset, double scale,
             unsigned imax, double miss);

void scalingf(unsigned *dest,
              const float *src,
              size_t nelem,
              double offset, double scale,
              unsigned imax, double miss);

size_t masked_scaling(unsigned *dest,
                      const double *src,
                      size_t nelem,
                      double offset, double scale,
                      unsigned imiss, double miss);

size_t masked_scalingf(unsigned *dest,
                       const float *src,
                       size_t nelem,
                       double offset, double scale,
                       unsigned imiss, double miss);

double step_size(double minv, double maxv, int nbits);
void scaling_parameters(double *dma, double dmin, double dmax, int num);

/* record.c */
int read_words_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp);
int read_dwords_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp);
int write_record_sep(fort_size_t size, FILE *fp);
int write_words_into_record(const void *ptr, size_t nelem, FILE *fp);
int write_dwords_into_record(const void *ptr, size_t nelem, FILE *fp);
int write_bytes_into_record(const void *ptr, size_t nelem, FILE *fp);

/* xfread.c */
int xfread(void *ptr, size_t size, size_t nmemb, FILE *fp);

/* read_urc */
int read_URC1(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);
int read_URC2(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);

/* read_urx.c */
int read_URX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);
int read_MRX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);

/* read_ury.c */
int read_URY(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);
int read_MRY(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp);

#include "find_minmax.h"

#endif /* GT3_INTERNAL__H */
