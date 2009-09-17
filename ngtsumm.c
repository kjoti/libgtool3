/*
 *  ngtsumm.c -- print data summary.
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"

#define PROGNAME "ngtsumm"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif

static int each_plane = 0;
static int xrange[] = { 0, 0x7ffffff };
static int yrange[] = { 0, 0x7ffffff };
static int zrange[] = { 0, 0x7ffffff };
static int slicing = 0;


struct data_profile {
    int miss_cnt, nan_cnt, pinf_cnt, minf_cnt;

    double miss; /* used as input data */
};


void
init_profile(struct data_profile *prof)
{
    prof->miss_cnt = 0;
    prof->nan_cnt  = 0;
    prof->pinf_cnt = 0;
    prof->minf_cnt = 0;
}


void
get_dataprof_float(const void *ptr, int len, struct data_profile *prof)
{
    const float *data = (const float *)ptr;
    int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
    float miss;
    int i;

    miss = (float)prof->miss;
    for (i = 0; i < len; i++) {
        if (data[i] == miss) {
            mcnt++;
            continue;
        }
        if (isnan(data[i])) {   /* NaN */
            nan_cnt++;
            continue;
        }
        if (data[i] <= -HUGE_VAL) { /* -Inf. */
            minf++;
            continue;
        }
        if (data[i] >= HUGE_VAL) { /* +Inf. */
            pinf++;
            continue;
        }
    }
    prof->miss_cnt += mcnt;
    prof->nan_cnt  += nan_cnt;
    prof->pinf_cnt += pinf;
    prof->minf_cnt += minf;
}


void
get_dataprof_double(const void *ptr, int len, struct data_profile *prof)
{
    const double *data = (const double *)ptr;
    int mcnt = 0, nan_cnt = 0, minf = 0, pinf = 0;
    double miss;
    int i;

    miss = prof->miss;
    for (i = 0; i < len; i++) {
        if (data[i] == miss) {
            mcnt++;
            continue;
        }
        if (isnan(data[i])) {   /* NaN */
            nan_cnt++;
            continue;
        }
        if (data[i] <= -HUGE_VAL) { /* -Inf. */
            minf++;
            continue;
        }
        if (data[i] >= HUGE_VAL) { /* +Inf. */
            pinf++;
            continue;
        }
    }
    prof->miss_cnt += mcnt;
    prof->nan_cnt  += nan_cnt;
    prof->pinf_cnt += pinf;
    prof->minf_cnt += minf;
}


int
pack_slice_float(void *ptr, const GT3_Varbuf *var)
{
    const float *data = (const float *)var->data;
    float *output = (float *)ptr;
    int cnt = 0;
    int i, j;
    int xmax, ymax;

    xmax = min(xrange[1], var->dimlen[0]);
    ymax = min(yrange[1], var->dimlen[1]);
    for (j = yrange[0]; j < ymax; j++)
        for (i = xrange[0]; i < xmax; i++)
            output[cnt++] = data[j * var->dimlen[0] + i];

    return cnt;
}


int
pack_slice_double(void *ptr, const GT3_Varbuf *var)
{
    const double *data = (const double *)var->data;
    double *output = (double *)ptr;
    int cnt = 0;
    int i, j;
    int xmax, ymax;

    xmax = min(xrange[1], var->dimlen[0]);
    ymax = min(yrange[1], var->dimlen[1]);
    for (j = yrange[0]; j < ymax; j++)
        for (i = xrange[0]; i < xmax; i++)
            output[cnt++] = data[j * var->dimlen[0] + i];

    return cnt;
}


void
print_caption(FILE *fp, const char *path)
{
    const char *fmt = "%-8s %-12s %5s %10s %10s %10s %10s\n";
    const char *z = each_plane ? "Z" : "";

    fprintf(fp, "# Filename: %s\n", path);
    fprintf(fp, fmt, "#    No.", "ITEM", z,
            "MISS", "NaN", "+Inf", "-Inf");
}


void
print_profile(FILE *output, struct data_profile *prof, const char *prefix)
{
    fprintf(output,
            "%-27s %10d %10d %10d %10d\n",
            prefix, prof->miss_cnt, prof->nan_cnt,
            prof->pinf_cnt, prof->minf_cnt);
}


int
print_summary(FILE *output, GT3_Varbuf *var)
{
    int z, zmax;
    struct data_profile prof;
    char item[32], prefix[32];
    void (*get_dataprof)(const void *, int, struct data_profile *);
    int (*pack_slice)(void *, const GT3_Varbuf *);
    int zstr, rval, len = 0, elem_size;
    void *data;

    if (GT3_readVarZ(var, 0) < 0)
        return -1;

    GT3_getVarAttrStr(item, sizeof item, var, "ITEM");
    GT3_getVarAttrInt(&zstr, var, "ASTR3");

    if (var->type == GT3_TYPE_FLOAT) {
        get_dataprof = get_dataprof_float;
        pack_slice   = pack_slice_float;
        elem_size    = 4;
    } else {
        get_dataprof = get_dataprof_double;
        pack_slice   = pack_slice_double;
        elem_size    = 8;
    }

    if (slicing) {
        int xlen, ylen;

        xlen = min(var->dimlen[0], xrange[1] - xrange[0]);
        ylen = min(var->dimlen[1], yrange[1] - yrange[0]);
        if (xlen <= 0 || ylen <= 0
            || (data = malloc(xlen * ylen * elem_size)) == NULL)
            return -1;
    } else {
        data = var->data;
        len  = var->dimlen[0] * var->dimlen[1];
    }

    init_profile(&prof);
    prof.miss = var->miss;

    rval = -1;
    zmax = min(zrange[1], var->dimlen[2]);
    for (z = zrange[0]; z < zmax; z++) {
        if ((rval = GT3_readVarZ(var, z)) < 0)
            break;

        if (slicing)
            len = pack_slice(data, var);

        get_dataprof(data, len, &prof);
        if (each_plane) {
            snprintf(prefix, sizeof prefix, "%8d %-12s %5d",
                     var->fp->curr + 1, item, zstr + z);
            print_profile(output, &prof, prefix);
            init_profile(&prof);
        }
    }

    if (slicing)
        free(data);

    if (rval == 0 && !each_plane) {
        snprintf(prefix, sizeof prefix, "%8d %-12s %5s",
                 var->fp->curr + 1, item, "");
        print_profile(output, &prof, prefix);
    }
    return rval;
}


int
summ_file(const char *path, struct sequence *seq)
{
    GT3_File *fp = NULL;
    GT3_Varbuf *var;
    int stat;

    if ((fp = GT3_open(path)) == NULL
        || (var = GT3_getVarbuf(fp)) == NULL) {
        GT3_close(fp);
        GT3_printErrorMessages(stderr);
        return -1;
    }

    print_caption(stdout, path);
    if (seq == NULL)
        while (!GT3_eof(fp)) {
            if (print_summary(stdout, var) < 0 || GT3_next(fp) < 0) {
                GT3_printErrorMessages(stderr);
                break;
            }
        }
    else
        while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
            if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
                break;

            if (stat == ITER_OUTRANGE)
                continue;

            print_summary(stdout, var);
        }

    GT3_freeVarbuf(var);
    GT3_close(fp);
    return 0;
}


static int
set_range(int range[], const char *str)
{
    int nf;

    if ((nf = get_ints(range, 2, str, ':')) < 0)
        return -1;

    /*
     *  XXX
     *  transform
     *   FROM  1-offset and closed bound    [X,Y] => do i = X, Y.
     *   TO    0-offset and semi-open bound [X,Y) => for (i = X; i < Y; i++).
     */
    range[0]--;
    if (range[0] < 0)
        range[0] = 0;
    if (nf == 1)
        range[1] = range[0] + 1;
    return 0;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: ngtsumm [options] [files...]\n"
        "\n"
        "Print the number of grids of MISS, NaN, and +/-Inf.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -l        print for each Z-plane\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z RANGE  specify Z-range\n"
        "\n"
        "    RANGE  := start[:[end]] | :[end]\n"
        "    LIST   := RANGE[,RANGE]*\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    int ch, rval;
    struct sequence *seq = NULL;

    while ((ch = getopt(argc, argv, "hlt:x:y:z:")) != EOF)
        switch (ch) {
        case 'l':
            each_plane = 1;
            break;

        case 't':
            seq = initSeq(optarg, 1, 0x7fffffff);
            break;

        case 'x':
            slicing = 1;
            set_range(xrange, optarg);
            break;

        case 'y':
            slicing = 1;
            set_range(yrange, optarg);
            break;

        case 'z':
            set_range(zrange, optarg);
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
    for (; argc > 0 && argv; --argc, ++argv) {
        if (seq)
            reinitSeq(seq, 1, 0x7fffffff);

        if (summ_file(*argv, seq) < 0)
            rval = 1;
    }
    return rval;
}
