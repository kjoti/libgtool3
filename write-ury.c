/*
 * write-ury.c  -- writing data in URY/MRY.
 */
#include "internal.h"

#include <assert.h>
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
get_ury_parameterf(double *dma,
                   const float *data, size_t nelem,
                   double miss, int nbits)
{
    int in, ix;
    float missf = (float)miss;
    int num = (1U << nbits) - 2;

    in = find_min_float(data, nelem, &missf);
    if (in < 0) {
        dma[0] = 0.;
        dma[1] = 0.;
        return;
    }
    ix = find_max_float(data, nelem, &missf);
    assert(ix >= 0);

    if (num < 1)
        num = 1;
    scaling_parameters(dma, data[in], data[ix], num);
}


static void
get_ury_parameter(double *dma,
                  const double *data, size_t nelem,
                  double miss, int nbits)
{
    int in, ix;
    int num = (1U << nbits) - 2;

    in = find_min_double(data, nelem, &miss);
    if (in < 0) {
        dma[0] = 0.;
        dma[1] = 0.;
        return;
    }
    ix = find_max_double(data, nelem, &miss);
    assert(ix >= 0);

    if (num < 1)
        num = 1;
    scaling_parameters(dma, data[in], data[ix], num);
}


static int
write_ury(const void *ptr,
          size_t size,          /* 4(float) or 8(double) */
          size_t zelem,         /* # of elements in a z-plane */
          size_t nz,            /* # of z-planes */
          int nbits, double miss,
          FILE *fp)
{
    double dma_buf[256];
    double *dma = dma_buf;
    unsigned imiss;
    size_t nelems, len, plen, packed_len;
    int i;
    const char *ptr2;
#define URYBUFSIZ (32 * 1024)
    unsigned idata[URYBUFSIZ];
    uint32_t packed[URYBUFSIZ];

    assert(URYBUFSIZ % 32 == 0);

    if ((dma = (double *)
         tiny_alloc(dma_buf,
                    sizeof dma_buf,
                    2 * nz * sizeof(double))) == NULL)
        goto error;

    /*
     * determine scaling parameters (auto-scaling)
     */
    if (size == 4) {
        const float *data = ptr;

        for (i = 0; i < nz; i++)
            get_ury_parameterf(dma + 2 * i,
                               data + i * zelem,
                               zelem, miss, nbits);
    } else {
        const double *data = ptr;

        for (i = 0; i < nz; i++)
            get_ury_parameter(dma + 2 * i,
                              data + i * zelem,
                              zelem, miss, nbits);
    }

    /*
     * write scaling parameters
     */
    if (write_dwords_into_record(dma, 2 * nz, fp) < 0)
        goto error;

    imiss = (1U << nbits) - 1;
    packed_len = pack32_len(zelem, nbits);

    /*
     * write a header of data-body.
     */
    if (write_record_sep(4 * packed_len * nz, fp) < 0)
        goto error;

    /*
     * write data-body (packed)
     */
    for (i = 0; i < nz; i++) {
        ptr2 = (const char *)ptr + i *zelem * size;
        nelems = zelem;

        while (nelems > 0) {
            len = (nelems > URYBUFSIZ) ? URYBUFSIZ : nelems;

            if (size == 4)
                scalingf(idata,
                         (float *)ptr2,
                         len,
                         dma[2*i], dma[1+2*i],
                         imiss, miss);
            else
                scaling(idata,
                        (double *)ptr2,
                        len,
                        dma[2*i], dma[1+2*i],
                        imiss, miss);

            plen = pack_bits_into32(packed, idata, len, nbits);

            if (IS_LITTLE_ENDIAN)
                reverse_words(packed, plen);

            if (fwrite(packed, 4, plen, fp) != plen)
                goto error;

            ptr2 += len * size;
            nelems -= len;
        }
    }

    /* write a trailer of data-body */
    if (write_record_sep(4 * packed_len * nz, fp) < 0)
        goto error;

    tiny_free(dma, dma_buf);
    return 0;

error:
    gt3_error(SYSERR, NULL);
    tiny_free(dma, dma_buf);
    return -1;
}


int
write_ury_via_double(const void *ptr,
                     size_t zelem, size_t nz,
                     int nbits, double miss, FILE *fp)
{
    return write_ury(ptr, sizeof(double), zelem, nz, nbits, miss, fp);
}


int
write_ury_via_float(const void *ptr,
                    size_t zelem, size_t nz,
                    int nbits, double miss, FILE *fp)
{
    return write_ury(ptr, sizeof(float), zelem, nz, nbits, miss, fp);
}


static int
write_mry(const void *ptr2,
          size_t size,          /* 4 or 8 */
          size_t zelems, size_t nz,
          int nbits, double miss,
          FILE *fp)
{
    const char *ptr = ptr2;
    uint32_t cnt_buf[128];
    uint32_t plen_buf[128];
    double dma_buf[256];
    uint32_t *cnt = cnt_buf;
    uint32_t *plen = plen_buf;
    double *dma = dma_buf;
    uint32_t plen_all;
    unsigned imiss;
    unsigned i;

    if ((cnt = (uint32_t *)
         tiny_alloc(cnt_buf,
                    sizeof cnt_buf,
                    sizeof(uint32_t) * nz)) == NULL
        || (plen = (uint32_t *)
            tiny_alloc(plen_buf,
                       sizeof plen_buf,
                       sizeof(uint32_t) * nz)) == NULL
        || (dma = (double *)
            tiny_alloc(dma_buf,
                       sizeof dma_buf,
                       sizeof(double) * 2 * nz)) == NULL)
        goto error;

    for (ptr = ptr2, i = 0; i < nz; i++, ptr += zelems * size) {
        cnt[i] = masked_count(ptr, size, zelems, miss);

        plen[i] = (uint32_t)pack32_len(cnt[i], nbits);

        if (size == 4)
            get_ury_parameterf(dma + 2 * i, (float *)ptr,
                               zelems, miss, nbits);
        else
            get_ury_parameter(dma + 2 * i, (double *)ptr,
                              zelems, miss, nbits);
    }

    for (plen_all = 0, i = 0; i < nz; i++)
        plen_all += plen[i];

    if (write_words_into_record(&plen_all, 1, fp) < 0
        || write_words_into_record(cnt, nz, fp) < 0
        || write_words_into_record(plen, nz, fp) < 0
        || write_dwords_into_record(dma, 2 * nz, fp) < 0
        || write_mask(ptr2, size, zelems, nz, miss, fp) < 0)
        goto error;


    imiss = (1U << nbits) - 1;

    /*
     * write packed array.
     */
    if (write_record_sep(4 * plen_all, fp) < 0)
        goto error;
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
            goto error;
        }

        for (i = 0; i < nz; i++) {
            ptr = (const char *)ptr2 + i * zelems * size;

            if (size == 4)
                ncopied = masked_scalingf(idata,
                                          (float *)ptr,
                                          zelems,
                                          dma[2*i], dma[1+2*i],
                                          imiss, miss);
            else
                ncopied = masked_scaling(idata,
                                         (double *)ptr,
                                         zelems,
                                         dma[2*i], dma[1+2*i],
                                         imiss, miss);

            assert(ncopied == cnt[i]);
            len = pack_bits_into32(packed, idata, ncopied, nbits);
            if (IS_LITTLE_ENDIAN)
                reverse_words(packed, len);

            if (fwrite(packed, 4, len, fp) != len)
                goto error;
        }

        tiny_free(idata, idata_buf);
        tiny_free(packed, packed_buf);
    }
    if (write_record_sep(4 * plen_all, fp) < 0)
        goto error;

    tiny_free(dma, dma_buf);
    tiny_free(plen, plen_buf);
    tiny_free(cnt, cnt_buf);
    return 0;

error:
    gt3_error(SYSERR, NULL);
    tiny_free(dma, dma_buf);
    tiny_free(plen, plen_buf);
    tiny_free(cnt, cnt_buf);
    return -1;
}


int
write_mry_via_double(const void *ptr,
                     size_t zelems, size_t nz,
                     int nbits, double miss,
                     FILE *fp)
{
    return write_mry(ptr, sizeof(double), zelems, nz, nbits, miss, fp);
}


int
write_mry_via_float(const void *ptr,
                    size_t zelems, size_t nz,
                    int nbits, double miss,
                    FILE *fp)
{
    return write_mry(ptr, sizeof(float), zelems, nz, nbits, miss, fp);
}
