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

struct range {
    int str, end;
};

struct mdata {
    char dimname[3][17];
    int shape[3];
    int off[3];
    double *wght[3];
    double miss;
    size_t size, reserved_size;

    double *data;
    double *wsum;
};

static struct range *g_xrange = NULL;
static struct range *g_yrange = NULL;

/* mean flags */
#define X_MEAN   1U
#define Y_MEAN   2U
#define Z_MEAN   4U
#define X_WEIGHT 8U
#define Y_WEIGHT 16U
#define Z_WEIGHT 32U


static int
calc_mean(struct mdata *mdata, GT3_Varbuf *vbuf, unsigned mode)
{
    double wx, wy, wz, wght;
    int i, x, y, z;
    int xm, ym, zm;
    int idest;
    double value;

    wx = wy = wz = 1.;
    for (i = 0; i < mdata->size; i++) {
        mdata->data[i] = 0.;
        mdata->wsum[i] = 0.;
    }

    for (z = 0; z < vbuf->dimlen[2]; z++) {
        if (GT3_readVarZ(vbuf, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
        zm = (Z_MEAN & mode) ? 0 : z;
        if (mdata->wght[2])
            wz = mdata->wght[2][z];

        for (y = 0; y < vbuf->dimlen[1]; y++) {
            for (x = 0; x < vbuf->dimlen[0]; x++) {
                i = x + vbuf->dimlen[0] * y;
                value = DATA(vbuf, i);
                if (value == vbuf->miss)
                    continue;

                xm = (X_MEAN & mode) ? 0 : x;
                ym = (Y_MEAN & mode) ? 0 : y;

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
setup_dim(struct mdata *var,
          const GT3_HEADER *head,
          int axis, unsigned flag)
{
    const char *aitm[] = { "AITM1", "AITM2", "AITM3" };
    char name[17];


    GT3_copyHeaderItem(name, sizeof name, head, aitm[axis]);
    if (strcmp(var->dimname[axis], name) != 0) {
        strcpy(var->dimname[axis], name);
        free(var->wght[axis]);
        var->wght[axis] = NULL;

        if (flag && (var->wght[axis] = GT3_getDimWeight(name)) == NULL) {
            GT3_printErrorMessages(stderr);
            logging(LOG_WARN, "Ignore weight of %s.", name);
            /* return -1; */
        }
    }
    return 0;
}


static int
setup_var(struct mdata *var,
          const int *dimlen,
          const GT3_HEADER *head,
          unsigned mode)
{
    double miss;

    if (   setup_dim(var, head, 0, mode & X_WEIGHT) < 0
        || setup_dim(var, head, 1, mode & Y_WEIGHT) < 0
        || setup_dim(var, head, 2, mode & Z_WEIGHT) < 0)
        return -1;

    var->shape[0] = (mode & X_MEAN) ? 1 : dimlen[0];
    var->shape[1] = (mode & Y_MEAN) ? 1 : dimlen[1];
    var->shape[2] = (mode & Z_MEAN) ? 1 : dimlen[2];
    var->off[0] = var->off[1] = var->off[2] = 0;

    miss = -999.; /* default value */
    if (GT3_decodeHeaderDouble(&miss, head, "MISS") < 0)
        GT3_printErrorMessages(stderr);
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


int
write_mean(FILE *output, const struct mdata *mdata,
           const GT3_HEADER *headin,
           const char *fmt)
{
    GT3_HEADER head;
    int rval;

    memcpy(&head, headin, sizeof(GT3_HEADER));
    if ((rval = GT3_write(mdata->data, GT3_TYPE_DOUBLE,
                          mdata->shape[0],
                          mdata->shape[1],
                          mdata->shape[2],
                          &head, fmt, output)) < 0)
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
            || write_mean(output, mdata, &head, fmt) < 0)
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


int
main(int argc, char **argv)
{
    struct mdata mdata;
    unsigned mode = X_MEAN | Y_MEAN | Z_MEAN | X_WEIGHT | Y_WEIGHT | Z_WEIGHT;
    char *filename = "gtool.out";
    char dummy[17];
    char *fmt = "UR8";
    FILE *output = NULL;
    int ch;
    int exitval = 1;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "f:m:o:")) != -1)
        switch (ch) {
        case 'f':
            fmt = strdup(optarg);
            toupper_string(fmt);
            break;
        case 'm':
            mode = set_mmode(optarg);
            break;
        case 'o':
            filename = optarg;
            break;
        default:
            break;
        }

    if (GT3_output_format(dummy, fmt) < 0) {
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
