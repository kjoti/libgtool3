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

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"

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

struct range {
    int str, end;
};

struct mdata {
    char dimname[3][17];
    int off[3];
    double *wght[3];
    double miss;
    size_t size, reserved_size;

    int shape[3];

    /*
     * XXX: 'off' indicates 'str1 - 1' of input data, which is
     * not affected range option.
     */
    int nstr[3];
    double *data;
    double *wsum;
    struct range range[3];
};

#define RANGE_MAX 0x7fffffff
static struct range g_range[3] = {
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
};
static int shift_flag = 1;

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
    if (mode & Z_MEAN)
        mdata->dimname[2][0] = '\0';

    if (mode & Y_MEAN) {
        memcpy(mdata->dimname[1], mdata->dimname[2], 17);
        mdata->shape[1] = mdata->shape[2];
        mdata->nstr[1] = mdata->nstr[2];
        mdata->dimname[2][0] = '\0';
        mdata->shape[2] = 1;
        mdata->nstr[2] = 0;
    }

    if (mode & X_MEAN) {
        memcpy(mdata->dimname[0], mdata->dimname[1], 17);
        mdata->shape[0] = mdata->shape[1];
        mdata->nstr[0] = mdata->nstr[1];

        memcpy(mdata->dimname[1], mdata->dimname[2], 17);
        mdata->shape[1] = mdata->shape[2];
        mdata->nstr[1] = mdata->nstr[2];

        mdata->dimname[2][0] = '\0';
        mdata->shape[2] = 1;
        mdata->nstr[2] = 0;
    }
    return 0;
}


static int
calc_mean(struct mdata *mdata, GT3_Varbuf *vbuf, unsigned mode)
{
    double wx, wy, wz, wght;
    int i, x, y, z;
    int xm, ym, zm;
    int idest;
    int x0, x1, y0, y1;
    double value;

    wx = wy = wz = 1.;
    for (i = 0; i < mdata->size; i++) {
        mdata->data[i] = 0.;
        mdata->wsum[i] = 0.;
    }

    x0 = mdata->range[0].str;
    x1 = mdata->range[0].end;
    y0 = mdata->range[1].str;
    y1 = mdata->range[1].end;

    for (z = mdata->range[2].str; z < mdata->range[2].end; z++) {
        if (GT3_readVarZ(vbuf, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
        zm = (Z_MEAN & mode) ? 0 : z;
        if (mdata->wght[2])
            wz = mdata->wght[2][z];

        for (y = y0; y < y1; y++) {
            for (x = x0; x < x1; x++) {
                i = x - mdata->off[0] + vbuf->dimlen[0] * (y - mdata->off[1]);
                value = DATA(vbuf, i);
                if (value == vbuf->miss)
                    continue;

                xm = (X_MEAN & mode) ? 0 : x - x0;
                ym = (Y_MEAN & mode) ? 0 : y - y0;

                if (mdata->wght[0])
                    wx = mdata->wght[0][x];
                if (mdata->wght[1])
                    wy = mdata->wght[1][y];

                idest = xm + mdata->shape[0] * (ym + mdata->shape[1] * zm);

                wght = wx * wy * wz;
                mdata->data[idest] += wght * value;
                mdata->wsum[idest] += wght;
            }
        }
    }

    for (i = 0; i < mdata->size; i++)
        if (mdata->wsum[i] > 0.)
            mdata->data[i] /= mdata->wsum[i];
        else
            mdata->data[i] = mdata->miss;

    return 0;
}


static int
is_need_weight(const char *name)
{
    return (   name[0] == '\0'
            || memcmp(name, "NUMBER", 6) == 0 )
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
    val--;
    var->off[axis] = val;

    /* range */
    var->range[axis].str = max(val, g_range[axis].str);
    var->range[axis].end = min(val + size, g_range[axis].end);
    if (var->range[axis].str >= var->range[axis].end) {
        logging(LOG_ERR, "empty %c-range", "XYZ"[axis]);
        return -1;
    }
    /* nstr might be shifted */
    var->nstr[axis] = var->range[axis].str;
    return 0;
}


static int
setup_var(struct mdata *var,
          const int *dimlen,
          GT3_HEADER *head,
          unsigned mode)
{
    double miss;

    if (   setup_dim(var, dimlen[0], head, 0, mode & X_WEIGHT) < 0
        || setup_dim(var, dimlen[1], head, 1, mode & Y_WEIGHT) < 0
        || setup_dim(var, dimlen[2], head, 2, mode & Z_WEIGHT) < 0)
        return -1;

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
write_mean(FILE *output, const struct mdata *mdata,
           const GT3_HEADER *headin,
           unsigned mode,
           const char *fmt)
{
    GT3_HEADER head;
    int rval;
    char fmt_asis[17];
    char buf[17];

    if (!fmt) {
        /* use the same format to input data. */
        GT3_copyHeaderItem(fmt_asis, sizeof fmt_asis, headin, "DFMT");
    }

    memcpy(&head, headin, sizeof(GT3_HEADER));

    GT3_setHeaderString(&head, "AITM1", mdata->dimname[0]);
    GT3_setHeaderString(&head, "AITM2", mdata->dimname[1]);
    GT3_setHeaderString(&head, "AITM3", mdata->dimname[2]);

    GT3_setHeaderInt(&head, "ASTR1", 1 + mdata->nstr[0]);
    GT3_setHeaderInt(&head, "ASTR2", 1 + mdata->nstr[1]);
    GT3_setHeaderInt(&head, "ASTR3", 1 + mdata->nstr[2]);

    if (mode & X_MEAN) {
        GT3_setHeaderEdit(&head, (mode & X_WEIGHT) ? "XMW" : "XM");
        snprintf(buf, sizeof buf, "X-Mean:%d,%d",
                 mdata->range[0].str + 1, mdata->range[0].end);
        GT3_setHeaderEttl(&head, buf);
    }

    if (mode & Y_MEAN) {
        GT3_setHeaderEdit(&head, (mode & Y_WEIGHT) ? "YMW" : "YM");
        snprintf(buf, sizeof buf, "Y-Mean:%d,%d",
                 mdata->range[1].str + 1, mdata->range[1].end);
        GT3_setHeaderEttl(&head, buf);
    }
    if (mode & Z_MEAN) {
        GT3_setHeaderEdit(&head, (mode & Z_WEIGHT) ? "ZMW" : "ZM");
        snprintf(buf, sizeof buf, "Z-Mean:%d,%d",
                 mdata->range[2].str + 1, mdata->range[2].end);
        GT3_setHeaderEttl(&head, buf);
    }

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
        struct mdata *mdata, unsigned mode, const char *fmt)
{
    GT3_File *fp = NULL;
    GT3_Varbuf *vbuf = NULL;
    GT3_HEADER head;
    int rval = -1;


    if ((fp = GT3_open(path)) == NULL
        || ((vbuf = GT3_getVarbuf(fp)) == NULL)) {
        GT3_printErrorMessages(stderr);
        goto finish;
    }

    while (!GT3_eof(fp)) {
        if (GT3_readHeader(&head, fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }

        if (setup_var(mdata, fp->dimlen, &head, mode) < 0
            || realloc_var(mdata) < 0
            || calc_mean(mdata, vbuf, mode) < 0
            || (shift_flag && shift_var(mdata, mode) < 0)
            || write_mean(output, mdata, &head, mode, fmt) < 0)
            goto finish;

        if (GT3_next(fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
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


static struct range *
set_range(struct range *range, const char *str)
{
    int vals[] = { 1, RANGE_MAX };
    int num;

    if (str == NULL || (num = get_ints(vals, 2, str, ':')) < 0)
        return NULL;

    range->str = vals[0] - 1;
    range->end = vals[1];

    if (num == 1)
        range->end = range->str + 1;
    return range;
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
    int ch;
    int exitval = 1;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "f:m:no:x:y:z:h")) != -1)
        switch (ch) {
        case 'f':
            fmt = strdup(optarg);
            toupper_string(fmt);
            break;
        case 'n':
            shift_flag = 0;
            break;
        case 'm':
            mode = set_mmode(optarg);
            break;
        case 'o':
            filename = optarg;
            break;
        case 'x':
            if (set_range(g_range, optarg) == NULL) {
                logging(LOG_ERR, "-x: invalid x-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'y':
            if (set_range(g_range + 1, optarg) == NULL) {
                logging(LOG_ERR, "-y: invalid y-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'z':
            if (set_range(g_range + 2, optarg) == NULL) {
                logging(LOG_ERR, "-z: invalid z-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'h':
        default:
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
        if (ngtmean(output, *argv, &mdata, mode, fmt) < 0) {
            logging(LOG_ERR, "in %s.", *argv);
            goto finish;
        }
    }

    exitval = 0;

finish:
    if (output && fclose(output) < 0) {
        logging(LOG_SYSERR, NULL);
        exitval = 1;
    }
    return exitval;
}
