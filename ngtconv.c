/*
 * ngtconv.c -- gtool3 format converter.
 */
#include "internal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"
#include "range.h"

#define PROGNAME "ngtconv"
#define RANGE_MAX 0x7fffffff

#ifndef max
#  define max(a, b) ((a)>(b) ? (a) :(b))
#endif
#ifndef min
#  define min(a, b) ((a)<(b) ? (a) :(b))
#endif

struct buffer {
    double *ptr;
    size_t reserved;
    size_t curr;
};

static struct range g_range[] = {
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
    { 0, RANGE_MAX }
};
static struct sequence *g_zseq = NULL;
static struct buffer g_buffer;


/*
 * Return value:
 *    -1: error has occured.
 *     0: two files are not identical.
 *     1: two files are identical.
 *
 * FIXME: Is this function portable?
 */
static int
identical_file(const char *path1, const char *path2)
{
    struct stat sb1, sb2;

    if (stat(path1, &sb1) < 0 || stat(path2, &sb2) < 0)
        return -1;

    return (sb1.st_dev == sb2.st_dev && sb1.st_ino == sb2.st_ino) ? 1 : 0;
}


static void
copy_to_buffer(struct buffer* buff, GT3_Varbuf *var, size_t off, size_t num)
{
    size_t ncopied;

    /* XXX: enough size allocated */
    assert(num <= buff->reserved - buff->curr);
    ncopied = GT3_copyVarDouble(buff->ptr + buff->curr, num, var, off, 1);
    buff->curr += ncopied;
}


static int
allocate_buffer(struct buffer *buf, size_t newsize)
{
    double *p;

    if (newsize > buf->reserved) {
        if ((p = malloc(sizeof(double) * newsize)) == NULL)
            return -1;

        free(buf->ptr);
        buf->ptr = p;
        buf->reserved = newsize;
    }
    buf->curr = 0;
    return 0;
}


/*
 * XXX: fmt has enough size.
 *
 * UR4 => MR4, UR8 => MR8, URC => MRY16, URX?? => MRY??, URY?? => MRY??
 */
static void
masked_format(char *fmt, const char *orig)
{
    if (   strcmp(orig, "URC") == 0
        || strcmp(orig, "URC2") == 0
        || strcmp(orig, "UI2") == 0) {
        strcpy(fmt, "MRY16");
    } else {
        strcpy(fmt, orig);

        fmt[0] = 'M';
        if (fmt[2] == 'X')
            fmt[2] = 'Y';
    }

    if (GT3_format(fmt) < 0) {
        GT3_clearLastError();
        fmt[0] = '\0';
    }
}


static int
conv_chunk(FILE *output, const char *dfmt, GT3_Varbuf *var, GT3_File *fp)
{
    GT3_HEADER head;
    int nx, ny, nz;
    int i, n, y, z;
    size_t offset, nelems;
    struct range range[3];
    int astr[] = { 1, 1, 1 };
    char key[17];
    char suffix[] = { '1', '2', '3' };
    char fmtsp[17];


    if (GT3_readHeader(&head, fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    for (i = 0; i < 3; i++) {
        snprintf(key, sizeof key, "ASTR%c", suffix[i]);
        if (GT3_decodeHeaderInt(astr + i, &head, key) < 0) {
            logging(LOG_WARN, "invalid %s", key);
            GT3_printLastErrorMessage(stderr);
        }

        range[i].str = max(0, g_range[i].str);
        range[i].end = min(fp->dimlen[i], g_range[i].end);
    }

    nx = range[0].end - range[0].str;
    ny = range[1].end - range[1].str;
    nz = range[2].end - range[2].str;
    if (g_zseq) {
        reinitSeq(g_zseq, 1, fp->dimlen[2]);
        nz = countSeq(g_zseq);
    }

    if (nx <= 0 || ny <= 0 || nz <= 0) {
        logging(LOG_WARN, "empty domain");
        return 0;
    }

    if (allocate_buffer(&g_buffer, nx * ny * nz) < 0) {
        logging(LOG_SYSERR, NULL);
        return -1;
    }

    for (n = 0; n < nz; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                assert(!"NOTREACHED");
            }
            z = g_zseq->curr - 1;
        } else
            z = n + range[2].str;

        if (GT3_readVarZ(var, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }

        if (range[0].str > 0 || range[0].end < fp->dimlen[0]) {
            nelems = nx;
            for (y = range[1].str; y < range[1].end; y++) {
                offset = fp->dimlen[0] * y + range[0].str;

                copy_to_buffer(&g_buffer, var, offset, nelems);
            }
        } else {
            offset = fp->dimlen[0] * range[1].str;
            nelems = nx * ny;

            copy_to_buffer(&g_buffer, var, offset, nelems);
        }
    }

    GT3_setHeaderInt(&head, "ASTR1", astr[0] + range[0].str);
    GT3_setHeaderInt(&head, "ASTR2", astr[1] + range[1].str);
    if (g_zseq) {
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
        GT3_setHeaderInt(&head, "ASTR3", 1);
    } else
        GT3_setHeaderInt(&head, "ASTR3", astr[2] + range[2].str);

    /*
     * format 'ASIS' and 'MASK' support.
     */
    fmtsp[0] = '\0';
    if (strcmp(dfmt, "ASIS") == 0)
        GT3_copyHeaderItem(fmtsp, sizeof fmtsp, &head, "DFMT");
    if (strcmp(dfmt, "MASK") == 0) {
        char orig[17];

        GT3_copyHeaderItem(orig, sizeof orig, &head, "DFMT");
        masked_format(fmtsp, orig);
    }

    if (GT3_write(g_buffer.ptr, GT3_TYPE_DOUBLE,
                  nx, ny, nz,
                  &head,
                  fmtsp[0] != '\0' ? fmtsp : dfmt,
                  output) < 0) {

        GT3_printErrorMessages(stderr);
        return -1;
    }
    return 0;
}


static int
conv_file(const char *path, const char *fmt, FILE *output,
          struct sequence *seq)
{
    GT3_File *fp;
    GT3_Varbuf *var;
    int rval, stat;

    if ((fp = GT3_open(path)) == NULL
        || (var = GT3_getVarbuf(fp)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    rval = 0;
    if (seq == NULL)
        while (!GT3_eof(fp)) {
            if (conv_chunk(output, fmt, var, fp) < 0) {
                rval = -1;
                break;
            }
            if (GT3_next(fp) < 0) {
                GT3_printErrorMessages(stderr);
                rval = -1;
                break;
            }
        }
    else
        while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
            if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
                rval = -1;
                break;
            }
            if (stat == ITER_OUTRANGE)
                continue;

            if (conv_chunk(output, fmt, var, fp) < 0) {
                rval = -1;
                break;
            }
        }

    GT3_close(fp);
    return rval;
}


static char *
toupper_string(char *str)
{
    char *p = str;

    while ((*p = toupper(*p)))
        p++;
    return str;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] inputfile [outputfile]\n"
        "\n"
        "File format converter.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        output in append mode\n"
        "    -f        specify output format (default: UR4)\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z LIST   specify Z-planes\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    int ch;
    struct sequence *tseq = NULL;
    const char *mode = "wb";
    char *fmt = NULL;
    const char *default_fmt = "UR4";
    char *outpath = "gtool.out";
    FILE *output;
    char dummy[17];

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "af:t:x:y:z:h")) != -1)
        switch (ch) {
        case 'a':
            mode = "ab";
            break;
        case 'f':
            if ((fmt = strdup(optarg)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            toupper_string(fmt);
            if (strcmp(fmt, "ASIS") != 0
                && strcmp(fmt, "MASK") != 0
                && GT3_output_format(dummy, fmt) < 0) {
                logging(LOG_ERR, "%s: Unknown format name", fmt);
                exit(1);
            }
            break;
        case 't':
            if ((tseq = initSeq(optarg, 1, RANGE_MAX)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'x':
            if (get_range(g_range, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-x: invalid argument: %s", optarg);
                exit(1);
            }
            break;
        case 'y':
            if (get_range(g_range + 1, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-y: invalid argument: %s", optarg);
                exit(1);
            }
            break;
        case 'z':
            if (get_seq_or_range(g_range + 2, &g_zseq, optarg,
                                 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-z: invalid argument: %s", optarg);
                exit(1);
            }
            break;

        case 'h':
        default:
            usage();
            exit(0);
        }

    argc -= optind;
    argv += optind;
    if (argc == 0) {
        usage();
        exit(1);
    }

    if (argc > 1)
        outpath = argv[1];

    if (identical_file(argv[0], outpath) == 1) {
        logging(LOG_ERR,
                "\"%s\" and \"%s\" are identical.",
                argv[0], outpath);
        exit(1);
    }

    if ((output = fopen(outpath, mode)) == NULL) {
        logging(LOG_SYSERR, outpath);
        exit(1);
    }

    return conv_file(argv[0], fmt ? fmt : default_fmt, output, tseq) < 0
        ? 1 : 0;
}
