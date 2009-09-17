/*
 *  read_urc.c -- read URC.
 */
#include "internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "debug.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif


typedef void (*UNPACK_FUNC)(const unsigned *packed, int packed_len,
                            double ref, int ne, int nd,
                            double miss, float *data);

static void *
sread_word(void *dest, const void *src)
{
    char *q = dest;
    const char *p = src;

    if (IS_LITTLE_ENDIAN) {
        q[0] = p[3];
        q[1] = p[2];
        q[2] = p[1];
        q[3] = p[0];
    } else {
        q[0] = p[0];
        q[1] = p[1];
        q[2] = p[2];
        q[3] = p[3];
    }

    return dest;
}


static void *
sread_dword(void *dest, const void *src)
{
    char *q = dest;
    const char *p = src;

    if (IS_LITTLE_ENDIAN) {
        q[0] = p[7];
        q[1] = p[6];
        q[2] = p[5];
        q[3] = p[4];
        q[4] = p[3];
        q[5] = p[2];
        q[6] = p[1];
        q[7] = p[0];
    } else {
        q[0] = p[0];
        q[1] = p[1];
        q[2] = p[2];
        q[3] = p[3];
        q[4] = p[4];
        q[5] = p[5];
        q[6] = p[6];
        q[7] = p[7];
    }

    return dest;
}

/*
 *  read_URCv() supports URC1 and URC2 format.
 *
 *  XXX: 'skip' and 'nelem' are not in bytes.
 */
static int
read_URCv(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp,
          UNPACK_FUNC unpack_func)
{
    unsigned packed[1024];
    unsigned char pbuf[8 + 4 + 4 + 7 * sizeof(fort_size_t)];
    off_t off;
    double ref;
    int nd, ne;
    fort_size_t sizh;
    size_t num;
    int i;
    float *outp;

    off = var->fp->off
        + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
        + (8 + 4 + 4
           + 2 * var->dimlen[0] * var->dimlen[1]
           + 8 * sizeof(fort_size_t)) * zpos;

    if (fseeko(fp, off, SEEK_SET) < 0) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    /*
     *  Three parameters (ref, D, and E)
     */
    if (xfread(pbuf, 1, sizeof pbuf, fp) < 0)
        return -1;

    sread_dword(&ref, pbuf + 4); /* ref (double) */
    sread_word(&nd,   pbuf + 20); /* D (integer) */
    sread_word(&ne,   pbuf + 32); /* E (integer) */

    debug3("URC(ref,nd,ne) = %.4g, %d, %d", ref, nd, ne);

    /* fortran header for packed data... */
    sread_word(&sizh, pbuf + sizeof pbuf - 4);
    if (sizh != 2 * var->dimlen[0] * var->dimlen[1]) {
        gt3_error(GT3_ERR_BROKEN, NULL);
        return -1;
    }

    /*
     *
     */
    skip &= ~1U;
    nelem = (nelem + 1) & ~1U;

    if (skip != 0 && fseeko(fp, 2 * skip, SEEK_CUR) < 0) {
        gt3_error(SYSERR, "read_URCv()");
        return -1;
    }

    assert(var->type == GT3_TYPE_FLOAT);
    outp = (float *)var->data + skip;

    /*
     *  unpack data and store them into var->data.
     */
    for (i = 0; nelem > 0; i++, nelem -= num) {
        num = min(nelem, sizeof packed / 2);

        if (xfread(packed, 4, num / 2, fp) < 0)
            return -1;

        /* reversing byte order */
        if (IS_LITTLE_ENDIAN)
            reverse_words(packed, num / 2);

        unpack_func(packed, num / 2, ref, ne, nd,
                    var->miss, outp + i * (sizeof packed / 2));
    }
    return 0;
}


int
read_URC1(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_URCv(var, zpos, skip, nelem, fp, urc1_unpack);
}


int
read_URC2(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
    return read_URCv(var, zpos, skip, nelem, fp, urc2_unpack);
}
