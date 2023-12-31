/*
 * write.c  -- writing data.
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

/* a pointer to urc1_packing or urc2_packing */
typedef void (*PACKING_FUNC)(uint32_t *, const float *, int, double,
                             double, double, double);


static int
write_ur4_via_double(const double *data, size_t nelems, FILE *fp)
{
    float copied[BUFLEN];
    size_t len;
    int ncopy, i;

    if (write_record_sep((uint64_t)4 * nelems, fp) < 0)
        return -1;

    len = nelems;
    while (len > 0) {
        ncopy = (len > BUFLEN) ? BUFLEN : len;

        for (i = 0; i < ncopy; i++)
            copied[i] = (float)data[i];

        if (IS_LITTLE_ENDIAN)
            reverse_words(copied, ncopy);

        if (fwrite(copied, 4, ncopy, fp) != ncopy) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

        data += ncopy;
        len -= ncopy;
    }

    if (write_record_sep((uint64_t)4 * nelems, fp) < 0)
        return -1;

    return 0;
}


static int
write_ur4_via_float(const void *ptr, size_t len, FILE *fp)
{
    return write_words_into_record(ptr, len, fp);
}


static int
write_ur8_via_double(const void *ptr, size_t len, FILE *fp)
{
    return write_dwords_into_record(ptr, len, fp);
}


static int
write_ur8_via_float(const float *data, size_t nelems, FILE *fp)
{
    double copied[BUFLEN8];
    size_t len;
    int ncopy, i;

    if (write_record_sep((uint64_t)8 * nelems, fp) < 0)
        return -1;

    len = nelems;
    while (len > 0) {
        ncopy = (len > BUFLEN8) ? BUFLEN8 : len;

        for (i = 0; i < ncopy; i++)
            copied[i] = (double)data[i];

        if (IS_LITTLE_ENDIAN)
            reverse_dwords(copied, ncopy);

        if (fwrite(copied, 8, ncopy, fp) != ncopy) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

        data += ncopy;
        len -= ncopy;
    }

    if (write_record_sep((uint64_t)8 * nelems, fp) < 0)
        return -1;

    return 0;
}


static int
write_urc_zslice(const float *data, size_t len, double miss,
                 PACKING_FUNC packing, FILE *fp)
{
    char siz4[] = { 0, 0, 0, 4 };
    char siz8[] = { 0, 0, 0, 8 };
    uint64_t siz;
    uint32_t packed[8192];
    unsigned char parambuf[8 + 4 + 4 + 3 * 2 * 4];
    double rmin, ref, fac_e, fac_d;
    int ne, nd;
    int len_pack;
    int maxelem;

    calc_urc_param(data, len, miss, &rmin, &fac_e, &fac_d, &ne, &nd);

    /*
     * three packing parameters (REF, ND, and NE)
     */
    ref = rmin * fac_d;
    memcpy(parambuf,      siz8, 4); /* REF head */
    memcpy(parambuf + 12, siz8, 4); /* REF tail */
    memcpy(parambuf + 16, siz4, 4); /* ND  head */
    memcpy(parambuf + 24, siz4, 4); /* ND  tail */
    memcpy(parambuf + 28, siz4, 4); /* NE  head */
    memcpy(parambuf + 36, siz4, 4); /* NE  tail */
    if (IS_LITTLE_ENDIAN) {
        memcpy(parambuf +  4, reverse_dwords(&ref, 1), 8);
        memcpy(parambuf + 20, reverse_words(&nd,   1), 4);
        memcpy(parambuf + 32, reverse_words(&ne,   1), 4);
    } else {
        memcpy(parambuf +  4, &ref, 8);
        memcpy(parambuf + 20, &nd,  4);
        memcpy(parambuf + 32, &ne,  4);
    }
    if (fwrite(parambuf, 1, sizeof parambuf, fp) != sizeof parambuf) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    /* HEADER */
    siz = 2 * len;
    if (write_record_sep(siz, fp) < 0)
        return -1;

    /*
     * data body
     */
    maxelem = sizeof packed / 2;
    while (len > 0) {
        len_pack = (len > maxelem) ? maxelem : len;

        /*
         * 2-byte packing
         */
        packing(packed, data, len_pack, miss, rmin, fac_e, fac_d);

        if (IS_LITTLE_ENDIAN)
            reverse_words(packed, (len_pack + 1) / 2);

        /* write packed data */
        if (fwrite(packed, 2, len_pack, fp) != len_pack) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

        data += len_pack;
        len -= len_pack;
    }

    /* TRAILER */
    if (write_record_sep(siz, fp) < 0)
        return -1;

    return 0;
}


static int
write_urc_via_float(const float *data, size_t len, int nz, double miss,
                    PACKING_FUNC packing, FILE *fp)
{
    int i;

    for (i = 0; i < nz; i++) {
        write_urc_zslice(data, len, miss, packing, fp);
        data += len;
    }
    return 0;
}


static int
write_urc_via_double(const double *input, size_t len, int nz, double miss,
                     PACKING_FUNC packing, FILE *fp)
{
    int i;
    size_t n;
    float *data;

    if ((data = malloc(sizeof(float) * len)) == NULL) {
        gt3_error(SYSERR, NULL);
        return -1;
    }
    for (i = 0; i < nz; i++) {
        for (n = 0; n < len; n++)
            data[n] = (float)input[n];

        write_urc_zslice(data, len, miss, packing, fp);
        input += len;
    }
    free(data);
    return 0;
}


/*
 * GT3_output_format() gives actual output format from user-specified name.
 */
int
GT3_output_format(char *dfmt, const char *str)
{
    int fmt;

    if (strcmp(str, "URC1") == 0)
        fmt = GT3_FMT_URC1;     /* deprecated format */
    else if (strcmp(str, "URC") == 0)
        /*
         * "URC" specified by user is treated as "URC2".
         */
        fmt = GT3_FMT_URC;
    else
        fmt = GT3_format(str);

    if (fmt < 0)
        return -1;

    GT3_format_string(dfmt, fmt);
    return fmt;
}


/*
 * GT3_write() writes data into a stream.
 *
 *  ptr:    a pointer to data.
 *  type:   element type (GT3_TYPE_FLOAT or GT3_TYPE_DOUBLE)
 *  nx:     data length for X-dimension.
 *  ny:     data length fo  Y-dimension.
 *  nz:     data length for Z-dimension.
 *  headin: a pointer to header.
 *  dfmt:   format name (if NULL is specified, UR4 or UR8  is selected)
 */
int
GT3_write(const void *ptr, int type,
          int nx, int ny, int nz,
          const GT3_HEADER *headin, const char *dfmt, FILE *fp)
{
    char fmtstr[17];
    const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
    const char *aend[] = { "AEND1", "AEND2", "AEND3" };
    int str, end, i, dim[3];
    GT3_HEADER head;
    int fmt, rval = -1;
    double miss = -999.0;       /* -999.0: default value */
    size_t asize, zsize;
    unsigned nbits;

    /*
     * check passed arguments.
     */
    if (ptr == NULL) {
        gt3_error(GT3_ERR_CALL, "GT3_write(): Null pointer passed");
        return -1;
    }
    if (nx < 1 || ny < 1 || nz < 1) {
        gt3_error(GT3_ERR_CALL,
                  "GT3_write(): Invalid data shape (%d, %d, %d)",
                  nx, ny, nz);
        return -1;
    }
    if (type != GT3_TYPE_DOUBLE && type != GT3_TYPE_FLOAT) {
        gt3_error(GT3_ERR_CALL, "GT3_write(): Unknown datatype");
        return -1;
    }

    if (!dfmt) {
        if (type == GT3_TYPE_FLOAT) {
            fmt = GT3_FMT_UR4;
            strcpy(fmtstr, "UR4");
        } else {
            fmt = GT3_FMT_UR8;
            strcpy(fmtstr, "UR8");
        }
    } else
        if ((fmt = GT3_output_format(fmtstr, dfmt)) < 0) {
            gt3_error(GT3_ERR_CALL,
                      "GT3_write(): \"%s\" unknown format", dfmt);
            return -1;
        }

    /*
     * copy the gtool3-header and modify it.
     */
    GT3_copyHeader(&head, headin);
    GT3_setHeaderString(&head, "DFMT", fmtstr);
    GT3_setHeaderInt(&head, "SIZE", nx * ny * nz);

    /*
     * set "AEND1", "AEND2", and "AEND3".
     * "ASTR[1-3]" is determined by 'headin'.
     */
    dim[0] = nx;
    dim[1] = ny;
    dim[2] = nz;
    for (i = 0; i < 3; i++) {
        if (GT3_decodeHeaderInt(&str, &head, astr[i]) < 0) {
            str = 1;
            GT3_setHeaderInt(&head, astr[i], str);
        }
        end = str - 1 + dim[i];
        GT3_setHeaderInt(&head, aend[i], end);
    }

    /*
     * write gtool header.
     */
    if (write_bytes_into_record(&head.h, GT3_HEADER_SIZE, fp) < 0)
        return -1;

    /*
     * write data-body.
     */
    zsize = nx * ny;
    asize = zsize * nz;
    GT3_decodeHeaderDouble(&miss, &head, "MISS");
    nbits = (unsigned)fmt >> GT3_FMT_MBIT;

    if (type == GT3_TYPE_DOUBLE)
        switch (fmt & GT3_FMT_MASK) {
        case GT3_FMT_UR4:
            rval = write_ur4_via_double(ptr, asize, fp);
            break;
        case GT3_FMT_URC:
            rval = write_urc_via_double(ptr, zsize, nz, miss,
                                        urc2_packing, fp);
            break;
        case GT3_FMT_URC1:
            rval = write_urc_via_double(ptr, zsize, nz, miss,
                                        urc1_packing, fp);
            break;
        case GT3_FMT_UR8:
            rval = write_ur8_via_double(ptr, asize, fp);
            break;
        case GT3_FMT_URX:
            rval = write_urx_via_double(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_MR4:
            rval = write_mr4_via_double(ptr, asize, miss, fp);
            break;
        case GT3_FMT_MR8:
            rval = write_mr8_via_double(ptr, asize, miss, fp);
            break;
        case GT3_FMT_MRX:
            rval = write_mrx_via_double(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_URY:
            rval = write_ury_via_double(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_MRY:
            rval = write_mry_via_double(ptr, zsize, nz, nbits, miss, fp);
            break;
        }
    else
        switch (fmt & GT3_FMT_MASK) {
        case GT3_FMT_UR4:
            rval = write_ur4_via_float(ptr, asize, fp);
            break;
        case GT3_FMT_URC:
            rval = write_urc_via_float(ptr, zsize, nz, miss,
                                       urc2_packing, fp);
            break;
        case GT3_FMT_URC1:
            rval = write_urc_via_float(ptr, zsize, nz, miss,
                                       urc1_packing, fp);
            break;
        case GT3_FMT_UR8:
            rval = write_ur8_via_float(ptr, asize, fp);
            break;
        case GT3_FMT_URX:
            rval = write_urx_via_float(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_MR4:
            rval = write_mr4_via_float(ptr, asize, miss, fp);
            break;
        case GT3_FMT_MR8:
            rval = write_mr8_via_float(ptr, asize, miss, fp);
            break;
        case GT3_FMT_MRX:
            rval = write_mrx_via_float(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_URY:
            rval = write_ury_via_float(ptr, zsize, nz, nbits, miss, fp);
            break;
        case GT3_FMT_MRY:
            rval = write_mry_via_float(ptr, zsize, nz, nbits, miss, fp);
            break;
        }

    fflush(fp);
    return rval;
}


/*
 * Write data with specified bit-packing parameters (offset and scale).
 *
 * Output format will be URY?? or MRY??.
 */
int
GT3_write_bitpack(const void *ptr, int type,
                  int nx, int ny, int nz,
                  const GT3_HEADER *headin,
                  double offset, double scale,
                  unsigned nbits, unsigned is_mask,
                  FILE *fp)
{
    GT3_HEADER head;
    double miss = -999.0;
    char dfmt[17];
    int i, str, end, dim[3];
    const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
    const char *aend[] = { "AEND1", "AEND2", "AEND3" };
    /* a pointer to write_{ury,mry}_man_via_{float,double} */
    typedef int (*WWS_FUNC)(const void *ptr,
                            size_t zelem, size_t nz,
                            unsigned nbits, double miss,
                            double offset, double scale,
                            FILE *fp);
    WWS_FUNC functab[] = {
        write_ury_man_via_float,
        write_mry_man_via_float,
        write_ury_man_via_double,
        write_mry_man_via_double
    };
    unsigned func;
    WWS_FUNC write_func;

    /*
     * check passed arguments.
     */
    if (ptr == NULL) {
        gt3_error(GT3_ERR_CALL, "GT3_write_bitpack(): Null pointer passed");
        return -1;
    }
    if (nx < 1 || ny < 1 || nz < 1) {
        gt3_error(GT3_ERR_CALL,
                  "GT3_write_bitpack(): Invalid data shape (%d, %d, %d)",
                  nx, ny, nz);
        return -1;
    }
    if (type != GT3_TYPE_DOUBLE && type != GT3_TYPE_FLOAT) {
        gt3_error(GT3_ERR_CALL, "GT3_write_bitpack(): Unknown datatype");
        return -1;
    }
    if (nbits > 31) {
        gt3_error(GT3_ERR_CALL,
                  "GT3_write_bitpack(): nbits should be less than 32");
        return -1;
    }

    /*
     * update meta data.
     */
    GT3_copyHeader(&head, headin);
    snprintf(dfmt, sizeof dfmt, "%cRY%02u", is_mask ? 'M' : 'U', nbits);
    GT3_setHeaderString(&head, "DFMT", dfmt);
    GT3_setHeaderInt(&head, "SIZE", nx * ny * nz);

    /* set "AEND1", "AEND2", and "AEND3". */
    dim[0] = nx;
    dim[1] = ny;
    dim[2] = nz;
    for (i = 0; i < 3; i++) {
        if (GT3_decodeHeaderInt(&str, &head, astr[i]) < 0) {
            str = 1;
            GT3_setHeaderInt(&head, astr[i], str);
        }
        end = str - 1 + dim[i];
        GT3_setHeaderInt(&head, aend[i], end);
    }

    /*
     * write gtool header.
     */
    if (write_bytes_into_record(&head.h, GT3_HEADER_SIZE, fp) < 0)
        return -1;

    /*
     * select a function to be invoked.
     */
    func = (is_mask ? 1 : 0) | (type == GT3_TYPE_DOUBLE) << 1;
    assert(func < 4);
    write_func = functab[func];

    GT3_decodeHeaderDouble(&miss, &head, "MISS");
    return write_func(ptr, nx * ny, nz, nbits, miss,
                      offset, scale, fp);
}


#ifdef TEST_MAIN
int
main(int argc, char **argv)
{
    char dfmt[17];
    int fmt;

    assert(sizeof(float) == 4);
    assert(sizeof(double) == 8);
    assert(sizeof(fort_size_t) == 4);

    fmt = GT3_output_format(dfmt, "URC");
    assert(fmt == GT3_FMT_URC);
    assert(strcmp(dfmt, "URC2") == 0);

    fmt = GT3_output_format(dfmt, "URC1");
    assert(fmt == GT3_FMT_URC1);
    assert(strcmp(dfmt, "URC") == 0);

    fmt = GT3_output_format(dfmt, "MR8");
    assert(fmt == GT3_FMT_MR8);
    assert(strcmp(dfmt, "MR8") == 0);

    fmt = GT3_output_format(dfmt, "URX12");
    assert((fmt & GT3_FMT_MASK) == GT3_FMT_URX);
    assert((fmt >> GT3_FMT_MBIT) == 12);
    assert(strcmp(dfmt, "URX12") == 0);

    return 0;
}
#endif
