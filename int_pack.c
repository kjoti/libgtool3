/*
 * int_pack.c -- pack N-bit integers into a 32-bit integer array.
 */
#include <assert.h>

#include "int_pack.h"

#define BWIDTH 32U


size_t
pack32_len(size_t siz, unsigned nbit)
{
    /* len = (nbit * siz + BWIDTH - 1) / BWIDTH; */
    size_t n = siz / BWIDTH; /* to avoid overflow */

    siz -= n * BWIDTH;
    return nbit * n + (nbit * siz + BWIDTH - 1) / BWIDTH;
}


/*
 * pack N-bit integers into a 32-bit integers array.
 * 'N' is between 1 and 31.
 *
 * retrun the length of packed array.
 */
size_t
pack_bits_into32(uint32_t *packed,
                 const unsigned *data, size_t nelem,
                 unsigned nbit)
{
    unsigned off;
    unsigned value, mask;
    uint32_t *ptr;
    size_t i, len;


    assert(nbit > 0 && nbit < 32);

    mask = (1U << nbit) - 1U;
    len = pack32_len(nelem, nbit);

    for (i = 0; i < len; i++)
        packed[i] = 0;          /* clear */

    for (i = 0, ptr = packed, off = 0; i < nelem; i++, off += nbit) {
        if (off > BWIDTH) {
            off -= BWIDTH;
            ptr++;
        }

        value = data[i] & mask;

        if (BWIDTH < off + nbit) {
            *ptr     |= value >> (off + nbit - BWIDTH);
            *(ptr+1) |= value << (2 * BWIDTH - off - nbit);
        } else
            *ptr     |= value << (BWIDTH - off - nbit);
    }
    return len;
}


void
unpack_bits_from32(unsigned *data,
                   size_t len,
                   const uint32_t *packed, unsigned nbit)
{
    unsigned i, i2, i3;
    unsigned ipos, off;
    unsigned value;
    unsigned mask;

    assert(nbit > 0 && nbit < 32);
    mask = (1U << nbit) - 1U;

    for (i = 0; i < len; i++) {
        i2 = i / BWIDTH;
        i3 = i % BWIDTH;

        ipos = nbit * i2 + nbit * i3 / BWIDTH;
        off = nbit + (nbit * i3) % BWIDTH;

        if (off > BWIDTH)
            value = ((packed[ipos] << (off - BWIDTH)) & mask)
                | ((packed[ipos+1] >> (2 * BWIDTH - off)) & mask);
        else
            value = (packed[ipos] >> (BWIDTH - off)) & mask;

        data[i] = value;
    }
}


/*
 * pack boolean flags (stored in an array of unsigned char)
 * into an 32-bit unsigned integer array.
 */
size_t
pack_bools_into32(uint32_t *packed,
                  const unsigned char *flags, size_t nelems)
{
    size_t i, num;
    unsigned extra;

    num = nelems >> 5;
    extra = nelems & 0x1f;
    for (i = 0; i < num; i++, flags += 32)
        packed[i] = (flags[0] & 1U) << 31U
            | (flags[1] & 1U) << 30U
            | (flags[2] & 1U) << 29U
            | (flags[3] & 1U) << 28U
            | (flags[4] & 1U) << 27U
            | (flags[5] & 1U) << 26U
            | (flags[6] & 1U) << 25U
            | (flags[7] & 1U) << 24U
            | (flags[8] & 1U) << 23U
            | (flags[9] & 1U) << 22U
            | (flags[10] & 1U) << 21U
            | (flags[11] & 1U) << 20U
            | (flags[12] & 1U) << 19U
            | (flags[13] & 1U) << 18U
            | (flags[14] & 1U) << 17U
            | (flags[15] & 1U) << 16U
            | (flags[16] & 1U) << 15U
            | (flags[17] & 1U) << 14U
            | (flags[18] & 1U) << 13U
            | (flags[19] & 1U) << 12U
            | (flags[20] & 1U) << 11U
            | (flags[21] & 1U) << 10U
            | (flags[22] & 1U) << 9U
            | (flags[23] & 1U) << 8U
            | (flags[24] & 1U) << 7U
            | (flags[25] & 1U) << 6U
            | (flags[26] & 1U) << 5U
            | (flags[27] & 1U) << 4U
            | (flags[28] & 1U) << 3U
            | (flags[29] & 1U) << 2U
            | (flags[30] & 1U) << 1U
            | (flags[31] & 1U);

    if (extra) {
        packed[num] = 0;
        for (i = 0; i < extra; i++)
            packed[num] |= (flags[i] & 1U) << (31U - i);

        num++;
    }
    return num;
}


#ifdef TEST_MAIN
#include <stdio.h>
void
test0(void)
{
    int nbit;
    int nelem, len;

    for (nbit = 1; nbit < 32; nbit++)
        for (nelem = 0; nelem < 100; nelem++) {
            len = pack32_len(nelem, nbit);

            assert(len * 32 >= nelem * nbit);
            assert((len - 1) * 32 < nelem * nbit);
        }
}


void
test(void)
{
    uint32_t packed[9];
    unsigned data[9];
    size_t len;
    unsigned nbit;

    /*
     * 16-bit packing.
     */
    nbit = 16;
    data[0] = 0xffff;
    data[1] = 0xeeee;
    data[2] = 0xdddd;
    data[3] = 0xcccc;

    len = pack_bits_into32(packed, data, 4, nbit);
    assert(len == 2);
    assert(packed[0] == 0xffffeeee);
    assert(packed[1] == 0xddddcccc);

    /*
     * 16-bit packing(2).
     */
    nbit = 16;
    data[0] = 0xfedc;
    data[1] = 0xba98;
    data[2] = 0x7654;
    data[3] = 0x3210;

    len = pack_bits_into32(packed, data, 4, nbit);
    assert(len == 2);
    assert(packed[0] == 0xfedcba98);
    assert(packed[1] == 0x76543210);

    /*
     * 12-bit packing
     */
    nbit = 12;
    data[0] = 0xfed;
    data[1] = 0xcba;
    data[2] = 0x987;
    data[3] = 0x654;
    data[4] = 0x321;
    data[5] = 0x012;
    data[6] = 0x345;
    data[7] = 0x678;
    data[8] = 0x9ab;

    len = pack_bits_into32(packed, data, 8, nbit);
    assert(len == 3);
    assert(packed[0] == 0xfedcba98);
    assert(packed[1] == 0x76543210);
    assert(packed[2] == 0x12345678);

    len = pack_bits_into32(packed, data, 9, nbit);
    assert(len == 4);
    assert(packed[3] == 0x9ab00000);

    /*
     * 4-bit packing
     */
    nbit = 4;
    data[0] = 0xf;
    data[1] = 0xf;
    data[2] = 0xe;
    data[3] = 0xf;
    data[4] = 0xc;
    data[5] = 0xf;
    data[6] = 0xd;
    data[7] = 0xf;

    len = pack_bits_into32(packed, data, 8, nbit);
    assert(len == 1);
    assert(packed[0] == 0xffefcfdf);

    /*
     * 1-bit packing
     */
    nbit = 1;
    data[0] = 1;
    data[1] = 0;
    data[2] = 1;
    data[3] = 0;
    data[4] = 0;
    data[5] = 0;
    data[6] = 1;
    data[7] = 1;
    len = pack_bits_into32(packed, data, 8, nbit);
    assert(len == 1);
    assert(packed[0] == 0xa3000000);
}


void
test2(unsigned nbit)
{
#define NELEM 4096

    unsigned data[NELEM], data2[NELEM];
    uint32_t packed[NELEM];
    int i;

    for (i = 0; i < NELEM; i++)
        data[i] = i % (1U << nbit);

    pack_bits_into32(packed, data, NELEM, nbit);
    unpack_bits_from32(data2, NELEM, packed, nbit);

    for (i = 0; i < NELEM; i++)
        assert(data[i] == data2[i]);
}


void
test3(void)
{
    {
        unsigned char flags[100];
        uint32_t packed[4];
        int i;
        size_t len;

        for (i = 0; i < 100; i++)
            flags[i] = 1;

        len = pack_bools_into32(packed, flags, 1);
        assert(len == 1);
        assert(packed[0] == 0x80000000);

        len = pack_bools_into32(packed, flags, 2);
        assert(len == 1);
        assert(packed[0] == 0xc0000000);

        len = pack_bools_into32(packed, flags, 3);
        assert(len == 1);
        assert(packed[0] == 0xe0000000);

        len = pack_bools_into32(packed, flags, 4);
        assert(len == 1);
        assert(packed[0] == 0xf0000000);

        len = pack_bools_into32(packed, flags, 5);
        assert(len == 1);
        assert(packed[0] == 0xf8000000);

        len = pack_bools_into32(packed, flags, 31);
        assert(len == 1);
        assert(packed[0] == 0xfffffffe);

        len = pack_bools_into32(packed, flags, 32);
        assert(len == 1);
        assert(packed[0] == 0xffffffff);

        len = pack_bools_into32(packed, flags, 33);
        assert(len == 2);
        assert(packed[0] == 0xffffffff);
        assert(packed[1] == 0x80000000);

        len = pack_bools_into32(packed, flags, 100);
        assert(len == 4);
        assert(packed[0] == 0xffffffff);
        assert(packed[1] == 0xffffffff);
        assert(packed[2] == 0xffffffff);
        assert(packed[3] == 0xf0000000);
    }

    {
        unsigned char flags[] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
            0, 0, 0, 0,
            1, 1, 0, 0,
            0, 0, 1, 1,
            1, 0, 0, 1, /* (1) */
            0, 0, 0, 1,
            0, 0, 1, 0,
            0, 1, 0, 0,
            1, 0, 0, 0,
            1, 1, 1, 1
        };
        uint32_t packed[2];
        size_t len;

        len = pack_bools_into32(packed, flags, sizeof flags);

        assert(len == 2);
        assert(packed[0] == 0x84210c39);
        assert(packed[1] == 0x1248f000);
    }
}


int
main(int argc, char **argv)
{
    unsigned nbit;

    test0();
    test();
    for (nbit = 1; nbit < 32; nbit++) {
        test2(nbit);
    }
    test3();
    return 0;
}
#endif /* TEST_MAIN */
