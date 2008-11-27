#include "internal.h"

#include "gtool3.h"

static const char *version = "libgtool3 0.10.0";

char *
GT3_version(void)
{
	return (char *)version;
}
