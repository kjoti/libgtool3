/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  logging.c
 */
#include <errno.h>
#include <string.h>
#include "logging.h"

static int low_level = LOG_NOTICE;
static FILE *output = NULL;
static int  needtoclose = 0;
static char prefix[32] = "";

static void
default_prefix_func(FILE *fp, int type)
{
	const char *marks[] = {
		"",
		"NOTICE: ",
		"WARN: ",
		"ERROR: ",
		"ERROR: "
	};

	fputs(prefix, fp);
	if (type >= 0 && type < sizeof marks / sizeof marks[0])
		fputs(marks[type], fp);
}

static void (*prefix_func)(FILE *fp, int type) = default_prefix_func;

static void
set_logging_name(const char *name)
{
	if (name)
		snprintf(prefix, sizeof prefix, "%s: ", name);
	else
		prefix[0] = '\0';
}


void
set_logging_prefix_func(void (*func)(FILE *, int))
{
	prefix_func = func ? func : default_prefix_func;
}


void
close_logging(void)
{
	if (needtoclose)
		fclose(output);
	output = NULL;
	needtoclose = 0;
}


void
open_logging(FILE *fp, const char *name)
{
	close_logging();

	output = fp;
	needtoclose = 0;
	set_logging_name(name);
}


int
open_logfile(const char *path, const char *name, int append)
{
	FILE *fp;

	close_logging();

	if ((fp = fopen(path, append ? "a" : "w")) == NULL)
		return -1;

	output = fp;
	needtoclose = 1;
	set_logging_name(name);
	return 0;
}


void
set_logging_level(const char *str)
{
	struct { const char *key; int value; } tab[] = {
		{ "verbose", LOG_INFO },
		{ "normal",  LOG_NOTICE },
		{ "quiet",   LOG_WARN },
		{ "silent",  LOG_ERR }
	};
	int i;

	for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
		if (strcmp(str, tab[i].key) == 0)
			low_level = tab[i].value;
}


void
logging(int type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (output && type >= low_level) {
		prefix_func(output, type);
		if (fmt)
			vfprintf(output, fmt, ap);
		if (type == LOG_SYSERR && errno != 0)
			fprintf(output, fmt ? ": %s" : "%s", strerror(errno));
		fputc('\n', output);
	}
	va_end(ap);
}
