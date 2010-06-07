/*
 * ngtstat.c -- print statistical info in gtool-files.
 */
#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "functmpl.h"
#include "myutils.h"
#include "logging.h"
#include "range.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtstat"
#define RANGE_MAX 0x7fffffff
static struct range g_range[] = {
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
    { 0, RANGE_MAX }
};
static struct sequence *g_zseq = NULL;
static int slicing = 0;
static int each_plane = 1;

/* stat for each layer */
struct statics {
    int zidx;                   /* index of z-layers */
    int count;                  /* the # of samples */
    double avr;                 /* average */
    double sd;                  /* standard deviation */
    double min, max;            /* min & max */
};


#define FUNCTMPL_PACK(TYPE, NAME) \
int \
NAME(void *ptr, const GT3_Varbuf *var, const struct range *range) \
{ \
    const TYPE *data = (const TYPE *)var->data; \
    TYPE *output = (TYPE *)ptr; \
    TYPE miss = (TYPE)var->miss; \
    int i, j, off; \
 \
    if (!slicing) \
        for (i = 0; i < var->dimlen[0] * var->dimlen[1]; i++) { \
            if (data[i] == miss) \
                continue; \
            *output++ = data[i]; \
        } \
    else \
        for (j = range[1].str; j < range[1].end; j++) { \
            off = j * var->dimlen[0]; \
            for (i = range[0].str; i < range[0].end; i++) { \
                if (data[off + i] == miss) \
                    continue; \
                *output++ = data[off + i]; \
            } \
        } \
    return output - (TYPE *)ptr; \
}


/*
 *   for float
 */
FUNCTMPL_PACK(float, pack_float)
FUNCTMPL_MINVAL(double, float, minvalf)
FUNCTMPL_MAXVAL(double, float, maxvalf)
FUNCTMPL_AVR(double, float, averagef)
FUNCTMPL_SDEVIATION(double, float, std_deviationf)

/*
 *  for double
 */
FUNCTMPL_PACK(double, pack_double)
FUNCTMPL_MINVAL(double, double, minval)
FUNCTMPL_MAXVAL(double, double, maxval)
FUNCTMPL_AVR(double, double, average)
FUNCTMPL_SDEVIATION(double, double, std_deviation)


static void
print_caption(const char *name)
{
    const char *z = each_plane ? "Z" : "";

    printf("# Filename: %s\n", name);
    printf("# %3s %-8s %3s %11s %11s %11s %11s %10s\n",
           "No.", "ITEM", z, "AVE", "SD", "MIN", "MAX", "NUM");
}


/*
 *  sumup statistical data in z-planes.
 */
static void
sumup_stat(struct statics *stat, const struct statics sz[], int len)
{
    int i;

    stat->min = HUGE_VAL;
    stat->max = -HUGE_VAL;
    for (i = 0; i < len; i++) {
        stat->count += sz[i].count;
        stat->avr   += sz[i].count * sz[i].avr;
        stat->min   =  min(stat->min, sz[i].min);
        stat->max   =  max(stat->max, sz[i].max);
    }

    if (stat->count > 0) {
        double var = 0.;
        double adiff;

        stat->avr /= stat->count;
        for (i = 0; i < len; i++) {
            adiff = stat->avr - sz[i].avr;
            var += sz[i].count * (sz[i].sd * sz[i].sd + adiff * adiff);
        }
        var /= stat->count;
        stat->sd = sqrt(var);
    }
}


static void
ngtstat_plane(struct statics *stat, const GT3_Varbuf *varbuf,
              void *work,
              const struct range *range)
{
    double avr = 0., sd = 0.;
    int len;
    int (*avr_func)(double *, const void *, int);
    int (*sd_func)(double *, const void *, double, int);
    int (*pack_func)(void *, const GT3_Varbuf *, const struct range *);
    double (*min_func)(const void *, int);
    double (*max_func)(const void *, int);

    if (varbuf->type == GT3_TYPE_FLOAT) {
        pack_func = pack_float;
        min_func = minvalf;
        max_func = maxvalf;
        avr_func = averagef;
        sd_func  = std_deviationf;
    } else {
        pack_func = pack_double;
        min_func = minval;
        max_func = maxval;
        avr_func = average;
        sd_func  = std_deviation;
    }

    len = pack_func(work, varbuf, range);

    avr_func(&avr, work, len);
    sd_func(&sd, work, avr, len);

    stat->count = len;
    stat->avr   = avr;
    stat->sd    = sd;
    stat->min   = min_func(work, len);
    stat->max   = max_func(work, len);
}


static void
print_stat(const struct statics *stat, int num, int tidx,
           const GT3_HEADER *head)
{
    char prefix[32], item[17];
    int i;

    item[0] = '\0';
    GT3_copyHeaderItem(item, sizeof item, head, "ITEM");
    snprintf(prefix, sizeof prefix, "%5d %-8s", tidx, item);

    if (each_plane) {
        for (i = 0; i < num; i++)
            printf("%14s %3d %11.5g %11.5g %11.5g %11.5g %10d\n",
                   prefix,
                   stat[i].zidx,
                   stat[i].avr,
                   stat[i].sd,
                   stat[i].min,
                   stat[i].max,
                   stat[i].count);
    }

    if (!each_plane || num > 1) {
        struct statics stat_all;

        memset(&stat_all, 0, sizeof(struct statics));
        sumup_stat(&stat_all, stat, num);
        printf("%14s ALL %11.5g %11.5g %11.5g %11.5g %10d\n",
               prefix,
               stat_all.avr,
               stat_all.sd,
               stat_all.min,
               stat_all.max,
               stat_all.count);
    }
}


int
ngtstat_var(GT3_Varbuf *varbuf)
{
    static void *work = NULL;
    static size_t worksize = 0;
    static struct statics *stat = NULL;
    static int max_num_plane = 0;
    GT3_HEADER head;
    struct range range[3];
    int n, z, znum, astr3;

    if (   GT3_readHeader(&head, varbuf->fp) < 0
        || GT3_decodeHeaderInt(&astr3, &head, "ASTR3") < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    for (n = 0; n < 3; n++) {
        range[n].str = max(0, g_range[n].str);
        range[n].end = min(varbuf->fp->dimlen[n], g_range[n].end);
    }

    znum = range[2].end - range[2].str;
    if (g_zseq) {
        reinitSeq(g_zseq, 1, varbuf->fp->dimlen[2]);
        znum = countSeq(g_zseq);
    }

    if (znum > max_num_plane) {
        free(stat);
        if ((stat = (struct statics *)
             malloc(sizeof(struct statics) * znum)) == NULL) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }
        max_num_plane = znum;
    }

    for (n = 0; n < znum; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                logging(LOG_WARN, "NOTREACHED");
                break;
            }
            z = g_zseq->curr - 1;
        } else
            z = n + range[2].str;

        if (GT3_readVarZ(varbuf, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }

        /*
         *  (re)allocate work buffer.
         */
        if (varbuf->bufsize > worksize) {
            free(work);
            if ((work = malloc(varbuf->bufsize)) == NULL) {
                logging(LOG_SYSERR, NULL);
                return -1;
            }
            worksize = varbuf->bufsize;
        }

        stat[n].zidx = z + astr3;
        ngtstat_plane(stat + n, varbuf, work, range);
    }

    print_stat(stat, znum, varbuf->fp->curr + 1, &head);
    return 0;
}


int
ngtstat(const char *path, struct sequence *seq)
{
    GT3_File *fp;
    GT3_Varbuf *var;
    int stat, rval = 0;

    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if ((var = GT3_getVarbuf(fp)) == NULL) {
        GT3_close(fp);
        GT3_printErrorMessages(stderr);
        return -1;
    }

    print_caption(path);
    if (seq == NULL)
        while (!GT3_eof(fp)) {
            if (ngtstat_var(var) < 0)
                rval = -1;

            if (GT3_next(fp) < 0) {
                GT3_printErrorMessages(stderr);
                rval = -1;
                break;
            }
        }
    else
        while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
            if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
                break;

            if (stat == ITER_OUTRANGE)
                continue;

            if (ngtstat_var(var) < 0)
                rval = -1;
        }

    GT3_freeVarbuf(var);
    GT3_close(fp);
    return rval;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] [files...]\n"
        "\n"
        "Print average(AVE), standard deviation(SD), MIN, and MAX.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        display total info of all Z-planes\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z LIST   specify Z-layers\n"
        "\n"
        "    RANGE  := start[:[end]] | :[end]\n"
        "    LIST   := RANGE[,RANGE]*\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int rval = 0;
    int ch;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "hat:x:y:z:")) != -1)
        switch (ch) {
        case 'a':
            each_plane = 0;
            break;
        case 't':
            if ((seq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'x':
            slicing = 1;
            if (get_range(g_range, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-x: invalid x-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'y':
            slicing = 1;
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
            exit(1);
            break;
        }

    argc -= optind;
    argv += optind;

    for (; argc > 0 && *argv; argc--, argv++) {
        if (seq)
            reinitSeq(seq, 1, 0x7fffffff);

        if (ngtstat(*argv, seq) < 0)
            rval = 1;
    }
    return rval;
}
