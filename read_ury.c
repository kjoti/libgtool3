/*
 * read_ury.c -- read URY & MRY.
 *
 * Though URX and MRX are deprecated, they are still supported.
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "int_pack.h"
#include "talloc.h"
#include "debug.h"


#define RESERVE_SIZE (640*320)
#define RESERVE_NZ   256


static int
get_zero_index(double offset, double scale, int max_count)
{
    const double eps = 1e-7;
    double c;

    if (offset != 0. && scale != 0.) {
        c = floor(-offset / scale + 0.5);

        if (c > 0. && c < max_count
            && fabs(offset + c * scale) < eps * fabs(scale))
            return (int)c;
    }
    return 0;
}


/*
 * read packed data in URY-format and decode them.
 */
static int
read_packed(double *outp, size_t nelems,
            unsigned nbits, double offset, double scale,
            double miss, FILE *fp)
{
#define URYBUFSIZ 1024
    uint32_t packed[32 * URYBUFSIZ];
    unsigned idata[32 * URYBUFSIZ];
    int zero_index;
    uint32_t imiss;
    size_t npack_per_read, ndata_per_read;
    size_t npack, ndata, nrest, nrest_packed;
    int i;

    imiss = (1U << nbits) - 1;

    npack_per_read = URYBUFSIZ * nbits;
    ndata_per_read = 32 * URYBUFSIZ;

    nrest = nelems;
    nrest_packed = pack32_len(nelems, nbits);

    zero_index = get_zero_index(offset, scale, imiss - 1);

    while (nrest > 0) {
        npack = nrest_packed > npack_per_read
            ? npack_per_read
            : nrest_packed;

        ndata = nrest > ndata_per_read
            ? ndata_per_read
            : nrest;

        assert(npack == pack32_len(ndata, nbits));

        if (xfread(packed, 4, npack, fp) < 0)
            return -1;

        if (IS_LITTLE_ENDIAN)
            reverse_words(packed, npack);

        unpack_bits_from32(idata, ndata, packed, nbits);

        if (zero_index > 0)
            for (i = 0; i < ndata; i++)
                outp[i] = (idata[i] != imiss)
                    ? scale * ((int)idata[i] - zero_index)
                    : miss;
        else
            for (i = 0; i < ndata; i++)
                outp[i] = (idata[i] != imiss)
                    ? offset + idata[i] * scale
                    : miss;

        outp += ndata;
        nrest -= ndata;
        nrest_packed -= npack;
    }
    assert(nrest == 0 && nrest_packed == 0);

    return 0;
}


/*
 * XXX: 'skip' and 'nelem' are ignored for now.
 * read_URY2() reads all data in a z-plane.
 */
static int
read_URY2(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem,
          FILE *fp, int oldflag)
{
    off_t off;
    double dma[2], offset, scale;
    unsigned nbits;
    size_t zelems;              /* # of elements in a z-level */

    /*
     * XXX: read_URY() always reads all the data in a z-plane.
     * 'skip' and 'nelem' passed as an argument are ignored.
     */
    zelems = (size_t)(var->dimlen[0] * var->dimlen[1]);
    nbits = (unsigned)var->fp->fmt >> GT3_FMT_MBIT;

    /*
     * read packing parameters for URY.
     */
    off = var->fp->off + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t);
    if (fseeko(fp, off, SEEK_SET) < 0)
        return -1;
    if (read_dwords_from_record(dma, 2 * zpos, 2, fp) < 0)
        return -1;

    /*
     * skip to zpos.
     */
    off = sizeof(fort_size_t) + 4 * zpos * pack32_len(zelems, nbits);
    if (fseeko(fp, off, SEEK_CUR) < 0)
        return -1;

    offset = dma[0];
    scale = dma[1];
    if (oldflag)
        scale = (nbits == 1) ? 0. : dma[1] / ((1U << nbits) - 2);

    /*
     * read packed DATA-BODY in zpos.
     */
    if (read_packed(var->data, zelems, nbits,
                    offset, scale, var->miss, fp) < 0)
        return -1;

    return 0;
}


/*
 * Note: 3rd argument (skip) is ignored, which should be zero.
 */
static int
read_MRY2(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem,
          FILE *fp, int oldflag)
{
    off_t off;
    double dma[2], offset, scale;
    GT3_Datamask *mask;
    unsigned nbits;
    size_t skip2;
    int i, n;
    double *outp;
    int nnn_buf[RESERVE_NZ];
    double data_buf[RESERVE_SIZE];
    int *nnn = nnn_buf;         /* the Number of Non-missing Number  */
    double *data = data_buf;

    /*
     * read MASK.
     */
    mask = var->fp->mask;
    if (!mask && (mask = GT3_newMask()) == NULL)
        return -1;
    if (GT3_loadMaskX(mask, zpos, var->fp) != 0)
        return -1;
    var->fp->mask = mask;

    if ((nnn = (int *)tiny_alloc(nnn_buf,
                                 sizeof nnn_buf,
                                 sizeof(int) * var->dimlen[2])) == NULL)
        return -1;

    /* skip to NNN */
    off = var->fp->off
        + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
        + 4 + 2 * sizeof(fort_size_t);
    if (fseeko(fp, off, SEEK_SET) < 0)
        goto error;

    /* read NNN. */
    if (read_words_from_record(nnn, 0, var->dimlen[2], fp) < 0)
        goto error;

    /* skip IZLEN */
    if (read_words_from_record(NULL, 0, 0, fp) < 0)
        goto error;

    /* read DMA. */
    if (read_dwords_from_record(dma, 2 * zpos, 2, fp) < 0)
        goto error;

    /* skip MASK. */
    if (read_words_from_record(NULL, 0, 0, fp) < 0)
        goto error;

    nbits = (unsigned)var->fp->fmt >> GT3_FMT_MBIT;

    offset = dma[0];
    scale = dma[1];
    if (oldflag)
        scale = (nbits == 1) ? 0. : dma[1] / ((1U << nbits) - 2);

    /*
     * skip to zpos.
     */
    for (skip2 = sizeof(fort_size_t), i = 0; i < zpos; i++)
        skip2 += 4 * pack32_len(nnn[i], nbits);
    if (fseeko(fp, skip2, SEEK_CUR) < 0)
        goto error;

    /*
     * read packed data and decode them.
     */
    if ((data = tiny_alloc(data_buf,
                           sizeof data_buf,
                           sizeof(double) * nnn[zpos])) == NULL
        || read_packed(data, nnn[zpos], nbits,
                       offset, scale, var->miss, fp) < 0)
        goto error;

    /*
     * unmask.
     */
    assert(var->type == GT3_TYPE_DOUBLE);
    outp = var->data;
    for (i = 0, n = 0; i < nelem; i++)
        if (getMaskValue(mask, i)) {
            outp[i] = data[n];
            n++;
        } else
            outp[i] = var->miss;

    assert(n == nnn[zpos]);

    tiny_free(nnn, nnn_buf);
    tiny_free(data, data_buf);
    return 0;

error:
    gt3_error(SYSERR, NULL);
    tiny_free(nnn, nnn_buf);
    tiny_free(data, data_buf);
    return -1;
}


int
read_URY(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_URY2(var, zpos, skip, nelem, fp, 0);
}

int
read_MRY(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_MRY2(var, zpos, skip, nelem, fp, 0);
}

/* XXX: URX is  deprecated. */
int
read_URX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_URY2(var, zpos, skip, nelem, fp, 1);
}

/* XXX: MRX is  deprecated. */
int
read_MRX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_MRY2(var, zpos, skip, nelem, fp, 1);
}
