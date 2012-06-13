/*
 * if_fortran.c -- provide an interface to the Fortran language.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"

#ifndef OPEN_MAX
#  define OPEN_MAX 256
#endif

#ifndef INT_MAX
#  define INT_MAX 0x7fffffff
#endif
#ifndef INT_MIN
#  define INT_MIN (-0x7fffffff - 1)
#endif

#ifndef min
#  define min(a,b) ((a)>(b) ? (b) : (a))
#endif
#define min3(a,b,c) min(min((a), (b)), (c))

#define MAX_NOUTPUTS OPEN_MAX
#define MAX_NINPUTS OPEN_MAX

/* naming convention. */
#define NAME(x)  gt3f_ ## x ## _

/*
 * input/output streams.
 */
static FILE *outputs[MAX_NOUTPUTS];
static GT3_Varbuf *varbuf[MAX_NINPUTS];

/*
 * basetime: the origin of the time axis.
 */
static GT3_Date basetime = {0, 1, 1, 0, 0, 0};

static int stop_flag = 0;

/*
 * convert from Fortran string to C string.
 */
static char *
copy_f2c(char *dest, int destlen, const char *src, int srclen)
{
    int len, maxlen;

    assert(destlen > 0 && srclen >= 0);
    maxlen = min(destlen - 1, srclen);
    for (len = maxlen; len > 0; len--)
        if (!isspace(src[len - 1]))
            break;

    memcpy(dest, src, len);
    dest[len] = '\0';
    return dest;
}


/*
 * convert from C string to Fortran string.
 */
static void
copy_c2f(char *dest, int destlen, const char *src)
{
    while (destlen > 0 && *src != '\0') {
        *dest++ = *src++;
        destlen--;
    }
    if (destlen > 0)
        memset(dest, ' ', destlen);
}


/*
 * terminate program if an error has occurred.
 */
static void
exit_on_error(int code)
{
    if (code < 0 && stop_flag != 0) {
        GT3_printErrorMessages(stderr);
        fprintf(stderr, "**** STOP in if_fortran.\n");
        exit(1);
    }
}


/*
 * set stop flag.
 *
 * stop_flag values:
 *   0: No stop even if an error.
 *   non-zero: stop if an error.
 */
void
NAME(stop_on_error)(const int *flag)
{
    stop_flag = *flag;
}


/*
 * print error messages into stderr.
 */
void
NAME(print_error)(void)
{
    GT3_printErrorMessages(stderr);
}


/*
 * open an output stream.
 *
 * In Fortran:
 *   call gt3f_open_output(iu, 'path_to_output')
 */
void
NAME(open_output)(int *iu, const char *path, int pathlen)
{
    char path_[PATH_MAX + 1];
    FILE *fp;
    int i;

    copy_f2c(path_, sizeof(path_), path, pathlen);

    *iu = -1;
    for (i = 0; i < MAX_NOUTPUTS; i++) {
        if (outputs[i] == NULL) {
            if ((fp = fopen(path_, "wb")) == NULL)
                gt3_error(SYSERR, path_);
            else {
                outputs[i] = fp;
                *iu = i;
            }
            break;
        }
    }
    if (i == MAX_NOUTPUTS)
        gt3_error(GT3_ERR_CALL, "gt3f_open_output: Too many outputs");
    exit_on_error(*iu);
}


/*
 * close an output stream.
 */
void
NAME(close_output)(const int *iu, int *status)
{
    *status = 0;
    if (*iu >= 0 && *iu < MAX_NOUTPUTS && outputs[*iu]) {
        if (fclose(outputs[*iu]) != 0) {
            gt3_error(SYSERR, NULL);
            *status = -1;
        }
        outputs[*iu] = NULL;
    }
}


/*
 * close all output streams.
 */
void
NAME(close_output_all)(int *status)
{
    int i;

    *status = 0;
    for (i = 0; i < MAX_NOUTPUTS; i++) {
        if (outputs[i] && fclose(outputs[i]) != 0) {
            gt3_error(SYSERR, NULL);
            *status = -1;
        }
        outputs[i] = NULL;
    }
}


/*
 * Note: There is no way to check whether 'head' has enough rooom.
 */
void
NAME(init_header)(char *head, int dummy)
{
    GT3_initHeader((GT3_HEADER *)head);
}


/*
 * set an item value in a header.
 */
void
NAME(set_item)(char *head,
               const char *key, const char *value,
               int dummy, int keylen, int vlen)
{
    char key_[17], value_[33];

    copy_f2c(key_, sizeof(key_), key, keylen);
    copy_f2c(value_, sizeof(value_), value, vlen);
    GT3_setHeaderString((GT3_HEADER *)head, key_, value_);
}


void
NAME(set_item_int)(char *head,
                   const char *key, const int *value,
                   int dummy, int keylen)
{
    char key_[17];

    copy_f2c(key_, sizeof(key_), key, keylen);
    GT3_setHeaderInt((GT3_HEADER *)head, key_, *value);
}


void
NAME(set_item_double)(char *head,
                      const char *key, const double *value,
                      int dummy, int keylen)
{
    char key_[17];

    copy_f2c(key_, sizeof(key_), key, keylen);
    GT3_setHeaderDouble((GT3_HEADER *)head, key_, *value);
}


void
NAME(set_item_date)(char *head,
                    const char *key,
                    const int *year, const int *mon, const int *day,
                    const int *hh, const int *mm, const int *ss,
                    int dummy, int keylen)
{
    char key_[17];
    GT3_Date date;

    copy_f2c(key_, sizeof(key_), key, keylen);
    date.year = *year;
    date.mon = *mon;
    date.day = *day;
    date.hour = *hh;
    date.min = *mm;
    date.sec = *ss;
    GT3_setHeaderDate((GT3_HEADER *)head, key_, &date);
}


/*
 * get an item(string) value from a header.
 */
void
NAME(get_item)(char *value,
               const char *head, const char *key,
               int *status,
               int vlen, int dummy, int keylen)
{
    char key_[17], item[33];

    copy_f2c(key_, sizeof(key_), key, keylen);
    if (GT3_copyHeaderItem(item, sizeof(item),
                           (const GT3_HEADER *)head, key_) == NULL) {
        *status = -1;
        exit_on_error(*status);
    }
    copy_c2f(value, vlen, item);
    *status = 0;
}


/*
 * get an item(int) value from a header.
 */
void
NAME(get_item_int)(int *value,
                   const char *head, const char *key,
                   int *status,
                   int dummy, int keylen)
{
    char key_[17];
    int rval;

    copy_f2c(key_, sizeof(key_), key, keylen);

    *status = GT3_decodeHeaderInt(&rval, (const GT3_HEADER *)head, key_);

    if (*status == 0)
        *value = rval;
    exit_on_error(*status);
}


/*
 * get an item(double) value from a header.
 */
void
NAME(get_item_double)(double *value,
                      const char *head, const char *key,
                      int *status,
                      int dummy, int keylen)
{
    char key_[17];
    double rval;

    copy_f2c(key_, sizeof(key_), key, keylen);

    *status = GT3_decodeHeaderDouble(&rval, (const GT3_HEADER *)head, key_);

    if (*status == 0)
        *value = rval;
    exit_on_error(*status);
}


/*
 * get an item(date) value from a header.
 */
void
NAME(get_item_date)(int *values,
                    const char *head, const char *key,
                    int *status,
                    int dummy, int keylen)
{
    char key_[17];
    GT3_Date date;

    copy_f2c(key_, sizeof(key_), key, keylen);

    *status = GT3_decodeHeaderDate(&date, (const GT3_HEADER *)head, key_);
    if (*status == 0) {
        values[0] = date.year;
        values[1] = date.mon;
        values[2] = date.day;
        values[3] = date.hour;
        values[4] = date.min;
        values[5] = date.sec;
    }
    exit_on_error(*status);
}


/*
 * write a gtool3 data into an output stream, opened via open_output_stream.
 */
void
NAME(write)(const int *iu,
            const void *ptr, const int *type,
            const int *nx, const int *ny, const int *nz,
            const char *head, const char *dfmt,
            int *status,
            int dummy, int dfmtlen)
{
    char fmt[17];

    *status = -1;
    if (*iu < 0 || *iu >= MAX_NOUTPUTS || outputs[*iu] == NULL)
        gt3_error(GT3_ERR_CALL, "gt3f_write: Invalid output(%d)", *iu);
    else {
        copy_f2c(fmt, sizeof(fmt), dfmt, dfmtlen);
        *status = GT3_write(ptr, *type, *nx, *ny, *nz,
                            (const GT3_HEADER *)head, fmt, outputs[*iu]);
    }
    exit_on_error(*status);
}


static int
invalid_input(int iu)
{
    return iu < 0 || iu >= MAX_NINPUTS || varbuf[iu] == NULL;
}


static int
get_varbuf(GT3_File *fp)
{
    int i;

    for (i = 0; i < MAX_NINPUTS; i++)
        if (varbuf[i] == NULL)
            break;

    if (i == MAX_NINPUTS) {
        gt3_error(GT3_ERR_CALL, "if_fortran: Too many inputs");
        return -1;
    }

    if ((varbuf[i] = GT3_getVarbuf(fp)) == NULL)
        return -1;

    return i;
}


/*
 * Open input for GTOOL3.
 *
 * [OUTPUT]
 *    iu: ID of input (-1 if error).
 * [INPUT]
 *    path: path to the GTOOL3 file.
 */
void
NAME(open_input)(int *iu, const char *path, int pathlen)
{
    char path_[PATH_MAX + 1];
    GT3_File *fp;

    *iu = -1;
    copy_f2c(path_, sizeof(path_), path, pathlen);
    if ((fp = GT3_open(path_)) != NULL) {
        *iu = get_varbuf(fp);
        if (*iu < 0)
            GT3_close(fp);
    }
    exit_on_error(*iu);
}


/*
 * another version of NAME(open_input).
 * use GT3_openHistFile().
 */
void
NAME(open_input2)(int *iu, const char *path, int pathlen)
{
    char path_[PATH_MAX + 1];
    GT3_File *fp;

    *iu = -1;
    copy_f2c(path_, sizeof(path_), path, pathlen);
    if ((fp = GT3_openHistFile(path_)) != NULL) {
        *iu = get_varbuf(fp);
        if (*iu < 0)
            GT3_close(fp);
    }
    exit_on_error(*iu);
}


/*
 * close an input stream.
 */
void
NAME(close_input)(const int *iu)
{
    GT3_File *fp;

    if (*iu >= 0 && *iu < MAX_NINPUTS && varbuf[*iu]) {
        fp = varbuf[*iu]->fp;
        GT3_freeVarbuf(varbuf[*iu]);
        GT3_close(fp);
        varbuf[*iu] = NULL;
    }
}


/*
 * close all input streams.
 */
void
NAME(close_input_all)(void)
{
    int i;
    GT3_File *fp;

    for (i = 0; i < MAX_NINPUTS; i++) {
        if (varbuf[i]) {
            fp = varbuf[i]->fp;
            GT3_freeVarbuf(varbuf[i]);
            GT3_close(fp);
        }
        varbuf[i] = NULL;
    }
}


/*
 * seek an input file.
 *
 * [INPUT]
 *   dest: destination position (starting with 0).
 *   whence: SEEK_SET(0), SEEK_CUR(1), or SEEK_END(2).
 */
void
NAME(seek)(const int *iu,
           const int *dest, const int *whence,
           int *status)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_seek: Invalid input(%d)", *iu);
    else
        *status = GT3_seek(varbuf[*iu]->fp, *dest, *whence);
    exit_on_error(*status);
}


/*
 * rewind an input stream.
 * equivalent to NAME(seek)(iu, 0, 0, status)
 */
void
NAME(rewind)(const int *iu, int *status)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_rewind: Invalid input(%d)", *iu);
    else
        *status = GT3_rewind(varbuf[*iu]->fp);
    exit_on_error(*status);
}


/*
 * go to the next chunk.
 */
void
NAME(next)(const int *iu, int *status)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_next: Invalid input(%d)", *iu);
    else
        *status = GT3_next(varbuf[*iu]->fp);
    exit_on_error(*status);
}


/*
 * return 0 if not EOF.
 */
void
NAME(eof)(const int *iu, int *status)
{
    *status = -1;
    if (invalid_input(*iu)) {
        gt3_error(GT3_ERR_CALL, "gt3f_eof: Invalid input(%d)", *iu);
        exit_on_error(*status);
        return;
    }
    *status = GT3_eof(varbuf[*iu]->fp);
}


/*
 * get current chunk position (starting with 0).
 *
 * return -1 if an error.
 */
void
NAME(tell_input)(const int *iu, int *pos)
{
    *pos = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_tell_input: Invalid input(%d)", *iu);
    else
        *pos = varbuf[*iu]->fp->curr;
    exit_on_error(*pos);
}


/*
 * get filename from unit number.
 */
void
NAME(get_filename)(const int *iu, char *path, int *status, int pathlen)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_get_filename: Invalid input(%d)", *iu);
    else {
        copy_c2f(path, pathlen, varbuf[*iu]->fp->path);
        *status = 0;
    }
    exit_on_error(*status);
}


/*
 * get the number of chunks which are contained in an input stream.
 */
void
NAME(get_num_chunks)(const int *iu, int *num)
{
    *num = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_get_num_chunks: Invalid input(%d)", *iu);
    else
        *num = GT3_getNumChunk(varbuf[*iu]->fp);

    exit_on_error(*num);
}


/*
 * get data shape at current chunk.
 */
void
NAME(get_shape)(const int *iu, int *shape, int *status)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "gt3f_get_shape: Invalid input(%d)", *iu);
    else {
        shape[0] = varbuf[*iu]->fp->dimlen[0];
        shape[1] = varbuf[*iu]->fp->dimlen[1];
        shape[2] = varbuf[*iu]->fp->dimlen[2];
        *status = 0;
    }
    exit_on_error(*status);
}


void
NAME(read_header)(const int *iu, char *head, int *status, int dummy)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL,
                  "gt3f_read_header: Invalid input(%d)", *iu);
    else
        *status = GT3_readHeader((GT3_HEADER *)head, varbuf[*iu]->fp);

    exit_on_error(*status);
}


/*
 * return the number of chunks in a file.
 *
 *    -1: if an error.
 */
void
NAME(count_chunk)(int *num, const char *path, int pathlen)
{
    char path_[PATH_MAX + 1];

    copy_f2c(path_, sizeof(path_), path, pathlen);
    *num = GT3_countChunk(path_);
}


/*
 * read variable data.
 */
static int
readin(double *buf, int *ncopied,
       int xsize, int ysize, int zsize,
       int xread, int yread, int zread,
       int xoff, int yoff, int zoff,
       GT3_Varbuf *vbuf)
{
    int shape[3], k, j;
    int inum, jnum, knum, bufstep;
    double *ptr;

    *ncopied = 0;
    shape[0] = vbuf->fp->dimlen[0];
    shape[1] = vbuf->fp->dimlen[1];
    shape[2] = vbuf->fp->dimlen[2];

    bufstep = xsize * ysize;

    inum = min3(xsize, shape[0] - xoff, xread);
    jnum = min3(ysize, shape[1] - yoff, yread);
    knum = min3(zsize, shape[2] - zoff, zread);
    if (inum <= 0 || jnum <= 0)
        knum = 0;

    for (k = 0; k < knum; k++, buf += bufstep) {
        if (GT3_readVarZ(vbuf, zoff + k) < 0)
            return -1;

        if (xoff == 0 && xsize == inum)
            *ncopied += GT3_copyVarDouble(buf, inum * jnum,
                                          vbuf,
                                          yoff * shape[0],
                                          1);
        else
            for (j = 0, ptr = buf; j < jnum; j++, ptr += xsize)
                *ncopied += GT3_copyVarDouble(ptr, inum,
                                              vbuf,
                                              xoff + (yoff + j) * shape[0],
                                              1);
    }
    return 0;
}


/*
 * [OUTPUT]
 *    buf: output buffer.
 *    ncopied: the number of elements copied.
 * [INPUT]
 *    iu: unit number.
 *    xsize, ysize, zsize: buffer size (3-D shape).
 *    xread, yread, zread: size to be read.
 *    xoff, yoff, zoff: offset for each dimension (starting with 0).
 */
void
NAME(read_var)(const int *iu, double *buf,
               const int *xsize, const int *ysize, const int *zsize,
               const int *xread, const int *yread, const int *zread,
               const int *xoff, const int *yoff, const int *zoff,
               int *ncopied)
{
    int status = -1;

    if (invalid_input(*iu)) {
        gt3_error(GT3_ERR_CALL, "gt3f_read_var: Invalid input(%d)", *iu);
        goto finish;
    }

    status = readin(buf, ncopied,
                    *xsize, *ysize, *zsize,
                    *xread, *yread, *zread,
                    *xoff, *yoff, *zoff,
                    varbuf[*iu]);
finish:
    exit_on_error(status);
}


/*
 * load variable data from a file (path).
 *
 * Note: tidx starting with 0.
 */
void
NAME(load_var)(const char *path, const int *tidx, double *buf,
               const int *xsize, const int *ysize, const int *zsize,
               const int *xread, const int *yread, const int *zread,
               const int *xoff, const int *yoff, const int *zoff,
               int *ncopied)
{
    GT3_File *fp = NULL;
    GT3_Varbuf *vbuf = NULL;
    int status = -1;

    if ((fp = GT3_open(path)) == NULL
        || GT3_seek(fp, *tidx, SEEK_SET) < 0
        || (vbuf = GT3_getVarbuf(fp)) == NULL)
        goto finish;

    status = readin(buf, ncopied,
                    *xsize, *ysize, *zsize,
                    *xread, *yread, *zread,
                    *xoff, *yoff, *zoff,
                    vbuf);

finish:
    GT3_freeVarbuf(vbuf);
    GT3_close(fp);
    exit_on_error(status);
}


void
NAME(get_miss)(const int *iu, double *vmiss, int *status)
{
    if (invalid_input(*iu)) {
        gt3_error(GT3_ERR_CALL, "gt3f_get_miss: Invalid input(%d)", *iu);

        *status = -1;
        exit_on_error(*status);
        return;
    }

    *vmiss = varbuf[*iu]->miss;
    *status = 0;
}


/*
 * get dimension length by name.
 * -1 if unknown error.
 *
 * e.g.)
 *   call gt3f_get_dimlen(nlon, 'GLON128')
 */
void
NAME(get_dimlen)(int *length, const char *name, int namelen)
{
    char name_[17];

    copy_f2c(name_, sizeof(name_), name, namelen);
    *length = GT3_getDimlen(name_);
}


/*
 * get grids by name, such as 'GLON320', 'GGLA160'.
 */
void
NAME(get_grid)(double *loc, const int *locsize,
               const char *name, int *status, int namelen)
{
    char name_[17];
    GT3_Dim *dim;
    int i, size;

    *status = -1;
    copy_f2c(name_, sizeof(name_), name, namelen);

    if ((dim = GT3_getDim(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = min(*locsize, dim->len - dim->cyclic);
    for (i = 0; i < size; i++)
        loc[i] = dim->values[i];

    GT3_freeDim(dim);
    *status = 0;
}


void
NAME(get_weight)(double *wght, const int *wghtsize,
                 const char *name, int *status, int namelen)
{
    char name_[17];
    double *w;
    int i, size;

    *status = -1;
    copy_f2c(name_, sizeof(name_), name, namelen);

    if ((w = GT3_getDimWeight(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = GT3_getDimlen(name_);
    size = min(*wghtsize, size);
    for (i = 0; i < size; i++)
        wght[i] = w[i];

    free(w);
    *status = 0;
}


void
NAME(get_gridbound)(double *bnds, int *vsize,
                    const char *name, int *status, int namelen)
{
    char name_[17];
    GT3_DimBound *dimbnd;
    int i, size;

    *status = -1;
    copy_f2c(name_, sizeof(name_), name, namelen);

    if ((dimbnd = GT3_getDimBound(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = min(*vsize, dimbnd->len);
    for (i = 0; i < size; i++)
        bnds[i] = dimbnd->bnd[i];

    GT3_freeDimBound(dimbnd);
    *status = 0;
}


/*
 * get calendar-type ID by name ('gregorian', 'noleap', ...).
 */
void
NAME(get_calendar)(int *calendar, const char *name, int namelen)
{
    char name_[17];

    copy_f2c(name_, sizeof(name_), name, namelen);
    *calendar = GT3_calendar_type(name_);
}


void
NAME(check_date)(int *status,
                 const int *year, const int *mon, const int *day,
                 const int *calendar)
{
    GT3_Date date;

    GT3_setDate(&date, *year, *mon, *day, 0, 0, 0);
    *status = GT3_checkDate(&date, *calendar);
}


/*
 * set the basetime (the origin of the time axis).
 */
void
NAME(set_basetime)(const int *year, const int *mon, const int *day,
                   const int *hh, const int *mm, const int *ss)
{
    basetime.year = *year;
    basetime.mon  = *mon;
    basetime.day  = *day;
    basetime.hour = *hh;
    basetime.min  = *mm;
    basetime.sec  = *ss;
}


/*
 * date(int[6]) -> time(double).
 *
 * get elapsed time[seconds] since the 'basetime'.
 *
 * [INPUT]
 *   d: array(6) [yyyy-mm-dd hh:mm:ss].
 *   calendar: calendar type.
 */
void
NAME(get_time)(double *time,
               const int *d, const int *calendar,
               int *status)
{
    GT3_Date date;

    *status = -1;
    if (GT3_checkDate(&basetime, *calendar) < 0) {
        gt3_error(GT3_ERR_CALL,
                  "gt3f_get_time: Invalid basedate(%04d-%02d-%02d)",
                  basetime.year, basetime.mon, basetime.day);
        exit_on_error(*status);
        return;
    }

    GT3_setDate(&date, d[0], d[1], d[2], d[3], d[4], d[5]);
    if (GT3_checkDate(&date, *calendar) < 0) {
        gt3_error(GT3_ERR_CALL,
                  "gt3f_get_time: Invalid date(%04d-%02d-%02d)",
                  date.year, date.mon, date.day);
        exit_on_error(*status);
        return;
    }

    *time = GT3_getTime(&date, &basetime, GT3_UNIT_SEC, *calendar);
    *status = 0;
}


/*
 * add time[seconds] to the date (d).
 */
static int
add_time(int *d, double time, int calendar)
{
    GT3_Date date;
    GT3_Duration dur;
    double value;

    GT3_setDate(&date, d[0], d[1], d[2], d[3], d[4], d[5]);
    if (GT3_checkDate(&date, calendar) < 0)
        return -1;

    value = round(time / (24. * 3600));
    if (value > INT_MAX || value < INT_MIN) {
        gt3_error(GT3_ERR_CALL, "Too large time value(%e)", time);
        return -1;
    }

    dur.value = (int)value;
    dur.unit = GT3_UNIT_DAY;
    GT3_addDuration(&date, &dur, calendar);

    value = round(time - 24. * 3600. * dur.value);
    dur.value = (int)value;
    dur.unit = GT3_UNIT_SEC;
    GT3_addDuration(&date, &dur, calendar);

    d[0] = date.year;
    d[1] = date.mon;
    d[2] = date.day;
    d[3] = date.hour;
    d[4] = date.min;
    d[5] = date.sec;
    return 0;
}


/*
 * time(double) -> date(int[6]).
 *
 * inverse function of NAME(get_time).
 */
void
NAME(get_date)(int *date, const double *time, const int *calendar,
               int *status)
{
    date[0] = basetime.year;
    date[1] = basetime.mon;
    date[2] = basetime.day;
    date[3] = basetime.hour;
    date[4] = basetime.min;
    date[5] = basetime.sec;

    *status = add_time(date, *time, *calendar);
    exit_on_error(*status);
}


/*
 * get mid-date between date1 and date2.
 */
void
NAME(get_middate)(int *date, const int *date1, const int *date2,
                  const int *calendar, int *status)
{
    GT3_Date mid, d1, d2;

    *status = -1;
    GT3_setDate(&d1, date1[0], date1[1], date1[2],
                date1[3], date1[4], date1[5]);
    GT3_setDate(&d2, date2[0], date2[1], date2[2],
                date2[3], date2[4], date2[5]);

    if (GT3_midDate(&mid, &d1, &d2, *calendar) < 0) {
        exit_on_error(*status);
        return;
    }

    date[0] = mid.year;
    date[1] = mid.mon;
    date[2] = mid.day;
    date[3] = mid.hour;
    date[4] = mid.min;
    date[5] = mid.sec;
    *status = 0;
}


/*
 * add days into 'date'.
 */
void
NAME(add_days)(int *date,
               const int *ndays, const int *calendar,
               int *status)
{
    *status = add_time(date, *ndays * 24 * 3600.0, *calendar);
    exit_on_error(*status);
}


/*
 * add seconds into 'date'.
 */
void
NAME(add_seconds)(int *date,
                  const double *sec, const int *calendar,
                  int *status)
{
    *status = add_time(date, *sec, *calendar);
    exit_on_error(*status);
}
