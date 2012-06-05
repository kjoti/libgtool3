/*
 * if_fortran.c -- provide the interface to the Fortran language.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"

#define MAX_NOUTPUTS OPEN_MAX
#define MAX_NINPUTS OPEN_MAX
static FILE *outputs[MAX_NOUTPUTS];
static GT3_Varbuf *varbuf[MAX_NINPUTS];

#ifndef min
#  define min(a,b) ((a)>(b) ? (b) : (a))
#endif

/* NAME convention. */
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
exit_on_error(int err)
{
    if (err != 0 && stop_flag != 0) {
        GT3_printErrorMessages(stderr);
        fprintf(stderr, "**** STOP in if_fortran..\n");
        exit(1);
    }
}


/*
 * set stop flag.
 *   flag:  0: No stop even if an error.
 *          1: stop if an error.
 */
void
NAME(set_stop_flag)(int *flag)
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
NAME(close_output)(int *status, const int *iu)
{
    *status = -1;
    if (*iu < 0 || *iu >= MAX_NOUTPUTS || outputs[*iu] == NULL) {
        gt3_error(GT3_ERR_CALL, NULL);
    } else {
        if ((*status = fclose(outputs[*iu])) != 0)
            gt3_error(SYSERR, NULL);
        outputs[*iu] = NULL;
    }
    exit_on_error(*status);
}


/*
 * Note: There is no way to check if 'head' has enough rooom.
 */
void
NAME(init_header)(char *head, int dummy_)
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
NAME(write)(int *status,
            const int *iu,
            const void *ptr, const int *type,
            const int *nx, const int *ny, const int *nz,
            const char *head, const char *dfmt,
            int dummy, int dfmtlen)
{
    char fmt[17];

    *status = -1;
    if (*iu < 0 || *iu >= MAX_NOUTPUTS || outputs[*iu] == NULL)
        gt3_error(GT3_ERR_CALL, "if_fortran: write()");
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
        gt3_error(GT3_ERR_CALL, "no more slots for input");
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
NAME(close_input)(int *status, const int *iu)
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
 *  dest: destination position.
 *  whence: 0: SEEK_SET(0), SEEK_CUR(1), or SEEK_END(2).
 */
void
NAME(seek_input)(int *status, const int *iu,
                 const int *dest, const int *whence)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "seek_input(): invalid input");
    else
        *status = GT3_seek(varbuf[*iu]->fp, *dest, *whence);

    exit_on_error(*status);
}


/*
 * goto next chunk.
 * Note: equivalent to NAME(seek_input)(status, iu, 1, 1)
 */
void
NAME(next_input)(int *status, const int *iu)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "next_input(): invalid input");
    else
        *status = GT3_next(varbuf[*iu]->fp);

    exit_on_error(*status);
}


void
NAME(eof_input)(int *status, const int *iu)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "next_input(): invalid input");
    else
        *status = GT3_eof(varbuf[*iu]->fp);

    exit_on_error(*status);
}


/*
 * get current chunk position (starting with 0).
 */
void
NAME(tell_input)(int *current, const int *iu)
{
    *current = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "tell_input(): invalid input");
    else
        *current = varbuf[*iu]->fp->curr;
    exit_on_error(*current);
}


/*
 * get data shape at current chunk.
 */
void
NAME(get_dimsize)(int *status, int *shape, const int *iu)
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
NAME(read_header)(int *status, char *head, const int *iu, int dummy)
{
    *status = -1;
    if (invalid_input(*iu))
        gt3_error(GT3_ERR_CALL, "read_header(): invalid input");
    else
        *status = GT3_readHeader((GT3_HEADER *)head, varbuf[*iu]->fp);

    exit_on_error(*status);
}


/*
 * read variable data.
 *
 * [OUTPUT]
 *    status: the number of copied elements (-1 if an error).
 *       buf: output buffer.
 *
 * [INPUT]
 *   bufsize: size of output buffer.
 *        iu: unit number.
 */
void
NAME(read_var)(int *status,
               double *buf, const int *bufsize,
               const int *iu)
{
    int i, nz, nelems;
    size_t roomsize;

    *status = -1;
    if (invalid_input(*iu) || *bufsize < 0) {
        gt3_error(GT3_ERR_CALL, "read_var(): invalid input");
        goto finish;
    }

    roomsize = (size_t)(*bufsize);
    nz = varbuf[*iu]->fp->dimlen[2];

    for (i = 0; i < nz && roomsize > 0; i++) {
        if (GT3_readVarZ(varbuf[*iu], i) < 0)
            goto finish;

        nelems = GT3_copyVarDouble(buf, roomsize, varbuf[*iu], 0, 1);

        buf += nelems;
        roomsize -= nelems;
    }
    *status = *bufsize - (int)roomsize;

finish:
    exit_on_error(*status);
}


/*
 * read data on a specified Z-layer.
 */
void
NAME(read_var_z)(int *status,
                 double *buf, const int *bufsize,
                 const int *iu, const int *z)
{
    *status = -1;
    if (invalid_input(*iu) || *bufsize < 0) {
        gt3_error(GT3_ERR_CALL, "read_var_z(): invalid input");
        goto finish;
    }
    if (GT3_readVarZ(varbuf[*iu], *z) < 0)
        goto finish;

    *status = GT3_copyVarDouble(buf, *bufsize, varbuf[*iu], 0, 1);
finish:
    exit_on_error(*status);
}


/*
 * get grid data by name, such as 'GLON320', 'GGLA160'.
 */
void
NAME(getgrid)(int *status,
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
NAME(getgridweight)(int *status,
                    double *wght, int *vsize,
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
NAME(gt3_getgridbound)(int *status,
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
