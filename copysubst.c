/*
 *  copysubst.c
 */
#include <string.h>

#include "myutils.h"


int
copysubst(char *dest, size_t len,
          const char *orig, const char *old, const char *new)
{
    size_t oldlen, newlen, remain;
    size_t slen;
    int overflow = 0;
    int cnt = 0;

    if (len < 1)
        return -1;

    len--; /* for null terminator */
    oldlen = strlen(old);
    newlen = strlen(new);
    remain = strlen(orig);

    while (len > 0 && remain > 0) {
        if (orig[0] == old[0] && remain >= oldlen
            && memcmp(orig, old, oldlen) == 0) {
            /* substituting */
            overflow = newlen > len;
            slen = overflow ? len : newlen;
            memcpy(dest, new, slen);

            dest += slen;
            len  -= slen;
            orig += oldlen;
            remain -= oldlen;
            cnt++;
        } else {
            *dest++ = *orig++;
            remain--;
            len--;
        }
    }
    *dest = '\0';

    return (remain > 0 || overflow) ? -1 : cnt;
}


#ifdef TEST_MAIN
#include <assert.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    char dest[17];
    int rval;

    rval = copysubst(dest, sizeof dest, "foo bar bar",
                     "bar", "SPAM");
    assert(rval == 2 && strcmp(dest, "foo SPAM SPAM") == 0);

    rval = copysubst(dest, sizeof dest, "foo bar bar foo",
                     "bar", "SPAM");
    assert(rval == -1 && strcmp(dest, "foo SPAM SPAM fo") == 0);

    rval = copysubst(dest, sizeof dest, "foo bar bar",
                     "ar", "");
    assert(rval == 2 && strcmp(dest, "foo b b") == 0);

    rval = copysubst(dest, sizeof dest, "foo bar bar",
                     "", "SPAM");
    assert(rval == 0 && strcmp(dest, "foo bar bar") == 0);

    return 0;
}
#endif
