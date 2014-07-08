#include "internal.h"

#include "gtool3.h"

static const char *version = "libgtool3 1.1.0 beta";

char *
GT3_version(void)
{
    return (char *)version;
}
