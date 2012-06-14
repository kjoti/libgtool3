#include "internal.h"

#include "gtool3.h"

static const char *version = "libgtool3 0.20.0 rc1";

char *
GT3_version(void)
{
    return (char *)version;
}
