/*
 *  gtool3.h -- a header file for libgtool3
 *
 *  $Date: 2006/11/07 00:53:11 $
 */

#ifndef GTOOL3__H
#define GTOOL3__H

#include <sys/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  GTOOL3 header type
 */
#define GT3_HEADER_SIZE 1024
typedef struct {
    char h[GT3_HEADER_SIZE];
} GT3_HEADER;


/*
 *  GTOOL3 format types
 */
enum {
    GT3_FMT_UR4,
    GT3_FMT_URC,                    /* URC version 2 */
    GT3_FMT_URC1,                   /* URC version 1 (deprecated) */
    GT3_FMT_UR8
};

/* data type */
enum {
    GT3_TYPE_FLOAT,
    GT3_TYPE_DOUBLE
};

/*
 *  Error codes in libgtool3
 */
enum {
    GT3_ERR_NONE = 0,           /* no error */
    GT3_ERR_SYS,                /* system error */

    GT3_ERR_BROKEN,             /* Broken file */
    GT3_ERR_CALL,               /* illegal API call */
    GT3_ERR_FILE,               /* Not a gtool file */
    GT3_ERR_HEADER,             /* invalid data in the header  */
    GT3_ERR_INDEX,              /* index out of range */

    GT3_ERR_UNDEF               /* undefined error (sentinel) */
};


struct GT3_Dim {
    char *name;
    double *values;
    int len;
    double range[2];            /* lower bound and upper bound */
    int cyclic;                 /* 0: not cyclic, 1: cyclic */
    char *title, *unit;
};
typedef struct GT3_Dim GT3_Dim;

struct GT3_DimBound {
    char *name;
    int len;
    double *bnd;
    int len_orig;
};
typedef struct GT3_DimBound GT3_DimBound;

/*
 *  Gtool-formatted file.
 */
struct GT3_File {
    char *path;                 /* pathname */
    FILE *fp;

    unsigned mode;

    /* "curr == num_chunk" means that this file has reached EOF. */
    int curr;                   /* current chunk No. (0...num_chunk) */
    int fmt;                    /* format of current chunk */

    /* chsize varies if this is not a history-file. */
    size_t chsize;              /* (current) chunk-size (in bytes) */
    int dimlen[3];              /* (current) dimension-length */

    /* XXX: num_chunk is not always available. */
    int num_chunk;              /* # of chunks in a file */

    off_t off;                  /* offset of current chunk */
    off_t size;                 /* file size (in bytes) */
};
typedef struct GT3_File GT3_File;


/*
 *  Buffer to read data from GT3_File.
 */
struct GT3_Varbuf {
    GT3_File *fp;               /* associated gtool3 file */
    int type;                   /* data type (float or double) */
    void *data;                 /* data buffer */
    size_t bufsize;             /* buffer size (in bytes) */

    int dimlen[3];              /* dimension size */
    double miss;                /* missing value */

    void *stat_;                /* internal status */
};
typedef struct GT3_Varbuf GT3_Varbuf;

/*
 *  for Date(Time), TimeDuration...
 */
struct GT3_Date {
    /* This struct contains no calendar type. */
    int year;
    int mon;                    /* 1-12 */
    int day;                    /* 1-31 */
    int hour, min, sec;
};
typedef struct GT3_Date GT3_Date;

/* XXX calendar type */
enum {
    GT3_CAL_GREGORIAN,
    GT3_CAL_NOLEAP,
    GT3_CAL_ALL_LEAP,
    GT3_CAL_360_DAY,
    GT3_CAL_JULIAN,
    GT3_CAL_DUMMY
};


/*
 *  prototype declarations
 */
/* file.c */
int GT3_readHeader(GT3_HEADER *header, GT3_File *fp);
int GT3_isHistfile(GT3_File *fp);
int GT3_format(const char *str);
int GT3_countChunk(const char *path);
GT3_File *GT3_open(const char *path);
GT3_File *GT3_openHistFile(const char *path);
GT3_File *GT3_openRW(const char *path);
int GT3_eof(GT3_File *fp);
int GT3_next(GT3_File *fp);
void GT3_close(GT3_File *fp);
int GT3_rewind(GT3_File *fp);
int GT3_seek(GT3_File *fp, int dest, int whence);
int GT3_skipZ(GT3_File *fp, int z);

/* varbuf.c */
void GT3_freeVarbuf(GT3_Varbuf *var);
GT3_Varbuf *GT3_getVarbuf(GT3_File *fp);
int GT3_readVarZ(GT3_Varbuf *var, int zpos);
int GT3_readVarZY(GT3_Varbuf *var, int zpos, int ypos);
int GT3_readVar(double *rval, GT3_Varbuf *var, int x, int y, int z);
int GT3_copyVarDouble(double *, size_t, const GT3_Varbuf *, int, int);
int GT3_copyVarFloat(float *, size_t, const GT3_Varbuf *, int, int);

char *GT3_getVarAttrStr(char *attr, int len, const GT3_Varbuf *var,
                        const char *key);
int GT3_getVarAttrInt(int *attr, const GT3_Varbuf *var, const char *key);
int GT3_getVarAttrDouble(double *attr, const GT3_Varbuf *var, const char *key);

/* gtdim.c */
GT3_Dim *GT3_loadDim(const char *name);
GT3_Dim *GT3_getBuiltinDim(const char *name);
GT3_Dim *GT3_getDim(const char *name);
void GT3_freeDim(GT3_Dim *dim);
double *GT3_loadDimWeight(const char *name);
double *GT3_getBuiltinDimWeight(const char *name);
double *GT3_getDimWeight(const char *name);
int GT3_writeDimFile(FILE *fp, const GT3_Dim *dim, const char *fmt);
int GT3_writeWeightFile(FILE *fp, const GT3_Dim *dim, const char *fmt);
GT3_DimBound *GT3_getDimBound(const char *name);
void GT3_freeDimBound(GT3_DimBound *dimbnd);

/* header.c */
char *GT3_copyHeaderItem(char *buf, int len, const GT3_HEADER *h,
                         const char *key);
int GT3_decodeHeaderInt(int *rval, const GT3_HEADER *h, const char *key);
int GT3_decodeHeaderDouble(double *rval, const GT3_HEADER *h, const char *key);
int GT3_decodeHeaderDate(GT3_Date *date, const GT3_HEADER *header, const char *key);
void GT3_initHeader(GT3_HEADER *header);
void GT3_setHeaderString(GT3_HEADER *header, const char *key, const char *str);
int GT3_setHeaderInt(GT3_HEADER *header, const char *key, int val);
int GT3_setHeaderDouble(GT3_HEADER *header, const char *key, double val);
int GT3_setHeaderDate(GT3_HEADER *header, const char *key, const GT3_Date *date);
void GT3_mergeHeader(GT3_HEADER *dest, const GT3_HEADER *src);
void GT3_copyHeader(GT3_HEADER *dest, const GT3_HEADER *src);
int GT3_getHeaderItemID(const char *name);

/* write.c */
int GT3_write(const void *ptr, int type,
              int nx, int ny, int nz,
              const GT3_HEADER *headin, const char *dfmt, FILE *fp);

/* error.c */
void GT3_clearLastError(void);
void GT3_printLastErrorMessage(FILE *output);
void GT3_printErrorMessages(FILE *output);
int GT3_ErrorCount(void);
int GT3_getLastError(void);
int GT3_copyLastErrorMessage(char *buf, size_t buflen);
void GT3_setExitOnError(int onoff);
void GT3_setPrintOnError(int onoff);
void GT3_setProgname(const char *name);

/* timedim */
void GT3_setDate(GT3_Date *date, int, int, int, int, int, int);
int GT3_cmpDate(const GT3_Date *date, int, int, int, int, int, int);
int GT3_cmpDate2(const GT3_Date *date1, const GT3_Date *date2);
int GT3_diffDate(GT3_Date *diff, 
                 const GT3_Date *from, const GT3_Date *to, int ctype);
int GT3_guessCalendarFile(const char *path);

/* version */
char *GT3_version(void);

#ifdef __cplusplus
}
#endif

#endif
