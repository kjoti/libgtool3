/*
 * urc_pack.c -- gtool URC format pack/unpack routine.
 */
#include "internal.h"

#include <assert.h>
#include <math.h>


#ifndef HAVE_ILOGB
#  define ilogb(x)    ((int)floor(log10(x) / log10(2.)))
#endif

#ifndef HAVE_SCALBN
#  define scalbn(x,n) ((x) * pow(2., (n)))
#endif

#define pow2(n) scalbn(1.0, (n))
#define irint(x) (unsigned)(rint(x))  /* rounding */

#define IMISS     65534U
#define MAX_AVAIL 65533U


static void
scalefac(double rmin, double rmax,
         double *pfac_e, double *pfac_d, int *pne, int *pnd)
{
    double r, rdelta = HUGE_VAL;
    double fac_e, fac;
    int ne, nd;

    for (nd = -16, fac = 1e-16; nd < 17; nd++, fac *= 10.) {
        ne = ilogb((rmax - rmin) * fac / MAX_AVAIL) + 1;

        fac_e = pow2(ne);
        r = fac_e / fac;
        if (r < rdelta) {
            rdelta = r;
            *pfac_e = fac_e;
            *pfac_d = fac;
            *pne = ne;
            *pnd = nd;
        }
    }
    assert(rmin + MAX_AVAIL * (*pfac_e) / (*pfac_d) >= rmax);
}


/*
 * calculate three parameters for URC-packing.
 */
void
calc_urc_param(const float *data, size_t len, double miss,
               double *prmin, double *pfac_e, double *pfac_d,
               int *pne, int *pnd)
{
    double rmin = HUGE_VAL, rmax = -HUGE_VAL;
    double fac_e = HUGE_VAL, fac_d = 1.0;
    int ne = IMISS, nd = 0;
    float vmiss = (float)miss;
    size_t i;

    for (i = 0; i < len; i++) {
        if (data[i] == vmiss)
            continue;

        if (data[i] < rmin)
            rmin = data[i];

        if (data[i] > rmax)
            rmax = data[i];
    }
    if (rmax - rmin > 0.0)
        scalefac(rmin, rmax, &fac_e, &fac_d, &ne, &nd);

    /* URC-packing parameters */
    *prmin = rmin;
    *pfac_e = fac_e;
    *pfac_d = fac_d;
    *pne = ne;
    *pnd = nd;
}


/*
 * packing data in URC version1 (deprecated).
 */
void
urc1_packing(uint32_t *packed,
             const float *data, int len, double miss,
             double rmin, double fac_e, double fac_d)
{
    uint32_t ihigh, ilow;
    float vmiss = (float)miss;
    int i;

    for (i = 0; i < len / 2; i++) {
        ihigh = (data[2*i] != vmiss)
            ? (uint32_t)(fac_d * (data[2*i]   - rmin) / fac_e) : IMISS;
        ilow  = (data[2*i+1] != vmiss)
            ? (uint32_t)(fac_d * (data[2*i+1] - rmin) / fac_e) : IMISS;

        packed[i] = (ihigh << 16) | ilow;
    }
    if (len % 2) {
        i = len / 2;
        ihigh = (data[2*i] != vmiss)
            ? (uint32_t)(fac_d * (data[2*i]   - rmin) / fac_e) : IMISS;

        packed[i] = ihigh << 16;
    }
}


/*
 * packing data in URC version2.
 */
void
urc2_packing(uint32_t *packed,
             const float *data, int len, double miss,
             double rmin, double fac_e, double fac_d)
{
    uint32_t ihigh, ilow;
    float vmiss = (float)miss;
    int i;

    for (i = 0; i < len / 2; i++) {
        ihigh = (data[2*i] != vmiss)
            ? irint(fac_d * (data[2*i]   - rmin) / fac_e) : IMISS;
        ilow  = (data[2*i+1] != vmiss)
            ? irint(fac_d * (data[2*i+1] - rmin) / fac_e) : IMISS;

        packed[i] = (ihigh << 16) | ilow;
    }
    if (len % 2) {
        i = len / 2;
        ihigh = (data[2*i] != vmiss)
            ? irint(fac_d * (data[2*i]   - rmin) / fac_e) : IMISS;

        packed[i] = ihigh << 16;
    }
}


void
urc1_pack(const float *data, int len, double miss,
          uint32_t *packed, double *pref, int *pne, int *pnd)
{
    double rmin, fac_e, fac_d;
    int nd, ne;

    calc_urc_param(data, len, miss, &rmin, &fac_e, &fac_d, &ne, &nd);
    urc1_packing(packed, data, len, miss, rmin, fac_e, fac_d);

    *pref = rmin * fac_d;
    *pne = ne;
    *pnd = nd;
}


void
urc2_pack(const float *data, int len, double miss,
          uint32_t *packed, double *pref, int *pne, int *pnd)
{
    double rmin, fac_e, fac_d;
    int nd, ne;

    calc_urc_param(data, len, miss, &rmin, &fac_e, &fac_d, &ne, &nd);
    urc2_packing(packed, data, len, miss, rmin, fac_e, fac_d);

    *pref = rmin * fac_d;
    *pne = ne;
    *pnd = nd;
}


/*
 * unpack data in URC version 1 (obsoleted).
 */
void
urc1_unpack(const uint32_t *packed, int packed_len,
            double ref, int ne, int nd,
            double miss, float *data)
{
    int i;
    uint32_t ihigh, ilow;
    double base = 0.0, scal = 1.0;
    float vmiss = (float)miss;

    if (ne != IMISS) {
        base = pow2(ne);
        scal = pow(10., -nd);
    }

    if (ref != 0.0) {
        for (i = 0; i < packed_len; i++) {
            ihigh = packed[i] >> 16;
            ilow  = packed[i] & 0xffff;

            data[2*i]   = (ihigh != IMISS)
                ? (ref + (ihigh + 0.5) * base) * scal : vmiss;
            data[2*i+1] = (ilow  != IMISS)
                ? (ref + (ilow  + 0.5) * base) * scal : vmiss;
        }
    } else {
        double dhigh, dlow;

        for (i = 0; i < packed_len; i++) {
            ihigh = packed[i] >> 16;
            ilow  = packed[i] & 0xffff;

            dhigh = ihigh == 0 ? (double)ihigh : ihigh + 0.5;
            dlow  = ilow  == 0 ? (double)ilow  : ilow  + 0.5;

            data[2*i]   = (ihigh != IMISS) ? dhigh * base * scal : vmiss;
            data[2*i+1] = (ilow  != IMISS) ? dlow  * base * scal : vmiss;
        }
    }
}


/*
 * unpack data in URC version 2.
 */
void
urc2_unpack(const uint32_t *packed, int packed_len,
            double ref, int ne, int nd,
            double miss, float *data)
{
    int i;
    uint32_t ihigh, ilow;
    double base = 0.0, scal = 1.0;
    float vmiss = (float)miss;

    if (ne != IMISS) {
        base = pow2(ne);
        scal = pow(10., -nd);
    }
    for (i = 0; i < packed_len; i++) {
        ihigh = packed[i] >> 16;
        ilow  = packed[i] & 0xffff;

        data[2*i]   = (ihigh != IMISS) ? (ref + ihigh * base) * scal : vmiss;
        data[2*i+1] = (ilow  != IMISS) ? (ref + ilow  * base) * scal : vmiss;
    }
}


#ifdef TEST_MAIN
#include <assert.h>
#include <stdio.h>

#define PACK   urc2_pack
#define UNPACK urc2_unpack


void
print_data(const float *data, const float *data2, int len)
{
    int i;

    printf("%16s %16s %16s\n", "(orignal)", "(unpacked)", "(err)");
    for (i = 0; i < len; i++)
        printf("%16.8e %16.8e %16.8e\n",
               data[i], data2[i], data2[i] - data[i]);

}


int
main(int argc, char **argv)
{
    float data[6], data2[6];
    int ne, nd;
    uint32_t packed[6];
    double ref;
    float vmiss = -999.0f;
    float cval  = 1.23456789f;
    double err;

    /* test 1 */
    data[0] = data[1] = data[2] = data[3] = cval;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    assert(data2[0] == cval);
    assert(data2[1] == cval);
    assert(data2[2] == cval);
    assert(data2[3] == cval);

    /* test 2 */
    data[0] = data[1] = data[2] = data[3] = vmiss;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    assert(data2[0] == vmiss);
    assert(data2[1] == vmiss);
    assert(data2[2] == vmiss);
    assert(data2[3] == vmiss);

    /* test 3 */
    data[0] = data[2] = vmiss;
    data[1] = data[3] = cval;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    assert(data2[0] == vmiss);
    assert(data2[1] == cval);
    assert(data2[2] == vmiss);
    assert(data2[3] == cval);

    /* test 3-2 */
    data[0] = data[2] = cval;
    data[1] = data[3] = vmiss;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    assert(data2[0] == cval);
    assert(data2[1] == vmiss);
    assert(data2[2] == cval);
    assert(data2[3] == vmiss);

    /* test 4 */
    data[0] = 0.;
    data[1] = 0.123456789;
    data[2] = 0.987654321;
    data[3] = 0.999999;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    err = 1.0 / 65000.0;
    print_data(data, data2, 4);
    assert(fabs(data2[0] - data[0]) <= err);
    assert(fabs(data2[1] - data[1]) <= err);
    assert(fabs(data2[2] - data[2]) <= err);
    assert(fabs(data2[3] - data[3]) <= err);

    /* test 5 */
    data[0] = 0.123456789;
    data[1] = 0.234567891;
    data[2] = 0.345678912;
    data[3] = -0.123456789;

    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    err = 0.25 / 65000.0;
    print_data(data, data2, 4);
    assert(fabs(data2[0] - data[0]) <= err);
    assert(fabs(data2[1] - data[1]) <= err);
    assert(fabs(data2[2] - data[2]) <= err);
    assert(fabs(data2[3] - data[3]) <= err);

    /* test 6 */
    data[0] = 0.1234567;
    data[1] = 0.1234568;
    data[2] = 0.1234569;
    data[3] = 0.1234570;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    err = 0.1 / 65000.;
    print_data(data, data2, 4);
    assert(fabs(data2[0] - data[0]) <= err);
    assert(fabs(data2[1] - data[1]) <= err);
    assert(fabs(data2[2] - data[2]) <= err);
    assert(fabs(data2[3] - data[3]) <= err);

    /* test 7 */
    data[0] = 1e1;
    data[1] = 1e2;
    data[2] = 1e4;
    data[3] = 1e6;
    PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 2, ref, ne, nd, vmiss, data2);

    err = 1e6 / 65000.;
    print_data(data, data2, 4);
    assert(fabs(data2[0] - data[0]) <= err);
    assert(fabs(data2[1] - data[1]) <= err);
    assert(fabs(data2[2] - data[2]) <= err);
    assert(fabs(data2[3] - data[3]) <= err);

    /* test 8 */
    data[0] = 0.12345678;
    data[1] = 0.12345679;
    data[2] = 0.12345680;
    data[3] = 0.12345681;
    data[4] = vmiss;
    data[5] = 1e4;              /* will be ignored */

    PACK(data, 5, vmiss, packed, &ref, &ne, &nd);
    UNPACK(packed, 3, ref, ne, nd, vmiss, data2);

    err = 1e0 / 65000.;
    print_data(data, data2, 5);
    assert(fabs(data2[0] - data[0]) <= err);
    assert(fabs(data2[1] - data[1]) <= err);
    assert(fabs(data2[2] - data[2]) <= err);
    assert(fabs(data2[3] - data[3]) <= err);
    assert(data[4]  == vmiss);

    /* test 9 (this test can be passed in only URC2) */
    {
        double ref2;
        int ne2, nd2;

        data[0] = -1.23456789e-4;
        data[1] = -2.34567891e-2;
        data[2] = 3.456789123e+1;
        data[3] = 4.567891234e+2;
        PACK(data, 4, vmiss, packed, &ref, &ne, &nd);
        UNPACK(packed, 2, ref, ne, nd, vmiss, data2);
        print_data(data, data2, 4);

        PACK(data2, 4, vmiss, packed, &ref2, &ne2, &nd2);
        UNPACK(packed, 2, ref2, ne2, nd2, vmiss, data);

        print_data(data2, data, 4);
        assert(ref == ref2);
        assert(ne  == ne2);
        assert(nd  == nd2);
        assert(data[0] == data2[0]);
        assert(data[1] == data2[1]);
        assert(data[2] == data2[2]);
        assert(data[3] == data2[3]);
    }

    return 0;
}
#endif
