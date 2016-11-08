/*
 * ngtmkax.c -- make builtin-axis files.
 */
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "logging.h"
#include "myutils.h"

#define PROGNAME "ngtmkax"


int
make_axisfile(const char *name, const char *outdir, const char *fmt)
{
    char path[PATH_MAX];
    GT3_Dim *dim;
    FILE *fp;
    int splen;

    if ((dim = GT3_getBuiltinDim(name)) == NULL) {
        if (GT3_ErrorCount() > 0)
            GT3_printErrorMessages(stderr);
        else
            logging(LOG_ERR, "%s: Not a Built-in axisname", name);
        return -1;
    }

    /*
     * write GTAXLOC.*
     */
    splen = snprintf(path, sizeof path, "%s/GTAXLOC.%s", outdir, name);
    if (splen >= sizeof path) {
        logging(LOG_ERR, "%s: Too long path (truncated)", path);
        return -1;
    }
    if ((fp = fopen(path, "wb")) == NULL) {
        logging(LOG_SYSERR, path);
        return -1;
    }

    if (GT3_writeDimFile(fp, dim, fmt) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    fclose(fp);

    /*
     * write GTAXWGT.*
     */
    snprintf(path, sizeof path, "%s/GTAXWGT.%s", outdir, name);
    if ((fp = fopen(path, "wb")) == NULL) {
        logging(LOG_SYSERR, path);
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
        "    -f        specify gtool3 format (default is UR8)\n"
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
    char dummy[17];
    char *fmt = "UR8";

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "f:ho:")) != -1)
        switch (ch) {
        case 'f':
            toupper_string(optarg);
            if (GT3_output_format(dummy, optarg) < 0) {
                logging(LOG_ERR, "%s: Unknown format", optarg);
                exit(1);
            }
            fmt = optarg;
            break;

        case 'o':
            outdir = strdup(optarg);
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
