/*
 * xfread.c
 */
#include "internal.h"
#include <sys/types.h>
#include <stdio.h>

/*
 * To distinguish I/O error and file-format error (unexpected EOF).
 */
int
xfread(void *ptr, size_t size, size_t nmemb, FILE *fp)
{
    if (fread(ptr, size, nmemb, fp) != nmemb) {
        if (feof(fp))
            gt3_error(GT3_ERR_BROKEN, "Unexpected EOF");
        else
            gt3_error(SYSERR, "I/O Error");

        return -1;
    }
    return 0;
}
