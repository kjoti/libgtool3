/*
 * gtool3.h -- a header file for libgtool3
 */
#ifndef GTOOL3__H
#define GTOOL3__H

#include <sys/types.h>

/* for uint32_t */
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#  include <inttypes.h>
#else
typedef unsigned uint32_t;
#endif

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GTOOL3 header type
 */
#define GT3_HEADER_SIZE 1024
typedef struct {
    char h[GT3_HEADER_SIZE];
} GT3_HEADER;


/*
 * GTOOL3 format types
 */
#define GT3_FMT_MBIT 8U
#define GT3_FMT_MASK ((1U << GT3_FMT_MBIT) - 1U)

enum {
    GT3_FMT_UR4,
    GT3_FMT_URC,                /* URC version 2 */
    GT3_FMT_URC1,               /* URC version 1 (deprecated) */
    GT3_FMT_UR8,
    GT3_FMT_URX,
    GT3_FMT_MR4,
    GT3_FMT_MR8,
    GT3_FMT_MRX,
    GT3_FMT_URY,
    GT3_FMT_MRY,
    GT3_FMT_NULL
};

/* data type */
enum {
    GT3_TYPE_FLOAT,
    GT3_TYPE_DOUBLE
};


/* for mode in GT3_File */
enum {
    GT3_CONST_CHUNK_SIZE = 1U
};

/*
 * Error codes in libgtool3
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
 * Mask for MR4, MR8, and MRX.
 */
struct GT3_Datamask {
    size_t nelem;               /* # of elements (current) */
    size_t reserved;            /* # of elements (reserved) */

    uint32_t *mask;

    int loaded;                 /* the chunk number mask is loaded */
    int indexed;                /* Index up-to-date?  */
    int *index;
    size_t index_len;           /* length of index */
};
typedef struct GT3_Datamask GT3_Datamask;

/*
 * Gtool-formatted file.
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

    off_t off;                  /* current chunk position */
    off_t size;                 /* file size (in bytes) */

    GT3_Datamask *mask;
};
typedef struct GT3_File GT3_File;


/*
 * Buffer to read data from GT3_File.
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
 * for Date(Time), TimeDuration...
 */
struct GT3_Date {
    /* This struct contains no calendar type. */
    int year;
    int mon;                    /* 1-12 */
    int day;                    /* 1-31 */
    int hour, min, sec;
};
typedef struct GT3_Date GT3_Date;

struct GT3_Duration {
    int value;
    int unit;                   /* GT3_UNIT_XXX */
};
typedef struct GT3_Duration GT3_Duration;

/*
 * Virtually concatenated file.
 */
struct GT3_VCatFile {
    int num_files;              /* N: the number of files */
    char **path;                /* [0] ... [N-1] */
    int *index;                 /* [0] ... [N] */
    int reserved;               /* >= N */

    int opened_;                /* -1, 0 ... N-1 */
    GT3_File *ofile_;
};
typedef struct GT3_VCatFile GT3_VCatFile;


/* XXX calendar type */
enum {
    GT3_CAL_GREGORIAN,
    GT3_CAL_NOLEAP,
    GT3_CAL_ALL_LEAP,
    GT3_CAL_360_DAY,
    GT3_CAL_JULIAN,
    GT3_CAL_DUMMY
};


enum {
    GT3_UNIT_YEAR,
    GT3_UNIT_MON,
    GT3_UNIT_DAY,
    GT3_UNIT_HOUR,
    GT3_UNIT_MIN,
    GT3_UNIT_SEC
};

/*
 * prototype declarations
 */
/* file.c */
int GT3_readHeader(GT3_HEADER *header, GT3_File *fp);
int GT3_isHistfile(GT3_File *fp);
int GT3_format(const char *str);
int GT3_format_string(char *str, int fmt);
int GT3_countChunk(const char *path);
int GT3_getNumChunk(const GT3_File *fp);
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
GT3_Varbuf *GT3_getVarbuf2(GT3_Varbuf *old, GT3_File *fp);
int GT3_readVarZ(GT3_Varbuf *var, int zpos);
int GT3_readVarZY(GT3_Varbuf *var, int zpos, int ypos);
int GT3_readVar(double *rval, GT3_Varbuf *var, int x, int y, int z);
int GT3_copyVarDouble(double *, size_t, const GT3_Varbuf *, int, int);
int GT3_copyVarFloat(float *, size_t, const GT3_Varbuf *, int, int);

char *GT3_getVarAttrStr(char *attr, int len, const GT3_Varbuf *var,
                        const char *key);
int GT3_getVarAttrInt(int *attr, const GT3_Varbuf *var, const char *key);
int GT3_getVarAttrDouble(double *attr, const GT3_Varbuf *var, const char *key);
int GT3_reattachVarbuf(GT3_Varbuf *var, GT3_File *fp);

/* mask.c */
GT3_Datamask *GT3_newMask(void);
void GT3_freeMask(GT3_Datamask *ptr);
int GT3_setMaskSize(GT3_Datamask *ptr, size_t nelem);
int GT3_updateMaskIndex(GT3_Datamask *mask, int interval);
int GT3_getMaskValue(const GT3_Datamask *mask, int i);
int GT3_loadMask(GT3_Datamask *mask, GT3_File *fp);
int GT3_loadMaskX(GT3_Datamask *mask, int zpos, GT3_File *fp);

/* gtdim.c */
GT3_Dim *GT3_loadDim(const char *name);
GT3_Dim *GT3_getBuiltinDim(const char *name);
int GT3_getDimlen(const char *name);
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
int GT3_decodeHeaderTunit(const GT3_HEADER *header);
void GT3_initHeader(GT3_HEADER *header);
void GT3_setHeaderString(GT3_HEADER *header, const char *key, const char *str);
int GT3_setHeaderInt(GT3_HEADER *header, const char *key, int val);
int GT3_setHeaderDouble(GT3_HEADER *header, const char *key, double val);
int GT3_setHeaderDate(GT3_HEADER *header, const char *key, const GT3_Date *date);
void GT3_setHeaderEdit(GT3_HEADER *head, const char *str);
void GT3_setHeaderEttl(GT3_HEADER *head, const char *str);
void GT3_setHeaderMemo(GT3_HEADER *head, const char *str);
void GT3_mergeHeader(GT3_HEADER *dest, const GT3_HEADER *src);
void GT3_copyHeader(GT3_HEADER *dest, const GT3_HEADER *src);
int GT3_getHeaderItemID(const char *name);

/* write.c */
int GT3_output_format(char *dfmt, const char *str);
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

/* timedim.c */
void GT3_setDate(GT3_Date *date, int, int, int, int, int, int);
int GT3_cmpDate(const GT3_Date *date, int, int, int, int, int, int);
int GT3_cmpDate2(const GT3_Date *date1, const GT3_Date *date2);
int GT3_midDate(GT3_Date *, const GT3_Date *, const GT3_Date *, int);
void GT3_copyDate(GT3_Date *dest, const GT3_Date *src);
void GT3_addDuration(GT3_Date *date, const GT3_Duration *dur, int ctype);
double GT3_getTime(const GT3_Date *date, const GT3_Date *since,
                   int tunit, int ctype);
int GT3_guessCalendarHeader(const GT3_HEADER *head);
int GT3_guessCalendarFile(const char *path);

int GT3_calcDuration(GT3_Duration *dur,
                     const GT3_Date *date1, const GT3_Date *date2,
                     int calendar);

int GT3_getDuration(GT3_Duration *dur, GT3_File *fp, int calendar);
int GT3_checkDate(const GT3_Date *date, int calendar);
const char *GT3_calendar_name(int calendar);

/* vcat.c */
GT3_VCatFile *GT3_newVCatFile(void);
int GT3_vcatFile(GT3_VCatFile *vf, const char *path);
void GT3_destroyVCatFile(GT3_VCatFile *vf);
GT3_Varbuf *GT3_setVarbuf_VF(GT3_Varbuf *var, GT3_VCatFile *vf, int tpos);
int GT3_readHeader_VF(GT3_HEADER *header, GT3_VCatFile *vf, int tpos);
int GT3_numChunk_VF(const GT3_VCatFile *vf);
int GT3_glob_VF(GT3_VCatFile *vf, const char *pattern);

/* version.c */
char *GT3_version(void);

#ifdef __cplusplus
}
#endif

#endif
