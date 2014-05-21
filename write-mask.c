/*
 * write.c  -- writing data in GT3_Var.
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

#define BUFLEN  (IO_BUF_SIZE / 4)
#define BUFLEN8 (IO_BUF_SIZE / 8)


static void
get_flag_for_mask(unsigned char *flag,
                  size_t len,
                  const void *ptr,
                  size_t size, double miss)
{
    int i;

    if (size == 4) {
        const float *data = ptr;
        float missf = (float)miss;

        for (i = 0; i < len; i++)
            flag[i] = (data[i] != missf) ? 1 : 0;
    } else {
        const double *data = ptr;

        for (i = 0; i < len; i++)
            flag[i] = (data[i] != miss) ? 1 : 0;
    }
}


static size_t
masked_copyf(size_t *nread,
             float *dest,
             const void *srcptr,
             size_t size,       /* 4 or 8 */
             size_t destlen, size_t srclen,
             double miss)
{
    size_t cnt;
    int i;

    if (size == 4) {
        const float *data = srcptr;
        float missf = (float)miss;

        for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
            if (data[i] != missf) {
                dest[cnt] = data[i];
                cnt++;
            }
    } else {
        const double *data = srcptr;

        for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
            if (data[i] != miss) {
                dest[cnt] = (float)data[i];
                cnt++;
            }
    }

    *nread = i;
    return cnt;
}


static size_t
masked_copy(size_t *nread,
            double *dest,
            const void *srcptr,
            size_t size,        /* 4 or 8 */
            size_t destlen, size_t srclen,
            double miss)
{
    size_t cnt;
    int i;

    if (size == 4) {
        const float *data = srcptr;
        float missf = (float)miss;

        for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
            if (data[i] != missf) {
                dest[cnt] = data[i];
                cnt++;
            }
    } else {
        const double *data = srcptr;

        for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
            if (data[i] != miss) {
                dest[cnt] = data[i];
                cnt++;
            }
    }

    *nread = i;
    return cnt;
}


unsigned
masked_count(const void *ptr, size_t size, size_t nelems, double miss)
{
    unsigned cnt;
    int i;

    if (size == 4) {
        const float *data = ptr;
        float missf = (float)miss;

        for (cnt = 0, i = 0; i < nelems; i++)
            if (data[i] != missf)
                cnt++;
    } else {
        const double *data = ptr;

        for (cnt = 0, i = 0; i < nelems; i++)
            if (data[i] != miss)
                cnt++;
    }
    return cnt;
}


/*
 * write MASK (common to MR4, MR8, MRX).
 */
int
write_mask(const void *ptr2,
           size_t size, /* 4(float) or 8(double) */
           size_t nelems, size_t nsets,
           double miss, FILE *fp)
{
    uint32_t mask[BUFLEN];
    unsigned char flag[32 * BUFLEN];
    size_t num, masklen, len, mlen;
    int n;
    const char *ptr = ptr2;

    masklen = pack32_len(nelems, 1);
    if (write_record_sep(4 * masklen * nsets, fp) < 0)
        return -1;

    for (n = 0; n < nsets; n++) {
        num = nelems;

        while (num > 0) {
            len = num > 32 * BUFLEN ? 32 * BUFLEN : num;

            get_flag_for_mask(flag, len, ptr, size, miss);

            mlen = pack_bools_into32(mask, flag, len);
            assert(mlen > 0 && mlen <= BUFLEN);

            if (IS_LITTLE_ENDIAN)
                reverse_words(mask, mlen);

            if (fwrite(mask, 4, mlen, fp) != mlen) {
                gt3_error(SYSERR, NULL);
                return -1;
            }
            num -= len;
            ptr += len * size;
        }
    }

    if (write_record_sep(4 * masklen * nsets, fp) < 0)
        return -1;

    return 0;
}


static int
write_mr4(const void *ptr2,
          size_t size,          /* ptr2: 4(float) or 8(double) */
          size_t nelems, double miss, FILE *fp)
{
    const char *ptr;
    uint32_t cnt;
    size_t ncopy, nread;
    float copied[BUFLEN];

    /*
     * write the # of not-missing value.
     */
    cnt = (uint32_t)masked_count(ptr2, size, nelems, miss);
    if (write_words_into_record(&cnt, 1, fp) < 0)
        return -1;

    /*
     * write MASK.
     */
    if (write_mask(ptr2, size, nelems, 1, miss, fp) < 0)
        return -1;

    /*
     * write DATA-BODY.
     */
    if (write_record_sep(sizeof(float) * cnt, fp) < 0)
        return -1;

    ptr = ptr2;
    while (nelems > 0) {
        ncopy = masked_copyf(&nread, copied, ptr, size, BUFLEN, nelems, miss);

        if (IS_LITTLE_ENDIAN)
            reverse_words(copied, ncopy);

        if (fwrite(copied, sizeof(float), ncopy, fp) != ncopy) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

        nelems -= nread;
        ptr += nread * size;
    }

    if (write_record_sep(sizeof(float) * cnt, fp) < 0)
        return -1;
    return 0;
}


static int
write_mr8(const void *ptr2,
          size_t size,          /* ptr2: 4(float) or 8(double) */
          size_t nelems, double miss, FILE *fp)
{
    const char *ptr;
    uint32_t cnt;
    size_t ncopy, nread;
    double copied[BUFLEN];

    /*
     * write the # of not-missing value.
     */
    cnt = (uint32_t)masked_count(ptr2, size, nelems, miss);
    if (write_words_into_record(&cnt, 1, fp) < 0)
        return -1;

    /*
     * write MASK.
     */
    if (write_mask(ptr2, size, nelems, 1, miss, fp) < 0)
        return -1;

    /*
     * write DATA-BODY.
     */
    if (write_record_sep(sizeof(double) * cnt, fp) < 0)
        return -1;

    ptr = ptr2;
    while (nelems > 0) {
        ncopy = masked_copy(&nread, copied, ptr, size, BUFLEN, nelems, miss);

        if (IS_LITTLE_ENDIAN)
            reverse_dwords(copied, ncopy);

        if (fwrite(copied, sizeof(double), ncopy, fp) != ncopy) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

        nelems -= nread;
        ptr += nread * size;
    }

    if (write_record_sep(sizeof(double) * cnt, fp) < 0)
        return -1;
    return 0;
}


int
write_mr4_via_double(const double *data, size_t nelems, double miss, FILE *fp)
{
    return write_mr4(data, sizeof(double), nelems, miss, fp);
}

int
write_mr4_via_float(const float *data, size_t nelems, double miss, FILE *fp)
{
    return write_mr4(data, sizeof(float), nelems, miss, fp);
}

int
write_mr8_via_double(const double *data, size_t nelems, double miss, FILE *fp)
{
    return write_mr8(data, sizeof(double), nelems, miss, fp);
}

int
write_mr8_via_float(const float *data, size_t nelems, double miss, FILE *fp)
{
    return write_mr8(data, sizeof(float), nelems, miss, fp);
}
