/*
 * varbuf.c -- Buffer to read data from GT3_File.
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "bits_set.h"
#include "int_pack.h"
#include "talloc.h"
#include "debug.h"

#define RESERVE_SIZE (640*320)

/*
 * status staff for GT3_Varbuf.
 * This is not accessible from clients.
 */
struct varbuf_status {
    GT3_HEADER head;

    int ch;                     /* cached chunk-index */
    int z;                      /* cached z-index (-1: not cached) */
    bits_set y;                 /* cached y-index */
};
typedef struct varbuf_status varbuf_status;

static int read_UR4(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);
static int read_UR8(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);
static int read_MR4(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);
static int read_MR8(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);

typedef int (*RFptr)(GT3_Varbuf *, int, size_t, size_t, FILE *);
static RFptr read_fptr[] = {
    read_UR4,
    read_URC2,
    read_URC1,
    read_UR8,
    read_URX,
    read_MR4,
    read_MR8,
    read_MRX,
    read_URY,
    read_MRY,
    NULL
};

#define clip(v, l, h) ((v) < (l) ? (l) : ((v) > (h) ? (h) : v))


static int
read_UR4(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    float *ptr;
    off_t off;
    size_t hsize;

    hsize = var->dimlen[0] * var->dimlen[1];
    off = var->fp->off
        + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
        + sizeof(float) * (zpos * hsize + skip)
        + sizeof(fort_size_t);

    if (fseeko(fp, off, SEEK_SET) < 0) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    assert(var->type == GT3_TYPE_FLOAT);
    assert(var->bufsize >= sizeof(float) * nelem);

    ptr = (float *)var->data;
    ptr += skip;

    if (xfread(ptr, sizeof(float), nelem, fp) < 0)
        return -1;

    if (IS_LITTLE_ENDIAN)
        reverse_words(ptr, nelem);

    return 0;
}


static int
read_UR8(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    double *ptr;
    off_t off;
    size_t hsize;

    hsize = var->dimlen[0] * var->dimlen[1];
    off = var->fp->off
        + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
        + sizeof(double) * (zpos * hsize + skip)
        + sizeof(fort_size_t);

    if (fseeko(fp, off, SEEK_SET) < 0) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    assert(var->type == GT3_TYPE_DOUBLE);
    ptr = (double *)var->data;
    ptr += skip;

    if (xfread(ptr, sizeof(double), nelem, fp) < 0)
        return -1;

    if (IS_LITTLE_ENDIAN)
        reverse_dwords(ptr, nelem);

    return 0;
}


/*
 * load the mask data, setup the mask index, and read data body.
 * (common to MR4 and MR8).
 */
static int
read_MRN_pre(void *temp,
             size_t *nread,
             GT3_Varbuf *var,
             size_t size,       /* size of each data (4 or 8) */
             int zpos, size_t skip, size_t nelem)
{
    GT3_Datamask *mask;
    size_t ncount;
    int idx0, interval;
    off_t off;

    interval = var->dimlen[0];
    /* assert(skip % interval == 0); */
    /* assert(nelem % interval == 0); */

    mask = var->fp->mask;
    if (!mask && (mask = GT3_newMask()) == NULL)
        return -1;

    /*
     * load mask data.
     */
    if (GT3_loadMask(mask, var->fp) != 0)
        return -1;
    var->fp->mask = mask;

    if (GT3_updateMaskIndex(mask, interval) < 0)
        return -1;

    /*
     * seek to the begging of the data-body.
     */
    idx0 = zpos * var->dimlen[1] + skip / interval;
    off = var->fp->off + 6 * sizeof(fort_size_t)
        + GT3_HEADER_SIZE       /* header */
        + 4                     /* NNN */
        + 4 * ((mask->nelem + 31) / 32) /* MASK */
        + sizeof(fort_size_t)
        + size * mask->index[idx0];

    if (fseeko(var->fp->fp, off, SEEK_SET) < 0)
        return -1;

    /*
     * ncount: the # of MASK-ON elements to read.
     */
    ncount = mask->index[idx0 + nelem / interval] - mask->index[idx0];
    assert(ncount <= nelem);

    if (xfread(temp, size, ncount, var->fp->fp) < 0)
        return -1;

    *nread = ncount;
    return 0;
}


static int
read_MR4(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    float masked_buf[RESERVE_SIZE];
    float *outp, *masked = NULL;
    size_t nread, offnum, i, n;

    assert(var->type == GT3_TYPE_FLOAT);

    if ((masked = tiny_alloc(masked_buf,
                             sizeof masked_buf,
                             sizeof(float) * nelem)) == NULL) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    if (read_MRN_pre(masked, &nread, var, sizeof(float),
                     zpos, skip, nelem) < 0) {
        tiny_free(masked, masked_buf);
        return -1;
    }

    if (IS_LITTLE_ENDIAN)
        reverse_words(masked, nread);

    offnum = var->dimlen[0] * var->dimlen[1] * zpos + skip;
    outp = (float *)var->data;
    outp += skip;
    for (i = 0, n = 0; i < nelem; i++) {
        if (getMaskValue(var->fp->mask, offnum + i)) {
            outp[i] = masked[n];
            n++;
        } else
            outp[i] = (float)(var->miss);
    }
    assert(n == nread);

    tiny_free(masked, masked_buf);

    return 0;
}


static int
read_MR8(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    double masked_buf[RESERVE_SIZE];
    double *outp, *masked = NULL;
    size_t nread, offnum, i, n;

    assert(var->type == GT3_TYPE_DOUBLE);

    if ((masked = tiny_alloc(masked_buf,
                             sizeof masked_buf,
                             sizeof(double) * nelem)) == NULL) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    if (read_MRN_pre(masked, &nread, var, sizeof(double),
                     zpos, skip, nelem) < 0) {
        tiny_free(masked, masked_buf);
        return -1;
    }

    if (IS_LITTLE_ENDIAN)
        reverse_dwords(masked, nread);

    offnum = var->dimlen[0] * var->dimlen[1] * zpos + skip;
    outp = (double *)var->data;
    outp += skip;
    for (i = 0, n = 0; i < nelem; i++) {
        if (getMaskValue(var->fp->mask, offnum + i)) {
            outp[i] = masked[n];
            n++;
        } else
            outp[i] = var->miss;
    }
    assert(n == nread);

    tiny_free(masked, masked_buf);

    return 0;
}


static int
update_varbuf(GT3_Varbuf *vbuf, GT3_File *fp)
{
    int dim[3];
    GT3_HEADER head;
    void *data = NULL;
    size_t newsize, elsize;
    int type;
    double missd;
    varbuf_status *status;

    if (GT3_readHeader(&head, fp) < 0)
        return -1;

    switch (fp->fmt) {
    case GT3_FMT_UR4:
    case GT3_FMT_MR4:
    case GT3_FMT_URC:
    case GT3_FMT_URC1:
        type = GT3_TYPE_FLOAT;
        elsize = sizeof(float);
        break;
    default:
        type = GT3_TYPE_DOUBLE;
        elsize = sizeof(double);
        break;
    }

    /*
     * set missing value.
     */
    if (GT3_decodeHeaderDouble(&missd, &head, "MISS") < 0) {
        gt3_error(GT3_ERR_HEADER, "MISS");

        missd = -999.0; /* ignore this error... */
    }

    dim[0] = fp->dimlen[0];
    dim[1] = fp->dimlen[1];
    dim[2] = fp->dimlen[2];

    newsize = elsize * (((size_t)dim[0] * dim[1] + 1) & ~1U);
    if (newsize > vbuf->bufsize) {
        /*
         * reallocation of data buffer.
         */
        if ((data = realloc(vbuf->data, newsize)) == NULL) {
            gt3_error(SYSERR, NULL);
            return -1;
        }
    }

    if (vbuf->stat_ == NULL) {
        if ((status = malloc(sizeof(varbuf_status))) == NULL) {
            gt3_error(SYSERR, NULL);
            free(data);
            return -1;
        }
        memset(status, 0, sizeof(varbuf_status));
        debug0("update_varbuf(): status allocated");
    } else
        status = (varbuf_status *)vbuf->stat_;

    /*
     * clear 'status'.
     */
    if (resize_bits_set(&status->y, dim[1] + 1) < 0) {
        gt3_error(SYSERR, NULL);
        return -1;
    }
    BS_CLSALL(status->y);
    GT3_copyHeader(&status->head, &head);
    status->ch = fp->curr;
    status->z = -1;

    /*
     * all checks passed.
     */
    vbuf->fp = fp;
    vbuf->type = type;
    vbuf->dimlen[0] = dim[0];
    vbuf->dimlen[1] = dim[1];
    vbuf->dimlen[2] = dim[2];
    vbuf->miss = missd;

    if (data) {
        vbuf->bufsize = newsize;
        vbuf->data = data;
        debug1("update_varbuf(): reallocated: %d-byte", vbuf->bufsize);
    }

    if (vbuf->stat_ == NULL)
        vbuf->stat_ = status;

    debug1("update_varbuf(): type  = %d", type);
    debug1("update_varbuf(): bufsize  = %d", vbuf->bufsize);
    debug3("update_varbuf(): dim = %d, %d, %d", dim[0], dim[1], dim[2]);
    debug1("update_varbuf(): miss  = %g", vbuf->miss);
    return 0;
}


static int
update2_varbuf(GT3_Varbuf *var)
{
    if (!GT3_isHistfile(var->fp)) {
        varbuf_status *stat = (varbuf_status *)var->stat_;

        if (stat->ch != var->fp->curr)
            return update_varbuf(var, var->fp);
    }
    return 0;
}


static GT3_Varbuf *
new_varbuf(void)
{
    GT3_Varbuf *temp;

    if ((temp = malloc(sizeof(GT3_Varbuf))) == NULL) {
        gt3_error(SYSERR, NULL);
        return NULL;
    }

    memset(temp, 0, sizeof(GT3_Varbuf));
    temp->fp = NULL;
    temp->data = NULL;
    temp->stat_ = NULL;
    return temp;
}


void
GT3_freeVarbuf(GT3_Varbuf *var)
{
    if (var) {
        varbuf_status *stat = (varbuf_status *)var->stat_;

        /* XXX GT_File is not closed.  */
        free(var->data);
        if (stat)
            free_bits_set(&stat->y);
        free(var->stat_);
        free(var);
    }
}


/*
 * Use GT3_reattachVarbuf() if possible.
 */
GT3_Varbuf *
GT3_getVarbuf2(GT3_Varbuf *old, GT3_File *fp)
{
    int newed = 0;

    if (old == NULL) {
        if ((old = new_varbuf()) == NULL)
            return NULL;

        newed = 1;
    }

    if (update_varbuf(old, fp) < 0) {
        if (newed)
            GT3_freeVarbuf(old);
        return NULL;
    }
    return old;
}


GT3_Varbuf *
GT3_getVarbuf(GT3_File *fp)
{
    return GT3_getVarbuf2(NULL, fp);
}


int
GT3_readVarZ(GT3_Varbuf *var, int zpos)
{
    size_t nelem;
    varbuf_status *stat = (varbuf_status *)var->stat_;
    int fmt;

    if (update2_varbuf(var) < 0)
        return -1;

    if (zpos < 0 || zpos >= var->dimlen[2]) {
        gt3_error(GT3_ERR_INDEX, "GT3_readVarZ(): z=%d", zpos);
        return -1;
    }

    /*
     * check if cached.
     */
    if (stat->ch == var->fp->curr
        && stat->z == zpos
        && BS_TEST(stat->y, var->dimlen[1])) {
        debug2("cached: t=%d, z=%d", var->fp->curr, zpos);
        return 0;
    }

    nelem = var->dimlen[0] * var->dimlen[1];
    fmt = (int)(var->fp->fmt & GT3_FMT_MASK);

    if (read_fptr[fmt](var, zpos, 0, nelem, var->fp->fp) < 0) {
        debug2("read failed: t=%d, z=%d", var->fp->curr, zpos);

        stat->z = -1;
        return -1;
    }

    /* set flags */
    stat->ch = var->fp->curr;
    stat->z = zpos;
    BS_SET(stat->y, var->dimlen[1]);

    return 0;
}


int
GT3_readVarZY(GT3_Varbuf *var, int zpos, int ypos)
{
    size_t skip, nelem;
    varbuf_status *stat = (varbuf_status *)var->stat_;
    int i, fmt;
    int supported[] = {
        GT3_FMT_UR4,
        GT3_FMT_URC,
        GT3_FMT_URC1,
        GT3_FMT_UR8,
        GT3_FMT_MR4,
        GT3_FMT_MR8
    };

    if (update2_varbuf(var) < 0)
        return -1;

    if (   zpos < 0 || zpos >= var->dimlen[2]
        || ypos < 0 || ypos >= var->dimlen[1]) {
        gt3_error(GT3_ERR_INDEX, "GT3_readVarZY(): y=%d, z=%d", ypos, zpos);
        return -1;
    }

    /*
     * In some format, use GT3_readVarZ().
     */
    fmt = (int)(var->fp->fmt & GT3_FMT_MASK);
    for (i = 0; i < sizeof supported / sizeof(int); i++)
        if (fmt == supported[i])
            break;
    if (i == sizeof supported / sizeof(int))
        return GT3_readVarZ(var, zpos);

    /*
     * for small buffer-size.
     */
    if (var->dimlen[0] * var->dimlen[1] < 1024)
        return GT3_readVarZ(var, zpos);

    /*
     * check if cached.
     */
    if (stat->ch == var->fp->curr
        && stat->z == zpos
        && (BS_TEST(stat->y, ypos) || BS_TEST(stat->y, var->dimlen[1]))) {

        debug3("cached: t=%d, z=%d, y=%d", var->fp->curr, zpos, ypos);
        return 0;
    }

    skip = ypos * var->dimlen[0];
    nelem = var->dimlen[0];
    if (read_fptr[fmt](var, zpos, skip, nelem, var->fp->fp) < 0) {
        debug3("read failed: t=%d, z=%d, y=%d", var->fp->curr, zpos, ypos);

        stat->z  = -1;
        return -1;
    }

    /*
     * set flags
     */
    if (stat->z != zpos || stat->ch != var->fp->curr) {
        BS_CLSALL(stat->y);
    }
    stat->ch = var->fp->curr;
    stat->z = zpos;
    BS_SET(stat->y, ypos);

    return 0;
}


int
GT3_readVar(double *rval, GT3_Varbuf *var, int x, int y, int z)
{
    if (GT3_readVarZY(var, z, y) < 0)
        return -1;

    if (x < 0 || x >= var->dimlen[0]) {
        gt3_error(GT3_ERR_INDEX, "GT3_readVar(): x=%d", x);
        return -1;
    }

    if (var->type == GT3_TYPE_FLOAT) {
        float *ptr;

        ptr = (float *)var->data;
        *rval = ptr[x + var->dimlen[0] * y];
    } else {
        double *ptr;

        ptr = (double *)var->data;
        *rval = ptr[x + var->dimlen[0] * y];
    }
    return 0;
}


/*
 * NOTE
 * GT3_{copy,get}XXX() functions do not update GT3_Varbuf.
 */

/*
 * GT3_copyVarDouble() copies data stored in GT3_Varbuf into 'buf'.
 *
 * This function does not guarantee that GT3_Varbuf is filled.
 * Users are responsible for calling GT3_readZ() to read data.
 *
 * return value: The number of copied elements.
 *
 * example:
 *   GT3_copyVarDouble(buf, buflen, var, 0, 1);
 *   -> all z-slice data are copied.
 *
 *   GT3_copyVarDouble(buf, buflen, var, 0, var->dimlen[0]);
 *   ->  some meridional data are copied.
 */
int
GT3_copyVarDouble(double *buf, size_t buflen,
                  const GT3_Varbuf *var, int offset, int stride)
{
    int maxlen = var->dimlen[0] * var->dimlen[1];
    int end, nelem;

    if (stride > 0) {
        offset = clip(offset, 0, maxlen);

        end = maxlen;
        nelem = (end - offset + (stride - 1)) / stride;
    } else if (stride < 0) {
        offset = clip(offset, -1, maxlen - 1);

        end = -1;
        nelem = (end - offset + (stride + 1)) / stride;
    } else {
        if (offset < 0 || offset >= maxlen)
            nelem = 0;
        else
            nelem = buflen;
    }

    if (nelem > buflen)
        nelem = buflen;

    assert(offset + (nelem - 1) * stride >= 0);
    assert(offset + (nelem - 1) * stride < maxlen);

    if (var->type == GT3_TYPE_DOUBLE) {
        int i;
        double *ptr = var->data;

        ptr += offset;
        for (i = 0; i < nelem; i++)
            buf[i] = ptr[i * stride];
    } else {
        int i;
        float *ptr = var->data;

        ptr += offset;
        for (i = 0; i < nelem; i++)
            buf[i] = ptr[i * stride];
    }
    return nelem;
}


/*
 * Copy data stored in GT3_Varbuf into the float buffer.
 * See also GT3_copyVarDouble().
 */
int
GT3_copyVarFloat(float *buf, size_t buflen,
                 const GT3_Varbuf *var, int offset, int stride)
{
    int maxlen = var->dimlen[0] * var->dimlen[1];
    int end, nelem;

    if (stride > 0) {
        offset = clip(offset, 0, maxlen);

        end = maxlen;
        nelem = (end - offset + (stride - 1)) / stride;
    } else if (stride < 0) {
        offset = clip(offset, -1, maxlen - 1);

        end = -1;
        nelem = (end - offset + (stride + 1)) / stride;
    } else {
        if (offset < 0 || offset >= maxlen)
            nelem = 0;
        else
            nelem = buflen;
    }

    if (nelem > buflen)
        nelem = buflen;

    assert(offset + (nelem - 1) * stride >= 0);
    assert(offset + (nelem - 1) * stride < maxlen);

    if (var->type == GT3_TYPE_DOUBLE) {
        int i;
        double *ptr = var->data;

        ptr += offset;
        for (i = 0; i < nelem; i++)
            buf[i] = (float)ptr[i * stride];
    } else {
        int i;
        float *ptr = var->data;

        ptr += offset;
        for (i = 0; i < nelem; i++)
            buf[i] = ptr[i * stride];
    }
    return nelem;
}


char *
GT3_getVarAttrStr(char *attr, int len, const GT3_Varbuf *var, const char *key)
{
    varbuf_status *stat = (varbuf_status *)var->stat_;

    return GT3_copyHeaderItem(attr, len, &stat->head, key);
}


int
GT3_getVarAttrInt(int *attr, const GT3_Varbuf *var, const char *key)
{
    varbuf_status *stat = (varbuf_status *)var->stat_;

    return GT3_decodeHeaderInt(attr, &stat->head, key);
}


int
GT3_getVarAttrDouble(double *attr, const GT3_Varbuf *var, const char *key)
{
    varbuf_status *stat = (varbuf_status *)var->stat_;

    return GT3_decodeHeaderDouble(attr, &stat->head, key);
}


/*
 * Replace an associated file in Varbuf.
 */
int
GT3_reattachVarbuf(GT3_Varbuf *var, GT3_File *fp)
{
    if (var == NULL)
        return -1;

    return update_varbuf(var, fp);
}


#ifdef TEST
int
test(const char *path)
{
    GT3_File *fp;
    GT3_Varbuf *var;
    int i, j, k;
    double val;
    int dim1, dim2, dim3;

    if ((fp = GT3_open(path)) == NULL
        || (var = GT3_getVarbuf(fp)) == NULL) {
        return -1;
    }

    while (!GT3_eof(fp)) {
        printf("**** %d\n", fp->curr);

        dim1 = fp->dimlen[0];
        dim2 = fp->dimlen[1];
        dim3 = fp->dimlen[2];
        for (k = 0; k < dim3; k++)
            for (j = 0; j < dim2; j++)
                for (i = 0; i < dim1; i++) {
                    printf("%3d %3d %3d ", i, j, k);

                    if (GT3_readVar(&val, var, i, j, k) < 0)
                        printf("(NaN)\n");
                    else
                        printf("%20.8g\n", val);
                }

        if (GT3_next(fp) < 0)
            break;
    }
    GT3_freeVarbuf(var);
    GT3_close(fp);
    return 0;
}


int
main(int argc, char **argv)
{
    GT3_setPrintOnError(1);
    while (--argc > 0 && *++argv)
        test(*argv);

    return 0;
}
#endif
