/*
 * ngtconv.c -- gtool3 format converter.
 */
#include "internal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
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
 * raw_output: a function for raw binary output.
 */
typedef size_t (*output_func)(double *, size_t, FILE *);
static output_func raw_output = NULL;

/*
 * enum of special operations.
 */
enum {
    OP_NONE,                    /* dummy */
    OP_ASIS,
    OP_MASK,
    OP_UNMASK,
    OP_INT,
    OP_MASKINT
};


static int
lookup_operation(const char *name)
{
    static struct { const char *key; int value; } optab[] = {
        { "ASIS",    OP_ASIS },
        { "MASK",    OP_MASK },
        { "UNMASK",  OP_UNMASK },
        { "INT",     OP_INT },
        { "MASKINT", OP_MASKINT }
    };
    int i;

    for (i = 0; i < sizeof optab / sizeof optab[0]; i++)
        if (strcmp(name, optab[i].key) == 0)
            return optab[i].value;

    return OP_NONE;
}


/*
 * raw output funcs.
 */
static size_t
fwrite_as_dble(double *ptr, size_t nelems, FILE *fp, int byteswap)
{
    if (byteswap)
        reverse_dwords(ptr, nelems); /* data will be broken. OK. */

    return fwrite(ptr, sizeof(double), nelems, fp);
}

static size_t
fwrite_as_dble_little(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_dble(ptr, nelems, fp, IS_LITTLE_ENDIAN ? 0 : 1);
}

static size_t
fwrite_as_dble_big(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_dble(ptr, nelems, fp, IS_LITTLE_ENDIAN ? 1 : 0);
}

static size_t
fwrite_as_dble_native(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_dble(ptr, nelems, fp, 0);
}

static size_t
fwrite_as_float(double *ptr, size_t nelems, FILE *fp, int byteswap)
{
#define BUFSIZE (IO_BUF_SIZE >> 2)
    float buf[BUFSIZE];
    int i;
    size_t len, cnt = 0;

    while (nelems > 0) {
        len = (nelems > BUFSIZE) ? BUFSIZE : nelems;

        for (i = 0; i < len; i++)
            buf[i] = (float)ptr[i];

        if (byteswap)
            reverse_words(buf, len);

        if (fwrite(buf, sizeof(float), len, fp) != len)
            break;

        cnt += len;
        nelems -= len;
        ptr += len;
    }
    return cnt;
}

static size_t
fwrite_as_float_little(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_float(ptr, nelems, fp, IS_LITTLE_ENDIAN ? 0 : 1);
}

static size_t
fwrite_as_float_big(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_float(ptr, nelems, fp, IS_LITTLE_ENDIAN ? 1 : 0);
}

static size_t
fwrite_as_float_native(double *ptr, size_t nelems, FILE *fp)
{
    return fwrite_as_float(ptr, nelems, fp, 0);
}


static int
find_raw_format(const char *name)
{
    int i;
    struct output_tab {
        const char *key;
        output_func func;
    };
    static struct output_tab tab[] = {
        {"RAW_DOUBLE_LITTLE", fwrite_as_dble_little},
        {"RAW_DOUBLE_BIG", fwrite_as_dble_big},
        {"RAW_FLOAT_LITTLE", fwrite_as_float_little},
        {"RAW_FLOAT_BIG", fwrite_as_float_big},
        {"RAW_DOUBLE", fwrite_as_dble_native},
        {"RAW_FLOAT", fwrite_as_float_native}
    };

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(name, tab[i].key) == 0) {
            raw_output = tab[i].func;
            return 0;
        }
    return -1;
}


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
    file_stat_t sb1, sb2;

    if (file_stat(path1, &sb1) < 0 || file_stat(path2, &sb2) < 0)
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
 * Return the offset value and the number of bits required.
 */
static int
find_params_for_int(double *param1, unsigned *param2,
                    const double *val, size_t nelems, double miss)
{
    double vmin = HUGE_VAL, vmax = -HUGE_VAL, vcount;
    uint32_t vwidth;
    unsigned nbits;
    size_t i;

    for (i = 0; i < nelems; i++)
        if (val[i] != miss) {
            vmin = (val[i] < vmin) ? val[i] : vmin;
            vmax = (val[i] > vmax) ? val[i] : vmax;
        }

    if (vmin > vmax) {          /* no value. */
        *param1 = 0.;
        *param2 = 1;
        return 0;
    }

    vmin = round(vmin);
    vmax = round(vmax);
    vcount = vmax - vmin + 2.;  /* including the missing value. */

    if (vcount > (double)0x80000000U) /* overflow */
        return -1;

    vwidth = (uint32_t)vcount;
    for (nbits = 1; nbits < 31; nbits++)
        if ((1U << nbits) >= vwidth)
            break;

    *param1 = vmin;
    *param2 = nbits;
    return 0;
}


/*
 * UR4 => MR4, UR8 => MR8, URC => MRY16, URX?? => MRY??, URY?? => MRY??
 */
static char *
masked_format(char *fmt)
{
    if (   strcmp(fmt, "URC") == 0
        || strcmp(fmt, "URC2") == 0
        || strcmp(fmt, "UI2") == 0) {
        strcpy(fmt, "MRY16");
    } else {
        if (fmt[0] == 'U' && fmt[1] == 'R') {
            fmt[0] = 'M';

            if (fmt[2] == 'X')  /* XXX: MRX is deprecated. */
                fmt[2] = 'Y';
        }
    }
    return fmt;
}


/*
 * MR4 => UR4, MR8 => UR8, MR[XY]?? => URY??.
 */
static char *
unmasked_format(char *fmt)
{
    if (fmt[0] == 'M' && fmt[1] == 'R') {
        fmt[0] = 'U';

        if (fmt[2] == 'X')      /* XXX: URX is deprecated. */
            fmt[2] = 'Y';
    }
    return fmt;
}


static int
conv_chunk(FILE *output, const char *dfmt, int optype,
           GT3_Varbuf *var, GT3_File *fp)
{
    GT3_HEADER head;
    int nx, ny, nz;
    int i, n, y, z;
    size_t offset, nelems;
    struct range range[3];
    int astr[] = { 1, 1, 1 };
    char key[17];
    char suffix[] = { '1', '2', '3' };
    int rval;

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

    if (allocate_buffer(&g_buffer, (size_t)nx * ny * nz) < 0) {
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

    /*
     * output in raw binary format {4-byte,8-byte} {big,little}.
     */
    if (raw_output) {
        nelems = (size_t)nx * ny * nz;

        if (raw_output(g_buffer.ptr, nelems, output) != nelems) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }
        return 0;
    }

    GT3_setHeaderInt(&head, "ASTR1", astr[0] + range[0].str);
    GT3_setHeaderInt(&head, "ASTR2", astr[1] + range[1].str);
    if (g_zseq) {
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
        GT3_setHeaderInt(&head, "ASTR3", 1);
    } else
        GT3_setHeaderInt(&head, "ASTR3", astr[2] + range[2].str);

    if (optype == OP_INT || optype == OP_MASKINT) {
        double offset, scale = 1., miss = -999.;
        unsigned nbits;

        /*
         * OP_INT || OP_MASKINT:
         * Bit packing for integers (normal and masked).
         */
        nelems = (size_t)nx * ny * nz;
        GT3_decodeHeaderDouble(&miss, &head, "MISS");

        if (find_params_for_int(&offset, &nbits,
                                g_buffer.ptr, nelems, miss) < 0) {
            logging(LOG_ERR, "INT/MASK_INT is not available (overflow).");
            return -1;
        }
        rval = GT3_write_bitpack(g_buffer.ptr, GT3_TYPE_DOUBLE,
                                 nx, ny, nz, &head,
                                 offset, scale,
                                 nbits, optype == OP_MASKINT,
                                 output);
    } else {
        char asis[17];
        const char *p;

        if (optype == OP_NONE) {
            p = dfmt;
        } else {
            p = asis;
            GT3_copyHeaderItem(asis, sizeof asis, &head, "DFMT");

            /* Tweak the asis for masking or unmasking. */
            if (optype == OP_MASK)
                masked_format(asis);
            if (optype == OP_UNMASK)
                unmasked_format(asis);
        }
        rval = GT3_write(g_buffer.ptr, GT3_TYPE_DOUBLE,
                         nx, ny, nz, &head, p, output);
    }
    if (rval < 0)
        GT3_printErrorMessages(stderr);

    return rval;
}


static int
conv_file(const char *path, const char *fmt, int optype, FILE *output,
          struct sequence *seq)
{
    GT3_File *fp;
    GT3_Varbuf *var;
    file_iterator it;
    int rval = -1, stat;

    if ((fp = GT3_open(path)) == NULL
        || (var = GT3_getVarbuf(fp)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    setup_file_iterator(&it, fp, seq);
    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        if (conv_chunk(output, fmt, optype, var, fp) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_freeVarbuf(var);
    GT3_close(fp);
    return rval;
}


void
usage(void)
{
    const char *usage_message =
        "Usage:\n"
        "       " PROGNAME " [options] input [output]\n"
        "       " PROGNAME " -o output [options] input1 [input2 ...]\n"
        "\n"
        "File format converter.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        output in append mode\n"
        "    -f fmt    specify output format (default: UR4)\n"
        "    -v        be verbose\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z LIST   specify Z-planes\n";

    const char *formats =
        "Available arguments for the -f option (case-insensitive):\n"
        "\n"
        "    GTOOL3 formats:\n"
        "       ur4, ur8, mr4, mr8\n"
        "       ury{01,02,...,31}, mry{01,02,...,31}\n"
        "\n"
        "    Special operations:\n"
        "       asis, mask, unmask, int, maskint\n"
        "\n"
        "    Raw binary formats:\n"
        "       raw_float  (native-endian)\n"
        "       raw_double (native-endian)\n"
        "       raw_float_little, raw_float_big\n"
        "       raw_double_little, raw_double_big\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
    fprintf(stderr, "%s\n", formats);
}


int
main(int argc, char **argv)
{
    int ch;
    struct sequence *tseq = NULL;
    const char *mode = "wb";
    char *fmt = "UR4";
    int optype = OP_NONE;
    char *outpath = NULL;
    FILE *output;
    char dummy[17];
    int i, num_inputs;
    int rval = 0;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "af:o:t:vx:y:z:h")) != -1)
        switch (ch) {
        case 'a':
            mode = "ab";
            break;
        case 'f':
            toupper_string(optarg);
            if ((optype = lookup_operation(optarg)) == OP_NONE
                && GT3_output_format(dummy, optarg) < 0
                && find_raw_format(optarg) < 0) {
                logging(LOG_ERR, "-f: %s: Unknown format", optarg);
                exit(1);
            }
            fmt = optarg;
            break;
        case 'o':
            outpath = optarg;
            break;
        case 't':
            if ((tseq = initSeq(optarg, 1, RANGE_MAX)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'v':
            set_logging_level("verbose");
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

    if (outpath) {
        /*
         * All the arguments are input files.
         */
        if (argc == 0) {
            usage();
            exit(1);
        }
        num_inputs = argc;
    } else {
        /*
         * Only the first argument is an input file.
         * And the second argument (if any) is an output file.
         */
        if (argc == 0 || argc > 2) {
            usage();
            exit(1);
        }
        num_inputs = 1;
        outpath = (argc == 2) ? argv[1] : "gtool.out";
    }

    /*
     * Check the common catastrophic mistake.
     */
    for (i = 0; i < num_inputs; i++)
        if (identical_file(argv[i], outpath) == 1) {
            logging(LOG_ERR, "\"%s\" is identical to \"%s\".",
                    outpath, argv[i]);
            exit(1);
        }

    if ((output = fopen(outpath, mode)) == NULL) {
        logging(LOG_SYSERR, outpath);
        exit(1);
    }

    for (i = 0; i < num_inputs; i++) {
        if (tseq)
            reinitSeq(tseq, 1, RANGE_MAX);

        logging(LOG_INFO, "Copying %s", argv[i]);
        if ((rval = conv_file(argv[i], fmt, optype, output, tseq)) < 0)
            break;
    }

    fclose(output);
    return rval < 0 ? 1 : 0;
}
