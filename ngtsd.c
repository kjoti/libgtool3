/*
 * ngtsd.c -- Calculate standard deviation.
 */
#include "internal.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"
#include "range.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtsd"

struct stddev {
    double *data1;              /* sum of X[i] */
    double *data2;              /* sum of X[i]**2 */
    unsigned *cnt;              /* N[i]: number of samples (each grid) */
    int shape[3];               /* data shape */
    size_t len;                 /* current data area size */

    size_t reserved;            /* reserved data area size */
    unsigned numset;            /* the number of used dataset */

    double miss;                /* missing value for output */
    GT3_HEADER head;            /* meta-data for output */
};

/*
 * global options
 */
#define RANGE_MAX 0x7fffffff
static struct range g_zrange = { 0, RANGE_MAX };
static struct sequence *g_zseq = NULL;
static const char *default_opath = "gtool.out";
static char *g_format = "UR4";
static int alive_limit = 10;


static void
set_alive_limit(void)
{
#ifdef HAVE_SYSCONF
    alive_limit = sysconf(_SC_OPEN_MAX) / 2;
#elif defined(OPEN_MAX)
    alive_limit = OPEN_MAX / 2;
#endif

    alive_limit = min(alive_limit, 100);
}


/*
 * required_zlevel() returns the number of vetical levels to be processed.
 */
static int
required_zlevel(int zmax)
{
    if (g_zseq) {
        reinitSeq(g_zseq, 1, zmax);
        zmax = countSeq(g_zseq);
    } else
        zmax = min(zmax, g_zrange.end) - max(0, g_zrange.str);

    return zmax;
}


static void
init_stddev(struct stddev *sd)
{
    memset(sd, 0, sizeof(struct stddev));
}


static void
free_stddev(struct stddev *sd)
{
    free(sd->data1);
    free(sd->cnt);
    init_stddev(sd);
}


/*
 * Resize data area.
 */
static int
resize_stddev(struct stddev *sd, const int *dimlen)
{
    size_t len;
    double *dptr = NULL;
    unsigned *uptr = NULL;

    len = dimlen[0];
    len *= dimlen[1];
    len *= dimlen[2];

    if (len > sd->reserved) {
        if ((dptr = malloc(2 * sizeof(double) * len)) == NULL
            || (uptr = malloc(sizeof(unsigned) * len)) == NULL) {
            free(dptr);
            free(uptr);
            logging(LOG_SYSERR, NULL);
            return -1;
        }
        free_stddev(sd);

        sd->reserved = len;

        sd->data1 = dptr;
        sd->data2 = dptr + len;
        sd->cnt = uptr;
    }

    sd->shape[0] = dimlen[0];
    sd->shape[1] = dimlen[1];
    sd->shape[2] = dimlen[2];
    sd->len = len;
    return 0;
}


/*
 * Reinit stddev data.
 */
static int
reinit_stddev(struct stddev *sd, GT3_Varbuf *var)
{
    size_t i;
    int dimlen[3];

    dimlen[0] = var->fp->dimlen[0];
    dimlen[1] = var->fp->dimlen[1];
    if ((dimlen[2] = required_zlevel(var->fp->dimlen[2])) <= 0) {
        logging(LOG_ERR, "Invalid z-level is specified with -z option.");
        return -1;
    }

    if (resize_stddev(sd, dimlen) < 0)
        return -1;

    if (GT3_readHeader(&sd->head, var->fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    sd->miss = var->miss;

    for (i = 0; i < sd->len; i++) {
        sd->data1[i] = 0.;
        sd->data2[i] = 0.;
        sd->cnt[i] = 0;
    }
    sd->numset = 0;

    return 0;
}


/*
 * Add a new dataset into stddev.
 */
static int
add_newdata(struct stddev *sd, GT3_Varbuf *var)
{
    double x, *a1, *a2;
    unsigned *cnt;
    size_t i, hlen;
    int n, z, zlen;

    /*
     * Check the shape of the input (var).
     */
    if (sd->shape[0] != var->fp->dimlen[0]
        || sd->shape[1] != var->fp->dimlen[1]) {
        logging(LOG_ERR,
                "Horizontal shape is changed: from (%dx%d) to (%dx%d).",
                sd->shape[0], sd->shape[1],
                var->fp->dimlen[0], var->fp->dimlen[1]);
        return -1;
    }
    zlen = required_zlevel(var->fp->dimlen[2]);
    if (sd->shape[2] != zlen) {
        logging(LOG_ERR, "Vertical level is changed: from %d to %d.",
                sd->shape[2], zlen);
        return -1;
    }

    /*
     * Add data.
     */
    hlen = (size_t)sd->shape[0] * sd->shape[1];
    for (n = 0; n < sd->shape[2]; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                assert(!"NOTREACHED");
            }
            z = g_zseq->curr - 1;
        } else
            z = g_zrange.str + n;

        if (GT3_readVarZ(var, z) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }

        a1 = sd->data1 + n * hlen;
        a2 = sd->data2 + n * hlen;
        cnt = sd->cnt + n * hlen;

        if (var->type == GT3_TYPE_FLOAT) {
            float *data = var->data;

            for (i = 0; i < hlen; i++) {
                x = (double)data[i];
                if (x != var->miss) {
                    a1[i] += x;
                    a2[i] += x * x;
                    cnt[i]++;
                }
            }
        } else {
            double *data = var->data;

            for (i = 0; i < hlen; i++) {
                x = data[i];
                if (x != var->miss) {
                    a1[i] += x;
                    a2[i] += x * x;
                    cnt[i]++;
                }
            }
        }
    }

    sd->numset++;

    logging(LOG_INFO, "Read from %s (No.%d).",
            var->fp->path, var->fp->curr + 1);
    return 0;
}


/*
 * Calculate the final result.
 */
static void
calc_stddev(struct stddev *sd)
{
    double r, a, v;
    size_t i;

    for (i = 0; i < sd->len; i++) {
        if (sd->cnt[i] > 0) {
            r = 1. / sd->cnt[i];
            a = sd->data1[i] * r;
            v = sd->data2[i] * r - a * a;
            v = sqrt(max(v, 0.));
        } else {
            a = sd->miss;
            v = sd->miss;
        }

        sd->data1[i] = a;       /* MEAN */
        sd->data2[i] = v;       /* SD */
    }
}


static int
write_stddev(const struct stddev *sd, FILE *fp, FILE *mfp)
{
    GT3_HEADER head, head2;
    char field[17];

    GT3_copyHeader(&head, &sd->head);

    /* ASTR3 */
    GT3_setHeaderInt(&head, "ASTR3", g_zrange.str + 1);
    if (g_zseq) {
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
        GT3_setHeaderInt(&head, "ASTR3", 1);
    }

    /* MISS */
    GT3_setHeaderMiss(&head, sd->miss);

    if (mfp)
        GT3_copyHeader(&head2, &head);

    /* EDIT & ETTL */
    GT3_setHeaderEdit(&head, "SD");
    snprintf(field, sizeof field - 1, "sd N=%u", sd->numset);
    GT3_setHeaderEttl(&head, field);

    if (GT3_write(sd->data2, GT3_TYPE_DOUBLE,
                  sd->shape[0], sd->shape[1], sd->shape[2],
                  &head, g_format, fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (mfp) {
        /* EDIT & ETTL */
        GT3_setHeaderEdit(&head2, "MEAN");
        snprintf(field, sizeof field - 1, "mean N=%u", sd->numset);
        GT3_setHeaderEttl(&head2, field);

        if (GT3_write(sd->data1, GT3_TYPE_DOUBLE,
                      sd->shape[0], sd->shape[1], sd->shape[2],
                      &head2, g_format, mfp) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
    }
    return 0;
}


static int
ngtsd_seq(struct stddev *sd, const char *path, struct sequence *seq)
{
    static GT3_Varbuf *var = NULL;
    GT3_File *fp;
    file_iterator it;
    int rval = -1;
    int stat;

    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (var == NULL) {
        if ((var = GT3_getVarbuf(fp)) == NULL) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    } else {
        if (GT3_reattachVarbuf(var, fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    }

    setup_file_iterator(&it, fp, seq);
    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        if (sd->numset == 0 && reinit_stddev(sd, var) < 0)
            goto finish;

        if (add_newdata(sd, var) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_close(fp);
    return rval;
}


static int
ngtsd_cyc(char **paths, int nfiles, struct sequence *seq,
          FILE *ofp, FILE *ofp2)
{
    GT3_File **inputs = NULL;
    GT3_File *fp = NULL;
    GT3_Varbuf *var = NULL;
    int keep_alive = nfiles <= alive_limit;
    struct stddev sd;
    int n, rval = -1;

    if ((inputs = malloc(sizeof(GT3_File *) * nfiles)) == NULL) {
        logging(LOG_SYSERR, NULL);
        return -1;
    }
    memset(inputs, 0, sizeof(GT3_File *) * nfiles);

    init_stddev(&sd);

    for (n = 0; n < nfiles; n++) {
        if ((inputs[n] = GT3_open(paths[n])) == NULL) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
        if (!keep_alive && GT3_suspend(inputs[n]) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    }

    while (nextSeq(seq)) {
        for (n = 0; n < nfiles; n++) {
            fp = inputs[n];

            if (!keep_alive && GT3_resume(fp) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }

            if (GT3_seek(fp, seq->curr - 1, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }

            if (var == NULL) {
                if ((var = GT3_getVarbuf(fp)) == NULL) {
                    GT3_printErrorMessages(stderr);
                    goto finish;
                }
            } else
                if (GT3_reattachVarbuf(var, fp) < 0) {
                    GT3_printErrorMessages(stderr);
                    goto finish;
                }

            if (n == 0 && reinit_stddev(&sd, var) < 0)
                goto finish;

            if (add_newdata(&sd, var) < 0)
                goto finish;

            if (!keep_alive && GT3_suspend(fp) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }
        }

        calc_stddev(&sd);
        if (write_stddev(&sd, ofp, ofp2) < 0)
            goto finish;
    }
    rval = 0;

finish:
    if (rval < 0 && n >= 0 && n < nfiles)
        logging(LOG_ERR, "%s: failed.", paths[n]);

    GT3_freeVarbuf(var);
    for (n = 0; n < nfiles; n++)
        GT3_close(inputs[n]);

    free(inputs);
    free_stddev(&sd);
    return rval;
}


static void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] file1 ...\n"
        "\n"
        "Output standard deviation.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        append to output file\n"
        "    -c        cyclic mode\n"
        "    -f fmt    specify output format\n"
        "    -m path   specify output filename (for Mean)\n"
        "    -o path   specify output filename (for SD)\n"
        "    -t LIST   specify data No.\n"
        "    -v        be verbose\n"
        "    -z LIST   specify z-level\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int ch, exitval = 1;
    const char *opath = NULL;
    const char *mpath = NULL;
    FILE *output, *output2 = NULL;
    char *mode = "wb";
    enum { SEQUENCE_MODE, CYCLIC_MODE };
    int sdmode = SEQUENCE_MODE;
    char dummy[17];

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    set_alive_limit();

    while ((ch = getopt(argc, argv, "acf:hm:o:t:vz:")) != -1)
        switch (ch) {
        case 'a':
            mode = "ab";
            break;

        case 'c':
            sdmode = CYCLIC_MODE;
            break;

        case 'f':
            toupper_string(optarg);
            if (GT3_output_format(dummy, optarg) < 0) {
                logging(LOG_ERR, "%s: Unknown format name.", optarg);
                exit(1);
            }
            g_format = strdup(optarg);
            break;

        case 'm':
            mpath = optarg;
            break;

        case 'o':
            opath = optarg;
            break;

        case 't':
            seq = initSeq(optarg, 1, RANGE_MAX);
            break;

        case 'v':
            set_logging_level("verbose");
            break;

        case 'z':
            if (get_seq_or_range(&g_zrange, &g_zseq,
                                 optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;
        case 'h':
            usage();
            exit(0);
        default:
            usage();
            exit(1);
            break;
        }

    if (!opath)
        opath = default_opath;

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        logging(LOG_NOTICE, "No input data.");
        usage();
        exit(1);
    }

    if ((output = fopen(opath, mode)) == NULL) {
        logging(LOG_SYSERR, opath);
        exit(1);
    }

    if (mpath) {
        if (strcmp(mpath, "-") == 0 || strcmp(mpath, opath) == 0) {
            output2 = output;
        } else {
            if ((output2 = fopen(mpath, mode)) == NULL) {
                logging(LOG_SYSERR, mpath);
                exit(1);
            }
        }
    }

    if (sdmode == CYCLIC_MODE) {
        int chmax;

        if ((chmax = GT3_countChunk(*argv)) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }

        if (!seq)
            seq = initSeq(":", 1, chmax);

        reinitSeq(seq, 1, chmax);
        if (ngtsd_cyc(argv, argc, seq, output, output2) < 0)
            goto finish;
    } else {
        struct stddev sd;

        init_stddev(&sd);
        for (;argc > 0 && *argv; argc--, argv++) {
            if (ngtsd_seq(&sd, *argv, seq) < 0) {
                logging(LOG_ERR, "%s: failed.", *argv);
                goto finish;
            }
            if (seq)
                reinitSeq(seq, 1, RANGE_MAX);
        }

        calc_stddev(&sd);
        if (write_stddev(&sd, output, output2) < 0) {
            logging(LOG_ERR, opath);
            goto finish;
        }
    }
    exitval = 0;

finish:
    if (output2 && output2 != output)
        fclose(output2);
    fclose(output);
    return exitval;
}
