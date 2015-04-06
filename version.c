#include "internal.h"

#include "gtool3.h"

static const char *version = "libgtool3 1.3.1 rc";

char *
GT3_version(void)
{
    return (char *)version;
}
