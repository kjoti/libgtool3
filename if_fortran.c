/*
 * if_fortran.c -- provide an interface to the Fortran language.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"

#ifndef OPEN_MAX
#  define OPEN_MAX 256
#endif

#define MAX_NOUTPUTS OPEN_MAX
#define MAX_NINPUTS OPEN_MAX
static FILE *outputs[MAX_NOUTPUTS];
static GT3_Varbuf *varbuf[MAX_NINPUTS];

#ifndef min
#  define min(a,b) ((a)>(b) ? (b) : (a))
#endif

/*
 * naming convention.
 */
#define NAME(x)  gt3f_ ## x ## _

static int stop_flag = 0;

/*
 * convert from Fortran string to C string.
 */
static char *
copy_fstring(char *dest, int destlen, const char *src, int srclen)
{
    int len, maxlen;

    maxlen = min(destlen - 1, srclen);
    assert(maxlen >= 0);

    for (len = maxlen; len > 0; len--)
        if (!isspace(src[len - 1]))
            break;

    memcpy(dest, src, len);
    dest[len] = '\0';
    return dest;
}


/*
 * terminate program if an error has occurred and stop_flag is ON.
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
 *   1: stop if an error.
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

    copy_fstring(path_, sizeof(path_), path, pathlen);

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
        gt3_error(GT3_ERR_CALL, "no more slots for output");

    exit_on_error(*iu);
}


void
NAME(close_output)(const int *iu, int *status)
{
    *status = -1;
    if (*iu < 0 || *iu >= MAX_NOUTPUTS)
        gt3_error(GT3_ERR_CALL, NULL);
    else {
        *status = outputs[*iu] ? fclose(outputs[*iu]) : 0;
        if (*status != 0)
            gt3_error(SYSERR, NULL);
        else
            outputs[*iu] = NULL;
    }
    exit_on_error(*status);
}


/*
 * Note: There is no way to check whether 'head' has enough rooom.
 */
void
NAME(init_header)(char *head, int dummy)
{
    GT3_initHeader((GT3_HEADER *)head);
}


void
NAME(set_header_string)(char *head,
                        const char *key, const char *value,
                        int dummy, int keylen, int vlen)
{
    char key_[17], value_[33];

    copy_fstring(key_, sizeof(key_), key, keylen);
    copy_fstring(value_, sizeof(value_), value, vlen);
    GT3_setHeaderString((GT3_HEADER *)head, key_, value_);
}


void
NAME(set_header_int)(char *head,
                     const char *key, const int *value,
                     int dummy, int keylen)
{
    char key_[17];

    copy_fstring(key_, sizeof(key_), key, keylen);
    GT3_setHeaderInt((GT3_HEADER *)head, key_, *value);
}


void
NAME(set_header_double)(char *head,
                        const char *key, const double *value,
                        int dummy, int keylen)
{
    char key_[17];

    copy_fstring(key_, sizeof(key_), key, keylen);
    GT3_setHeaderDouble((GT3_HEADER *)head, key_, *value);
}


void
NAME(set_header_date)(char *head,
                      const char *key,
                      const int *year, const int *mon, const int *day,
                      const int *hh, const int *mm, const int *ss,
                      int dummy, int keylen)
{
    char key_[17];
    GT3_Date date;

    copy_fstring(key_, sizeof(key_), key, keylen);
    date.year = *year;
    date.mon = *mon;
    date.day = *day;
    date.hour = *hh;
    date.min = *mm;
    date.sec = *ss;
    GT3_setHeaderDate((GT3_HEADER *)head, key_, &date);
}


void
NAME(get_header_int)(int *value,
                     const char *head, const char *key,
                     int *status,
                     int dummy, int keylen)
{
    char key_[17];
    int rval;

    copy_fstring(key_, sizeof(key_), key, keylen);

    *status = GT3_decodeHeaderInt(&rval, (const GT3_HEADER *)head, key_);

    if (*status == 0)
        *value = rval;
    exit_on_error(*status);
}


void
NAME(get_header_double)(double *value,
                        const char *head, const char *key,
                        int *status,
                        int dummy, int keylen)
{
    char key_[17];
    double rval;

    copy_fstring(key_, sizeof(key_), key, keylen);

    *status = GT3_decodeHeaderDouble(&rval, (const GT3_HEADER *)head, key_);

    if (*status == 0)
        *value = rval;
    exit_on_error(*status);
}


void
NAME(get_header_date)(int *values,
                      const char *head, const char *key,
                      int *status,
                      int dummy, int keylen)
{
    char key_[17];
    GT3_Date date;

    copy_fstring(key_, sizeof(key_), key, keylen);

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
        gt3_error(GT3_ERR_CALL, "%d: invalid stream", *iu);
    else {
        copy_fstring(fmt, sizeof(fmt), dfmt, dfmtlen);
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
        gt3_error(GT3_ERR_CALL, "if_fortran: no more slots for input");
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
    copy_fstring(path_, sizeof(path_), path, pathlen);

    if ((fp = GT3_openHistFile(path_)) != NULL) {
        *iu = get_varbuf(fp);
        if (*iu < 0)
            GT3_close(fp);
    }
    exit_on_error(*iu);
}


void
NAME(close_input)(const int *iu, int *status)
{
    GT3_File *fp;

    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, NULL);
    else {
        fp = varbuf[*iu]->fp;
        GT3_freeVarbuf(varbuf[*iu]);
        GT3_close(fp);

        varbuf[*iu] = NULL;
        *status = 0;
    }
    exit_on_error(*status);
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
        gt3_error(GT3_ERR_CALL, "seek_input(): invalid input");
    else
        *status = GT3_seek(varbuf[*iu]->fp, *dest, *whence);
    exit_on_error(*status);
}


/*
 * rewind input stream.
 * equivalent to NAME(seek)(iu, 0, 0, status)
 */
void
NAME(rewind)(const int *iu, int *status)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "seek_input(): invalid input");
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
        gt3_error(GT3_ERR_CALL, "next_input(): invalid input");
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
        gt3_error(GT3_ERR_CALL, "next_input(): invalid input");
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
        gt3_error(GT3_ERR_CALL, "tell_input(): invalid input");
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
    int len;

    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "invalid input");
    else {
        len = strlen(varbuf[*iu]->fp->path);
        len = min(len, pathlen);
        memcpy(path, varbuf[*iu]->fp->path, len);

        if (pathlen > len)
            memset(path + len, ' ', pathlen - len);
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
        gt3_error(GT3_ERR_CALL, "invalid input");
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
        gt3_error(GT3_ERR_CALL, "get_dimsize(): invalid input");
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
        gt3_error(GT3_ERR_CALL, "read_header(): invalid input");
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

    copy_fstring(path_, sizeof(path_), path, pathlen);
    *num = GT3_countChunk(path_);
}


/*
 * read variable data.
 *
 * [OUTPUT]
 *    buf: output buffer.
 *    ncopied: the number of copied elements.
 * [INPUT]
 *    iu: unit number.
 *    xsize, ysize, zsize: buffer shape.
 *    xoff, yoff, zoff:
 */
void
NAME(read_var)(const int *iu, double *buf,
               const int *xsize, const int *ysize, const int *zsize,
               const int *xoff, const int *yoff, const int *zoff,
               int *ncopied)
{
    int shape[3], status, k, j;
    int inum, jnum, knum, bufstep;
    double *ptr;

    status = -1;
    *ncopied = 0;
    if (invalid_input(*iu)) {
        gt3_error(GT3_ERR_CALL, "read_var(): invalid input");
        goto finish;
    }

    shape[0] = varbuf[*iu]->fp->dimlen[0];
    shape[1] = varbuf[*iu]->fp->dimlen[1];
    shape[2] = varbuf[*iu]->fp->dimlen[2];

    bufstep = *xsize * *ysize;

    inum = min(*xsize, shape[0] - *xoff);
    jnum = min(*ysize, shape[1] - *yoff);
    knum = min(*zsize, shape[2] - *zoff);

    if (inum <= 0 || jnum <= 0)
        knum = 0;

    for (k = 0; k < knum; k++, buf += bufstep) {
        if (GT3_readVarZ(varbuf[*iu], *zoff + k) < 0)
            goto finish;

        if (*xoff == 0 && *xsize == shape[0])
            *ncopied += GT3_copyVarDouble(buf, inum * jnum,
                                          varbuf[*iu],
                                          *yoff * shape[0],
                                          1);
        else
            for (j = 0, ptr = buf; j < jnum; j++, ptr += *xsize)
                *ncopied += GT3_copyVarDouble(ptr, inum,
                                              varbuf[*iu],
                                              *xoff + (*yoff + j) * shape[0],
                                              1);
    }
    status = 0;
finish:
    exit_on_error(status);
}


void
NAME(get_miss)(const int *iu, double *vmiss, int *status)
{
    if (invalid_input(*iu)) {
        gt3_error(GT3_ERR_CALL, "read_header(): invalid input");

        *status = -1;
        exit_on_error(*status);
        return;
    }

    *vmiss = varbuf[*iu]->miss;
    *status = 0;
}



/*
 * get grid data by name, such as 'GLON320', 'GGLA160'.
 */
void
NAME(get_grid)(int *status,
               double *v, int *vsize,
               const char *name, int namelen)
{
    char name_[17];
    GT3_Dim *dim;
    int i, size;

    *status = -1;
    copy_fstring(name_, sizeof(name_), name, namelen);

    if ((dim = GT3_getDim(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = min(*vsize, dim->len - dim->cyclic);
    for (i = 0; i < size; i++)
        v[i] = dim->values[i];

    GT3_freeDim(dim);
    *status = 0;
}


void
NAME(get_weight)(int *status,
                 double *wght, const int *vsize,
                 const char *name, int namelen)
{
    char name_[17];
    double *w;
    int i, size;

    *status = -1;
    copy_fstring(name_, sizeof(name_), name, namelen);

    if ((w = GT3_getDimWeight(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = GT3_getDimlen(name_);
    size = min(*vsize, size);
    for (i = 0; i < size; i++)
        wght[i] = w[i];

    free(w);
    *status = 0;
}


void
NAME(get_gridbound)(int *status,
                    double *bnd, int *vsize,
                    const char *name, int namelen)
{
    char name_[17];
    GT3_DimBound *dimbnd;
    int i, size;

    *status = -1;
    copy_fstring(name_, sizeof(name_), name, namelen);

    if ((dimbnd = GT3_getDimBound(name_)) == NULL) {
        exit_on_error(*status);
        return;
    }

    size = min(*vsize, dimbnd->len);
    for (i = 0; i < size; i++)
        bnd[i] = dimbnd->bnd[i];

    GT3_freeDimBound(dimbnd);
    *status = 0;
}


/*
 * read variable  datafile (path).
 *
 * [INPUT]
 *    t: time index (Data No.). Starting with 1.
 *    off: offset of variable elements.
 * [OUTPUT]
 *         v: output data.
 *    ncoped: the number of copied elements.
 */
void
NAME(load_var)(double *v, int *vsize, char *head,
               const char *path,
               const int *t, const unsigned *off,
               int *ncopied,
               int *ierr,
               int dummy, int pathlen)
{
    char path_[PATH_MAX + 1];
    GT3_File *fp = NULL;
    GT3_Varbuf *buf = NULL;
    GT3_HEADER h;
    unsigned hsize, skip;
    int nrequest, nread;
    int z;

    copy_fstring(path_, sizeof(path_), path, pathlen);

    *ierr = -1;
    *ncopied = 0;
    if ((fp = GT3_openHistFile(path_)) == NULL
        || (buf = GT3_getVarbuf(fp)) == NULL
        || GT3_seek(fp, *t, 0) < 0
        || GT3_readHeader(&h, fp) < 0)
        goto finish;

    hsize = buf->dimlen[0] * buf->dimlen[1];
    nrequest = min(*vsize, hsize * buf->dimlen[2] - *off);

    memcpy(head, &h, sizeof(GT3_HEADER));

    z = *off / hsize;
    skip = *off % hsize;
    while (nrequest > 0) {
        if (GT3_readVarZ(buf, z) < 0)
            goto finish;

        nread = GT3_copyVarDouble(v, nrequest, buf, skip, 1);
        assert(nread > 0);

        z++;
        skip = 0;
        nrequest -= nread;
        v += nread;
        *ncopied += nread;
    }
    *ierr = 0;
finish:
    GT3_close(fp);
    GT3_freeVarbuf(buf);
    exit_on_error(*ierr);
}


/*
 * load GTOOL3 header from a file.
 */
void
NAME(load_header)(char *head,
                  const char *path, const int *t,
                  int *ierr,
                  int dummy, int pathlen)
{
    char path_[PATH_MAX + 1];
    GT3_File *fp = NULL;
    GT3_HEADER h;

    copy_fstring(path_, sizeof(path_), path, pathlen);

    *ierr = -1;
    if ((fp = GT3_openHistFile(path_)) == NULL
        || GT3_seek(fp, *t, 0) < 0
        || GT3_readHeader(&h, fp) < 0)
        goto finish;

    memcpy(head, &h, GT3_HEADER_SIZE);
    *ierr = 0;
finish:
    GT3_close(fp);
    exit_on_error(*ierr);
}
