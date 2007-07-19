/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtmkax.c -- make builtin-axis files.
 */
#include "internal.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"

#define PROGNAME "ngtmkax"


static void
myperror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (errno != 0) {
		fprintf(stderr, "%s:", PROGNAME);
		if (fmt) {
			vfprintf(stderr, fmt, ap);
			fprintf(stderr, ":");
		}
		fprintf(stderr, " %s\n", strerror(errno));
	}
	va_end(ap);
}


int
make_axisfile(const char *name, const char *outdir, const char *fmt)
{
	char path[PATH_MAX + 1];
	GT3_Dim *dim;
	FILE *fp;

	if ((dim = GT3_getBuiltinDim(name)) == NULL) {
		if (GT3_ErrorCount() > 0)
			GT3_printErrorMessages(stderr);
		else
			fprintf(stderr, "%s: %s: Not a Built-in axisname\n",
					PROGNAME, name);
		return -1;
	}

	/*
	 *  write GTAXLOC.*
	 */
	snprintf(path, sizeof path, "%s/GTAXLOC.%s", outdir, name);
	if ((fp = fopen(path, "wb")) == NULL) {
		myperror(path);
		return -1;
	}

	if (GT3_writeDimFile(fp, dim, fmt) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	fclose(fp);

	/*
	 *  write GTAXWGT.*
	 */
	snprintf(path, sizeof path, "%s/GTAXWGT.%s", outdir, name);
	if ((fp = fopen(path, "wb")) == NULL) {
		myperror(path);
		return -1;
	}
	if (GT3_writeWeightFile(fp, dim, fmt) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	fclose(fp);

	GT3_freeDim(dim);
	return 0;
}


void
usage(void)
{
	const char *usage_message =
		"Usage: " PROGNAME " [options] AXISNAME...\n"
		"\n"
		"Output grid information files for GLON*, GGLA*, and GLAT*.\n"
		"\n"
		"Options:\n"
		"    -h        print help message\n"
		"    -f        specify gtool3 format (default is UR4)\n"
		"    -o        specify output directory (default is .)\n"
		"\n"
		"Example:\n"
		"  " PROGNAME " -o ~/myaixs GGLA64 GGLA64I GGLA128 GGLA64x2\n";

	fprintf(stderr, "%s\n", GT3_version());
	fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
	int ch, rval;
	const char *outdir = ".";
	const char *fmt = "UR4";


	while ((ch = getopt(argc, argv, "f:ho:")) != -1)
		switch (ch) {
		case 'f':
			fmt = strdup(optarg);
			break;

		case 'o':
			outdir = strdup(optarg);
			if (strlen(outdir) > PATH_MAX - 32) {
				fprintf(stderr, "%s: %s\n",
						PROGNAME,
						"The path specified with o-option is too long");
				exit(1);
			}
			break;

		case 'h':
		default:
			usage();
			exit(1);
			break;
		}

	argc -= optind;
	argv += optind;
	rval = 0;
	for (;argc > 0 && *argv; argc--, argv++)
		if (make_axisfile(*argv, outdir, fmt) < 0)
			rval = 1;

	return rval;
}
