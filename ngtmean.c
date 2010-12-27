/*
 * ngtmean.c
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fileiter.h"
#include "gtool3.h"
#include "logging.h"
#include "myutils.h"
#include "range.h"
#include "seq.h"

#define PROGNAME "ngtmean"

#define DATA(vbuf, i) \
    (((vbuf)->type == GT3_TYPE_DOUBLE) \
    ? *((double *)((vbuf)->data) + (i)) \
    : *((float *) ((vbuf)->data) + (i)) )

#ifndef max
#  define max(a, b) ((a)>(b) ? (a) :(b))
#endif
#ifndef min
#  define min(a, b) ((a)<(b) ? (a) :(b))
#endif

struct mdata {
    char dimname[3][17];
    int off[3];                 /* "ASTR - 1" in an input file */
    double *wght[3];
    double miss;

    /* shape of data, wsum (buffer) */
    size_t size, reserved_size;
    int shape[3];
    double *data;
    double *wsum;

    /* slicing (independent of ASTR) */
    struct range range[3];
};

#define RANGE_MAX 0x7fffffff
static struct range g_range[3] = {
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
    { 0, RANGE_MAX }
};
static int shift_flag = 1;
static struct sequence *g_zseq = NULL;
static int sum_flag = 0;

/* mean flags */
#define X_MEAN   1U
#define Y_MEAN   2U
#define Z_MEAN   4U
#define X_WEIGHT 8U
#define Y_WEIGHT 16U
#define Z_WEIGHT 32U


static int
shift_var(struct mdata *mdata, unsigned mode)
{
    if (mode & Z_MEAN) {
        mdata->dimname[2][0] = '\0';
        mdata->off[2] = 0;
        mdata->range[2].str = mdata->range[2].end = 0;
    }
    if (mode & Y_MEAN) {
        memcpy(mdata->dimname[1], mdata->dimname[2], 17);
        mdata->shape[1] = mdata->shape[2];
        mdata->off[1] = mdata->off[2];
        mdata->range[1] = mdata->range[2];
        mdata->dimname[2][0] = '\0';
        mdata->shape[2] = 1;
        mdata->off[2] = 0;
        mdata->range[2].str = mdata->range[2].end = 0;
    }

    if (mode & X_MEAN) {
        memcpy(mdata->dimname[0], mdata->dimname[1], 17);
        mdata->shape[0] = mdata->shape[1];
        mdata->off[0] = mdata->off[1];
        mdata->range[0] = mdata->range[1];

        memcpy(mdata->dimname[1], mdata->dimname[2], 17);
        mdata->shape[1] = mdata->shape[2];
        mdata->off[1] = mdata->off[2];
        mdata->range[1] = mdata->range[2];

        mdata->dimname[2][0] = '\0';
        mdata->shape[2] = 1;
        mdata->off[2] = 0;
        mdata->range[2].str = mdata->range[2].end = 0;
    }
    return 0;
}


static int
calc_mean(struct mdata *mdata, GT3_Varbuf *vbuf, unsigned mode)
{
    double wz, wght;
    int i, x, y, z;
    int xm, ym, zm, n;
    int idest;
    int x0, x1, y0, y1;
    double value;
    double *wghtx = NULL, *wghty = NULL, *wghtz = NULL;
    double wyz;

    wz = 1.;
    for (i = 0; i < mdata->size; i++) {
        mdata->data[i] = 0.;
        mdata->wsum[i] = 0.;
    }

    if (mdata->wght[0])
        wghtx = mdata->wght[0] + mdata->off[0];
    if (mdata->wght[1])
        wghty = mdata->wght[1] + mdata->off[1];
    if (mdata->wght[2])
        wghtz = mdata->wght[2] + mdata->off[2];

    x0 = mdata->range[0].str;
    x1 = mdata->range[0].end;
    y0 = mdata->range[1].str;
    y1 = mdata->range[1].end;

    if (g_zseq)
        reinitSeq(g_zseq, 1, vbuf->fp->dimlen[2]);

    for (n = 0; n < mdata->range[2].end - mdata->range[2].str; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                assert(!"NOTREACHED");
            }
            z = g_zseq->curr - 1;
        } else
            z = n + mdata->range[2].str;

        if (GT3_readVarZ(vbuf, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
        zm = (Z_MEAN & mode) ? 0 : n;
        if (wghtz)
            wz = wghtz[z];

        for (y = y0; y < y1; y++) {
            ym = (Y_MEAN & mode) ? 0 : y - y0;
            wyz = wghty ? wghty[y] * wz : wz;

            idest = mdata->shape[0] * (ym + mdata->shape[1] * zm);
            for (x = x0; x < x1; x++) {
                i = x + vbuf->dimlen[0] * y;
                value = DATA(vbuf, i);
                if (value == vbuf->miss)
                    continue;

                xm = (X_MEAN & mode) ? 0 : x - x0;
                wght = wghtx ? wghtx[x] * wyz : wyz;
                mdata->data[idest + xm] += wght * value;
                mdata->wsum[idest + xm] += wght;
            }
        }
    }

    if (sum_flag == 0) {
        for (i = 0; i < mdata->size; i++)
            if (mdata->wsum[i] > 0.)
                mdata->data[i] /= mdata->wsum[i];
            else
                mdata->data[i] = mdata->miss;
    } else
        for (i = 0; i < mdata->size; i++)
            if (mdata->wsum[i] == 0.)
                mdata->data[i] = mdata->miss;

    return 0;
}


static int
is_need_weight(const char *name)
{
    return (   name[0] == '\0'
            || memcmp(name, "SFC1\0", 5) == 0
            || memcmp(name, "NUMBER", 6) == 0
            || (sum_flag == 0 && memcmp(name, "GLON", 4) == 0)
            || (sum_flag == 0 && memcmp(name, "OCLON", 5) == 0) )
        ? 0 : 1;
}


static int
setup_dim(struct mdata *var,
          int size,
          const GT3_HEADER *head,
          int axis, unsigned flag)
{
    char key2[3] = { '1', '2', '3' };
    char key[8], name[17];
    int val;

    snprintf(key, sizeof key, "AITM%c", key2[axis]);
    GT3_copyHeaderItem(name, sizeof name, head, key);

    /* wght */
    if (flag && is_need_weight(name)) {
        if (strcmp(var->dimname[axis], name) != 0) {
            free(var->wght[axis]);

            if ((var->wght[axis] = GT3_getDimWeight(name)) == NULL) {
                GT3_printErrorMessages(stderr);
                logging(LOG_WARN, "Ignore weight of %s.", name);
                /* return -1; */
            }
        }
    } else {
        free(var->wght[axis]);
        var->wght[axis] = NULL;
    }

    /* dimname */
    strcpy(var->dimname[axis], name);

    /* off (ASTR[1-3] of input data) */
    snprintf(key, sizeof key, "ASTR%c", key2[axis]);
    if (GT3_decodeHeaderInt(&val, head, key) < 0) {
        GT3_printErrorMessages(stderr);
        logging(LOG_WARN, "Ignore this error...");
        val = 1;
    }
    var->off[axis] = val - 1;

    /* range: clip 0 .. dimlen */
    var->range[axis].str = max(0, g_range[axis].str);
    var->range[axis].end = min(size, g_range[axis].end);

    /*
     * check AEND[1-3] if weight is used.
     */
    if (var->wght[axis]) {
        GT3_Dim *dim;

        if ((dim = GT3_getDim(name))) {
            snprintf(key, sizeof key, "AEND%c", key2[axis]);

            val = var->off[axis] + size;
            GT3_decodeHeaderInt(&val, head, key);
            if (val > dim->len - dim->cyclic) {
                logging(LOG_WARN, "%s exceeds dimlen(%d)",
                        key, dim->len - dim->cyclic);
                logging(LOG_WARN, "Ignore weight for %s", name);
                free(var->wght[axis]);
                var->wght[axis] = NULL;
            }
        }
        GT3_freeDim(dim);
    }

    if (var->range[axis].str >= var->range[axis].end) {
        logging(LOG_ERR, "empty %c-range", "XYZ"[axis]);
        return -1;
    }
    return 0;
}


static int
setup_mdata(struct mdata *var,
            const int *dimlen,
            const GT3_HEADER *head,
            unsigned mode)
{
    double miss;

    if (   setup_dim(var, dimlen[0], head, 0, mode & X_WEIGHT) < 0
        || setup_dim(var, dimlen[1], head, 1, mode & Y_WEIGHT) < 0
        || setup_dim(var, dimlen[2], head, 2, mode & Z_WEIGHT) < 0)
        return -1;

    /*
     * z-slicing.
     */
    if (g_zseq) {
        reinitSeq(g_zseq, 1, dimlen[2]);
        var->range[2].str = 0;
        var->range[2].end = countSeq(g_zseq);
    }
    var->shape[0] = mode & X_MEAN ? 1 : var->range[0].end - var->range[0].str;
    var->shape[1] = mode & Y_MEAN ? 1 : var->range[1].end - var->range[1].str;
    var->shape[2] = mode & Z_MEAN ? 1 : var->range[2].end - var->range[2].str;

    if (GT3_decodeHeaderDouble(&miss, head, "MISS") < 0) {
        GT3_printErrorMessages(stderr);
        miss = -999.; /* default value */
    }
    var->miss = miss;
    return 0;
}


static int
realloc_var(struct mdata *var)
{
    size_t size;

    assert(var->shape[0] > 0 && var->shape[1] > 0 && var->shape[2] > 0);

    size = var->shape[0] * var->shape[1] * var->shape[2];

    if (size > var->reserved_size) {
        free(var->data);
        if ((var->data = malloc(2 * sizeof(double) * size)) == NULL) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }

        var->wsum = var->data + size; /* dirty? */
        var->reserved_size = size;
    }
    var->size = size;
    return 0;
}


static int
modify_head(GT3_HEADER *head, const struct mdata *mdata, unsigned mode)
{
    char buf[17];

    if (mode & X_MEAN) {
        GT3_setHeaderEdit(head, (mode & X_WEIGHT) ? "XMW" : "XM");
        snprintf(buf, sizeof buf, "%s:%d,%d",
                 mdata->dimname[0],
                 mdata->off[0] + mdata->range[0].str + 1,
                 mdata->off[0] + mdata->range[0].end);
        GT3_setHeaderEttl(head, buf);
    }
    if (mode & Y_MEAN) {
        GT3_setHeaderEdit(head, (mode & Y_WEIGHT) ? "YMW" : "YM");
        snprintf(buf, sizeof buf, "%s:%d,%d",
                 mdata->dimname[1],
                 mdata->off[1] + mdata->range[1].str + 1,
                 mdata->off[1] + mdata->range[1].end);
        GT3_setHeaderEttl(head, buf);
    }
    if (mode & Z_MEAN) {
        GT3_setHeaderEdit(head, (mode & Z_WEIGHT) ? "ZMW" : "ZM");
        if (g_zseq) {
            snprintf(buf, sizeof buf, "%s(%s)",
                     mdata->dimname[2], g_zseq->spec);
        } else {
            snprintf(buf, sizeof buf, "%s:%d,%d",
                     mdata->dimname[2],
                     mdata->off[2] + mdata->range[2].str + 1,
                     mdata->off[2] + mdata->range[2].end);
        }
        GT3_setHeaderEttl(head, buf);
    }
    return 0;
}


static int
write_mean(FILE *output, const struct mdata *mdata,
           const GT3_HEADER *headin,
           unsigned mode,
           const char *fmt)
{
    GT3_HEADER head;
    int rval;
    char fmt_asis[17];

    if (!fmt) {
        /* use the same format to input data. */
        GT3_copyHeaderItem(fmt_asis, sizeof fmt_asis, headin, "DFMT");
    }

    memcpy(&head, headin, sizeof(GT3_HEADER));

    GT3_setHeaderString(&head, "AITM1", mdata->dimname[0]);
    GT3_setHeaderString(&head, "AITM2", mdata->dimname[1]);
    if (g_zseq)
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
    else
        GT3_setHeaderString(&head, "AITM3", mdata->dimname[2]);

    GT3_setHeaderInt(&head, "ASTR1", 1 + mdata->off[0] + mdata->range[0].str);
    GT3_setHeaderInt(&head, "ASTR2", 1 + mdata->off[1] + mdata->range[1].str);
    GT3_setHeaderInt(&head, "ASTR3", 1 + mdata->off[2] + mdata->range[2].str);

    if ((rval = GT3_write(mdata->data, GT3_TYPE_DOUBLE,
                          mdata->shape[0],
                          mdata->shape[1],
                          mdata->shape[2],
                          &head,
                          fmt ? fmt : fmt_asis,
                          output)) < 0)
        GT3_printErrorMessages(stderr);

    return rval;
}


static int
ngtmean(FILE *output, const char *path,
        struct mdata *mdata, unsigned mode, const char *fmt,
        struct sequence *tseq)
{
    GT3_File *fp = NULL;
    GT3_Varbuf *vbuf = NULL;
    GT3_HEADER head;
    file_iterator it;
    int stat, rval = -1;

    if ((fp = GT3_open(path)) == NULL
        || ((vbuf = GT3_getVarbuf(fp)) == NULL)) {
        GT3_printErrorMessages(stderr);
        goto finish;
    }

    setup_file_iterator(&it, fp, tseq);
    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        if (GT3_readHeader(&head, fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
        if (setup_mdata(mdata, fp->dimlen, &head, mode) < 0
            || realloc_var(mdata) < 0
            || calc_mean(mdata, vbuf, mode) < 0
            || modify_head(&head, mdata, mode) < 0
            || (shift_flag && shift_var(mdata, mode) < 0)
            || write_mean(output, mdata, &head, mode, fmt) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_freeVarbuf(vbuf);
    GT3_close(fp);
    return rval;
}


static unsigned
set_mmode(const char *str)
{
    unsigned mode = 0;
    struct { char key; unsigned value; } tab[] = {
        { 'x', X_MEAN | X_WEIGHT },
        { 'y', Y_MEAN | Y_WEIGHT },
        { 'z', Z_MEAN | Z_WEIGHT },
        { 'X', X_MEAN },
        { 'Y', Y_MEAN },
        { 'Z', Z_MEAN },
    };
    int i;

    for (; *str; str++)
        for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
            if (*str == tab[i].key) {
                mode |= tab[i].value;
                break;
            }
    return mode;
}


static char *
toupper_string(char *str)
{
    char *p = str;

    while ((*p = toupper(*p)))
        p++;
    return str;
}


static void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] [files...]\n"
        "\n"
        "calculate mean.\n"
        "\n"
        "Options:\n"
        "    -f fmt    output GTOOL3 format (default: same as input)\n"
        "    -m MODE   mean mode (any combination \"xyzXYZ\")\n"
        "    -n        no shift axes\n"
        "    -o PATH   specify output file\n"
        "    -s        sum instead of mean\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z LIST   specify Z-layer\n"
        "    -h        print help message\n"
        "\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct mdata mdata;
    unsigned mode = X_MEAN | Y_MEAN | Z_MEAN | X_WEIGHT | Y_WEIGHT | Z_WEIGHT;
    char *filename = "gtool.out";
    char dummy[17];
    char *fmt = NULL;
    FILE *output = NULL;
    struct sequence *tseq = NULL;
    int ch;
    int exitval = 0;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "f:m:no:st:x:y:z:h")) != -1)
        switch (ch) {
        case 'f':
            fmt = strdup(optarg);
            toupper_string(fmt);
            break;
        case 'm':
            mode = set_mmode(optarg);
            break;
        case 'n':
            shift_flag = 0;
            break;
        case 'o':
            filename = optarg;
            break;
        case 's':
            sum_flag = 1;
            break;
        case 't':
            if ((tseq = initSeq(optarg, 1, RANGE_MAX)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'x':
            if (get_range(g_range, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-x: invalid x-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'y':
            if (get_range(g_range + 1, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-y: invalid y-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'z':
            if (get_seq_or_range(g_range + 2, &g_zseq,
                                 optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'h':
        default:
            usage();
            exit(0);
            break;
        }

    if (fmt && GT3_output_format(dummy, fmt) < 0) {
        logging(LOG_ERR, "%s: Unknown format", fmt);
        exit(1);
    }
    if ((output = fopen(filename, "wb")) == NULL) {
        logging(LOG_SYSERR, filename);
        exit(1);
    }

    argc -= optind;
    argv += optind;
    memset(&mdata, 0, sizeof(struct mdata));

    for (; argc > 0 && *argv; argc--, argv++) {
        if (tseq)
            reinitSeq(tseq, 1, 0x7ffffff);

        if (ngtmean(output, *argv, &mdata, mode, fmt, tseq) < 0) {
            logging(LOG_ERR, "in %s.", *argv);
            exitval = 1;
            break;
        }
    }

    if (fclose(output) < 0) {
        logging(LOG_SYSERR, NULL);
        exitval = 1;
    }
    return exitval;
}
