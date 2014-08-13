/*
 * write-urx.c  -- writing data in URX/MRX.
 *
 * XXX: URY obsolates URX.
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "myutils.h"
#include "talloc.h"
#include "int_pack.h"

#include "write-fmt.h"


static uint32_t
maxval_uint32(const uint32_t *vals, size_t num)
{
    uint32_t mv = 0;
    size_t i;

    for (i = 0; i < num; i++)
        if (vals[i] > mv)
            mv = vals[i];
    return mv;
}


static void
get_urx_parameterf(double *dma,
                   const float *data, size_t nelems,
                   double miss, unsigned nbits)
{
    float vmin = HUGE_VALF, vmax = -HUGE_VALF;
    float missf = (float)miss;
    int num = (1U << nbits) - 2;
    size_t i;

    for (i = 0; i < nelems; i++) {
        if (data[i] != missf) {
            vmin = data[i] < vmin ? data[i] : vmin;
            vmax = data[i] > vmax ? data[i] : vmax;
        }
    }
    if (vmin > vmax) {          /* no value */
        dma[0] = 0.;
        dma[1] = 0.;
    } else {
        if (num < 1)
            num = 1;
        scaling_parameters(dma, vmin, vmax, num);
        dma[1] *= num;
    }
}


static void
get_urx_parameter(double *dma,
                  const double *data, size_t nelems,
                  double miss, unsigned nbits)
{
    double vmin = HUGE_VAL, vmax = -HUGE_VAL;
    int num = (1U << nbits) - 2;
    size_t i;

    for (i = 0; i < nelems; i++) {
        if (data[i] != miss) {
            vmin = data[i] < vmin ? data[i] : vmin;
            vmax = data[i] > vmax ? data[i] : vmax;
        }
    }
    if (vmin > vmax) {          /* no value */
        dma[0] = 0.;
        dma[1] = 0.;
    } else {
        if (num < 1)
            num = 1;
        scaling_parameters(dma, vmin, vmax, num);
        dma[1] *= num;
    }
}


static int
write_urx(const void *ptr,
          size_t size,          /* 4(float) or 8(double) */
          size_t zelem,         /* # of elements in a z-plane */
          size_t nz,            /* # of z-planes */
          unsigned nbits, double miss,
          FILE *fp)
{
    double dma_buf[256];
    double *dma = dma_buf;
    double scale0;
    unsigned imiss;
    size_t i, nelems, len, plen, packed_len;
    const char *ptr2;
#define URXBUFSIZ (32 * 1024)
    unsigned idata[URXBUFSIZ];
    uint32_t packed[URXBUFSIZ];
    int rval = -1;

    assert(URXBUFSIZ % 32 == 0);

    if ((dma = tiny_alloc(dma_buf,
                          sizeof dma_buf,
                          2 * nz * sizeof(double))) == NULL) {
        gt3_error(SYSERR, NULL);
        goto finish;
    }

    /*
     * determine scaling parameters (auto-scaling)
     */
    if (size == 4) {
        const float *data = ptr;

        for (i = 0; i < nz; i++)
            get_urx_parameterf(dma + 2 * i,
                               data + i * zelem,
                               zelem, miss, nbits);
    } else {
        const double *data = ptr;

        for (i = 0; i < nz; i++)
            get_urx_parameter(dma + 2 * i,
                              data + i * zelem,
                              zelem, miss, nbits);
    }

    /*
     * write scaling parameters
     */
    if (write_dwords_into_record(dma, 2 * nz, fp) < 0)
        goto finish;

    imiss = (1U << nbits) - 1;
    scale0 = (imiss == 1) ? 1. : 1. / (imiss - 1);
    packed_len = pack32_len(zelem, nbits);

    /* write a header of data-body. */
    if (write_record_sep((uint64_t)4 * packed_len * nz, fp) < 0)
        goto finish;

    /*
     * write data-body (packed)
     */
    for (i = 0; i < nz; i++) {
        ptr2 = (const char *)ptr + i * zelem * size;
        nelems = zelem;

        while (nelems > 0) {
            len = (nelems > URXBUFSIZ) ? URXBUFSIZ : nelems;

            if (size == 4)
                scalingf(idata,
                         (float *)ptr2,
                         len,
                         dma[2*i], dma[1+2*i] * scale0,
                         imiss, miss);
            else
                scaling(idata,
                        (double *)ptr2,
                        len,
                        dma[2*i], dma[1+2*i] * scale0,
                        imiss, miss);

            plen = pack_bits_into32(packed, idata, len, nbits);

            if (IS_LITTLE_ENDIAN)
                reverse_words(packed, plen);

            if (fwrite(packed, 4, plen, fp) != plen) {
                gt3_error(SYSERR, NULL);
                goto finish;
            }

            ptr2 += len * size;
            nelems -= len;
        }
    }

    /* write a trailer of data-body */
    if (write_record_sep((uint64_t)4 * packed_len * nz, fp) < 0)
        goto finish;

    rval = 0;

finish:
    tiny_free(dma, dma_buf);
    return rval;
}


int
write_urx_via_double(const void *ptr,
                     size_t zelem, size_t nz,
                     unsigned nbits, double miss, FILE *fp)
{
    return write_urx(ptr, sizeof(double), zelem, nz, nbits, miss, fp);
}

int
write_urx_via_float(const void *ptr,
                    size_t zelem, size_t nz,
                    unsigned nbits, double miss, FILE *fp)
{
    return write_urx(ptr, sizeof(float), zelem, nz, nbits, miss, fp);
}


static int
write_mrx(const void *ptr2,
          size_t size,          /* 4 or 8 */
          size_t zelems, size_t nz,
          unsigned nbits, double miss,
          FILE *fp)
{
    const char *ptr = ptr2;
    uint32_t cnt_buf[128];
    uint32_t plen_buf[128];
    double dma_buf[256];
    size_t cnt0;
    uint32_t *cnt = cnt_buf;
    uint32_t *plen = plen_buf;
    double *dma = dma_buf;
    uint64_t plen_all = 0;
    uint32_t plen_a;
    unsigned imiss;
    double scale0;
    unsigned i;
    int rval = -1;

    if ((cnt = tiny_alloc(cnt_buf,
                          sizeof cnt_buf,
                          sizeof(uint32_t) * nz)) == NULL
        || (plen = tiny_alloc(plen_buf,
                              sizeof plen_buf,
                              sizeof(uint32_t) * nz)) == NULL
        || (dma = tiny_alloc(dma_buf,
                             sizeof dma_buf,
                             sizeof(double) * 2 * nz)) == NULL) {
        gt3_error(SYSERR, NULL);
        goto finish;
    }

    for (ptr = ptr2, i = 0; i < nz; i++, ptr += zelems * size) {
        cnt0 = masked_count(ptr, size, zelems, miss);
        if (cnt0 > 0xffffffffU) {
            gt3_error(GT3_ERR_TOOLONG, "in writing MRX");
            goto finish;
        }
        cnt[i] = (uint32_t)cnt0;
        plen[i] = (uint32_t)pack32_len(cnt[i], nbits);
        plen_all += plen[i];

        if (size == 4)
            get_urx_parameterf(dma + 2 * i, (float *)ptr,
                               zelems, miss, nbits);
        else
            get_urx_parameter(dma + 2 * i, (double *)ptr,
                              zelems, miss, nbits);
    }

    if (plen_all > 0xffffffffU) {
        gt3_error(GT3_ERR_TOOLONG, "in writing MRY");
        goto finish;
    }
    plen_a = (uint32_t)plen_all;

    if (write_words_into_record(&plen_a, 1, fp) < 0
        || write_words_into_record(cnt, nz, fp) < 0
        || write_words_into_record(plen, nz, fp) < 0
        || write_dwords_into_record(dma, 2 * nz, fp) < 0
        || write_mask(ptr2, size, zelems, nz, miss, fp) < 0)
        goto finish;

    imiss = (1U << nbits) - 1;
    scale0 = (imiss == 1) ? 1. : 1. / (imiss - 1);

    /* HEADER */
    if (write_record_sep(4 * plen_all, fp) < 0)
        goto finish;

    /* BODY */
    {
        size_t ncopied, len;
        unsigned idata_buf[32 * 1024];
        uint32_t packed_buf[32 * 1024];
        unsigned *idata = idata_buf;
        uint32_t *packed = packed_buf;

        if ((idata = tiny_alloc(
                 idata_buf,
                 sizeof idata_buf,
                 sizeof(unsigned) * maxval_uint32(cnt, nz))) == NULL
            || (packed = tiny_alloc(
                    packed_buf,
                    sizeof packed_buf,
                    sizeof(uint32_t) * maxval_uint32(plen, nz))) == NULL) {

            tiny_free(idata, idata_buf);
            tiny_free(packed, packed_buf);
            goto finish;
        }

        for (i = 0; i < nz; i++) {
            ptr = (const char *)ptr2 + i * zelems * size;

            if (size == 4)
                ncopied = masked_scalingf(idata,
                                          (float *)ptr,
                                          zelems,
                                          dma[2*i], dma[1+2*i] * scale0,
                                          imiss, miss);
            else
                ncopied = masked_scaling(idata,
                                         (double *)ptr,
                                         zelems,
                                         dma[2*i], dma[1+2*i] * scale0,
                                         imiss, miss);

            assert(ncopied == cnt[i]);
            len = pack_bits_into32(packed, idata, ncopied, nbits);
            if (IS_LITTLE_ENDIAN)
                reverse_words(packed, len);

            if (fwrite(packed, 4, len, fp) != len) {
                gt3_error(SYSERR, NULL);
                goto finish;
            }
        }

        tiny_free(idata, idata_buf);
        tiny_free(packed, packed_buf);
    }

    /* TRAILER */
    if (write_record_sep(4 * plen_all, fp) < 0)
        goto finish;

    rval = 0;

finish:
    tiny_free(dma, dma_buf);
    tiny_free(plen, plen_buf);
    tiny_free(cnt, cnt_buf);
    return rval;
}


int
write_mrx_via_double(const void *ptr,
                     size_t zelems, size_t nz,
                     unsigned nbits, double miss,
                     FILE *fp)
{
    return write_mrx(ptr, sizeof(double), zelems, nz, nbits, miss, fp);
}


int
write_mrx_via_float(const void *ptr,
                    size_t zelems, size_t nz,
                    unsigned nbits, double miss,
                    FILE *fp)
{
    return write_mrx(ptr, sizeof(float), zelems, nz, nbits, miss, fp);
}
