/*
 * talloc.c
 */
#include "talloc.h"


/*
 * usage:
 *
 * int
 * foo(int len, ...)
 * {
 *     int buf[16];
 *     int *bufp;
 *
 *     bufp = tiny_alloc(buf, sizeof buf, sizeof(int) * len);
 *     if (bufp == NULL) {
 *         perrro(NULL);
 *
 *     }
 *     do_something(bufp);
 *
 *     tiny_free(bufp, buf);
 *     return ...;
 * }
 */
void *
tiny_alloc(void *tiny, size_t tiny_size, size_t size)
{
    return (tiny_size >= size) ? tiny : malloc(size);
}


void
tiny_free(void *ptr, const void *ref)
{
    if (ptr != ref)
        free(ptr);
}
