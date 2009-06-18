#include "internal.h"

#include "gtool3.h"

static const char *version = "libgtool3 0.13.0 pre";

char *
GT3_version(void)
{
	return (char *)version;
}
