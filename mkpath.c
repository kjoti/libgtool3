/*
 * mkpath.c -- make directories recursively.
 */
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>

#include "myutils.h"

#ifdef __MINGW32__
#  include <io.h>
#  define MKDIR(path, mode) mkdir(path)
#else
#  define MKDIR(path, mode) mkdir(path, mode)
#endif


/*
 * make directories recursively like a command "mkdir -p".
 */
int
mkpath(const char *path)
{
    struct stat sb;
    char buf[PATH_MAX];
    char *dest, *tail, prev;

    if (path == NULL || path[0] == '\0')
        return 0;

    dest = buf;
    tail = buf + sizeof buf - 1;

    for (prev = ' '; prev != '\0' && dest < tail; path++) {
        /* skip adjacent slashes. */
        if (prev == '/' && *path == '/')
            continue;

        if (*path == '/' || (prev != '/' && *path == '\0')) {
            *dest = '\0';
            if (buf[0] != '\0' && MKDIR(buf, 0777) < 0) {
                if ((errno == EEXIST || errno == EISDIR || errno == EACCES)
                    && stat(buf, &sb) == 0
                    && S_ISDIR(sb.st_mode))
                    errno = 0;
                else
                    return -1;
            }
        }
        *dest++ = prev = *path;
    }
    if (prev != '\0') {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}
