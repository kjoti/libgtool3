/*
 * ngtsd.c -- calculate SD / RSME.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "dateiter.h"
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
    double *data;               /* sum of (X[i] - A[i])**2 */
    double *ref;                /* A[i]: reference data */
    unsigned *cnt;              /* N[i]: number of samples (each grid) */
    double miss;                /* missing value in reference data */
    int shape[3];               /* data shape */
    size_t len;

    size_t reserved;            /* reserved data area size (in byte) */
    unsigned numset;            /* the number of used dataset */

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


static void
init_stddev(struct stddev *sd)
{
    memset(sd, 0, sizeof(struct stddev));
}


static void
free_stddev(struct stddev *sd)
{
    free(sd->data);
    free(sd->cnt);
    init_stddev(sd);
}


/*
 * resize data area size.
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

        sd->data = dptr;
        sd->ref = dptr + len;
        sd->cnt = uptr;
    }

    sd->shape[0] = dimlen[0];
    sd->shape[1] = dimlen[1];
    sd->shape[2] = dimlen[2];
    sd->len = len;
    return 0;
}


/*
 * reinit stddev data with reference data ('var').
 */
static int
reinit_stddev(struct stddev *sd, GT3_Varbuf *var)
{
    size_t i, hlen;
    int z;

    if (resize_stddev(sd, var->fp->dimlen) < 0)
        return -1;

    /*
     * set reference data.
     */
    hlen = (size_t)sd->shape[0] * sd->shape[1];
    for (z = 0; z < sd->shape[2]; z++) {
        if (GT3_readVarZ(var, z) < 0
            || GT3_copyVarDouble(sd->ref + z * hlen, hlen,
                                 var, 0, 1) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
    }
    sd->miss = var->miss;

    /*
     * clean up.
     */
    for (i = 0; i < sd->len; i++) {
        sd->data[i] = 0.;
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
    double x, a, *sdata, *ref;
    unsigned *cnt;
    size_t i, hlen;
    int n, z, zlen;

    /*
     * check the shape of input var.
     */
    if (sd->shape[0] != var->fp->dimlen[0]
        || sd->shape[1] != var->fp->dimlen[1]) {
        logging(LOG_ERR, "Mismatch data shape: %s (No.%d)",
                var->fp->path, var->fp->curr + 1);
        return -1;
    }

    if (g_zseq) {
        reinitSeq(g_zseq, 1, var->fp->dimlen[2]);
        zlen = countSeq(g_zseq);
    } else
        zlen = min(var->fp->dimlen[2], g_zrange.end) - max(0, g_zrange.str);

    if (sd->shape[2] != zlen) {
        logging(LOG_ERR, "Mismatch data shape: %s (No.%d)",
                var->fp->path, var->fp->curr + 1);
        return -1;
    }

    /* copy meta-data. */
    if (sd->numset == 0 && GT3_readHeader(&sd->head, var->fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    /*
     * Add (x - a)**2.
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

        sdata = sd->data + n * hlen;
        ref = sd->ref + n * hlen;
        cnt = sd->cnt + n * hlen;

        if (var->type == GT3_TYPE_FLOAT) {
            float *data = var->data;

            for (i = 0; i < hlen; i++) {
                x = (double)data[i];
                a = ref[i];
                if (a != sd->miss && x != var->miss) {
                    x -= a;
                    sdata[i] += x * x;
                    cnt[i]++;
                }
            }
        } else {
            double *data = var->data;

            for (i = 0; i < hlen; i++) {
                x = data[i];
                a = ref[i];
                if (a != sd->miss && x != var->miss) {
                    x -= a;
                    sdata[i] += x * x;
                    cnt[i]++;
                }
            }
        }
    }

    sd->numset++;
    return 0;
}


static int
write_stddev(const struct stddev *sd, FILE *fp)
{
    GT3_HEADER head;
    char field[17];
    int rval;

    GT3_copyHeader(&head, &sd->head);

    /* ASTR3 */
    GT3_setHeaderInt(&head, "ASTR3", g_zrange.str + 1);
    if (g_zseq) {
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
        GT3_setHeaderInt(&head, "ASTR3", 1);
    }

    /* EDIT */
    snprintf(field, sizeof field - 1, "SD=%d", sd->numset);
    GT3_setHeaderEdit(&head, field);

    rval = GT3_write(sd->data, GT3_TYPE_DOUBLE,
                     sd->shape[0], sd->shape[1], sd->shape[2],
                     &head, g_format, fp);
    if (rval < 0)
        GT3_printErrorMessages(stderr);

    return rval;
}


/*
 * calculate the final result.
 */
static void
calc_stddev(struct stddev *sd)
{
    size_t i;

    for (i = 0; i < sd->len; i++)
        sd->data[i] = (sd->cnt[i] == 0)
            ? sd->miss
            : sqrt(sd->data[i] / sd->cnt[i]);
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
    logging(LOG_INFO, "Open %s", path);

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

        if (add_newdata(sd, var) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_close(fp);
    return rval;
}


static int
ngtsd_cyc(const char *refpath, char **ppath, int nfile,
          struct sequence *seq, FILE *ofp)
{
    GT3_File *fp = NULL, *fpref = NULL;
    GT3_Varbuf *var = NULL, *varref = NULL;
    struct stddev sd;
    int n, rval = -1;

    if ((fpref = GT3_open(refpath)) == NULL
        || (varref = GT3_getVarbuf(fpref)) == NULL) {
        GT3_printErrorMessages(stderr);
        goto finish;
    }

    init_stddev(&sd);
    while (nextSeq(seq)) {
        if (reinit_stddev(&sd, varref) < 0)
            goto finish;

        for (n = 0; n < nfile; n++) {
            if ((fp = GT3_open(ppath[n])) == NULL
                || GT3_seek(fp, seq->curr - 1, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }

            if (var == NULL) {
                if ((var = GT3_getVarbuf(fp)) == NULL) {
                    GT3_printErrorMessages(stderr);
                    goto finish;
                }
            } else
                GT3_reattachVarbuf(var, fp);

            if (add_newdata(&sd, var) < 0)
                goto finish;

            GT3_close(fp);
            fp = NULL;
        }

        calc_stddev(&sd);
        if (write_stddev(&sd, ofp) < 0)
            goto finish;

        if (GT3_next(fpref) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    }
    rval = 0;

finish:
    GT3_close(fpref);
    GT3_close(fp);
    GT3_freeVarbuf(varref);
    GT3_freeVarbuf(var);
    free_stddev(&sd);
    return rval;
}


static void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] reffile file1 ...\n"
        "\n"
        "Standard Deviation or Root Mean Square Error.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        append to output file\n"
        "    -c        cyclic mode\n"
        "    -f fmt    specify output format\n"
        "    -o path   specify output filename\n"
        "    -t LIST   specify data No. to stddev\n"
        "    -v        be verbose\n"
        "    -z LIST   specify z-layer\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int ch, exitval = 1;
    const char *opath = NULL;
    FILE *output;
    char *mode = "wb";
    enum { SEQUENCE_MODE, CYCLIC_MODE };
    int sdmode = SEQUENCE_MODE;
    char dummy[17];

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "acf:ho:t:vz:")) != -1)
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
                logging(LOG_ERR, "%s: Unknown format name", optarg);
                exit(1);
            }
            g_format = strdup(optarg);
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

    if (argc < 2) {
        logging(LOG_NOTICE, "No input data");
        usage();
        exit(1);
    }

    if ((output = fopen(opath, mode)) == NULL) {
        logging(LOG_SYSERR, opath);
        exit(1);
    }

    if (sdmode == CYCLIC_MODE) {
        int chmax;

        if ((chmax = GT3_countChunk(*(argv + 1))) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }

        if (!seq)
            seq = initSeq(":", 1, chmax);

        reinitSeq(seq, 1, chmax);
        if (ngtsd_cyc(*argv, argv + 1, argc - 1, seq, output) < 0) {
            logging(LOG_ERR, "failed to process in cyclic mode.");
            goto finish;
        }
    } else {
        struct stddev sd;
        GT3_File *reffp;
        GT3_Varbuf *refvar;

        init_stddev(&sd);

        /*
         * the first argument.
         */
        if ((reffp = GT3_open(*argv)) == NULL
            || (refvar = GT3_getVarbuf(reffp)) == NULL) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }

        if (reinit_stddev(&sd, refvar) < 0)
            goto finish;

        GT3_freeVarbuf(refvar);
        GT3_close(reffp);

        /*
         * the rest arguments.
         */
        argc--;
        argv++;
        for (;argc > 0 && *argv; argc--, argv++) {
            if (ngtsd_seq(&sd, *argv, seq) < 0) {
                logging(LOG_ERR, "failed to process %s.", *argv);
                goto finish;
            }
            if (seq)
                reinitSeq(seq, 1, RANGE_MAX);
        }

        calc_stddev(&sd);
        if (write_stddev(&sd, output) < 0) {
            logging(LOG_ERR, opath);
            goto finish;
        }
    }
    exitval = 0;

finish:
    fclose(output);
    return exitval;
}
