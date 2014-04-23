/*
 * record.c -- Fortran unformatted record I/O for GTOOL3.
 */
#include "internal.h"

#include <sys/types.h>
#include <stdio.h>
#include <string.h>


static int
read_from_record(void *ptr, size_t skip, size_t nelem,
                 size_t size, FILE *fp)
{
    fort_size_t recsiz;         /* record size */
    off_t eor;
    size_t nelem_record;        /* # of elements in the record. */

    if (fread(&recsiz, sizeof(fort_size_t), 1, fp) != 1)
        return -1;
    if (IS_LITTLE_ENDIAN)
        reverse_words(&recsiz, 1);

    if (recsiz % size != 0)
        return -1;

    /* eor: end of record (position) */
    eor = ftello(fp) + recsiz + sizeof(fort_size_t);

    nelem_record = recsiz / size;

    if (skip > nelem_record)
        skip = nelem_record;
    if (nelem > nelem_record - skip)
        nelem = nelem_record - skip;

    if (nelem > 0) {
        if (skip != 0 && fseeko(fp, size * skip, SEEK_CUR) < 0)
            return -1;

        if (fread(ptr, size, nelem, fp) != nelem)
            return -1;
    }

    return fseeko(fp, eor, SEEK_SET);
}


/*
 * read_words_from_record() reads words from a fortran-unformatted record,
 * and stores them in 'ptr'.
 *
 * WORD: 4-byte in size.
 */
int
read_words_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp)
{
    if (read_from_record(ptr, skip, nelem, 4, fp) < 0)
        return -1;

    if (IS_LITTLE_ENDIAN)
        reverse_words(ptr, nelem);

    return 0;
}


/*
 * read_dwords_from_record() reads dwords from a fortran-unformatted record,
 * and stores them in 'ptr'.
 *
 * DWORD: 8-byte in size.
 */
int
read_dwords_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp)
{
    if (read_from_record(ptr, skip, nelem, 8, fp) < 0)
        return -1;

    if (IS_LITTLE_ENDIAN)
        reverse_dwords(ptr, nelem);

    return 0;
}


int
write_record_sep(fort_size_t size, FILE *fp)
{
    if (IS_LITTLE_ENDIAN)
        reverse_words(&size, 1);

    if (fwrite(&size, sizeof(fort_size_t), 1, fp) != 1) {
        gt3_error(SYSERR, NULL);
        return -1;
    }
    return 0;
}


/*
 * Write a record of Fortran unformatted sequential.
 */
static int
write_into_record(const void *ptr,
                  size_t size,
                  size_t nelem,
                  void *(*reverse)(void *, int),
                  FILE *fp)
{
    char data[IO_BUF_SIZE];
    const char *ptr2;
    size_t nelem2, len, maxelems;

    /* HEADER */
    if (write_record_sep(size * nelem, fp) < 0)
        return -1;

    if (IS_LITTLE_ENDIAN && size > 1) {
        ptr2 = ptr;
        maxelems = sizeof data / size;
        nelem2 = nelem;

        while (nelem2 > 0) {
            len = nelem2 > maxelems ? maxelems : nelem2;

            memcpy(data, ptr2, size * len);
            reverse(data, len);

            if (fwrite(data, size, len, fp) != len) {
                gt3_error(SYSERR, NULL);
                return -1;
            }
            ptr2 += size * len;
            nelem2 -= len;
        }
    } else
        if (fwrite(ptr, size, nelem, fp) != nelem) {
            gt3_error(SYSERR, NULL);
            return -1;
        }

    /* TRAILER */
    if (write_record_sep(size * nelem, fp) < 0)
        return -1;

    return 0;
}


int
write_words_into_record(const void *ptr, size_t nelem, FILE *fp)
{
    return write_into_record(ptr, 4, nelem, reverse_words, fp);
}


int
write_dwords_into_record(const void *ptr, size_t nelem, FILE *fp)
{
    return write_into_record(ptr, 8, nelem, reverse_dwords, fp);
}


int
write_bytes_into_record(const void *ptr, size_t nelem, FILE *fp)
{
    return write_into_record(ptr, 1, nelem, NULL, fp);
}


#ifdef TEST_MAIN
#include <assert.h>

int
main(int argc, char **argv)
{
    assert(sizeof(fort_size_t) == 4);
    return 0;
}
#endif
