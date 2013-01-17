/*
 * ngtavr.c -- average by time
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

#define PROGNAME "ngtavr"

struct average {
    double *data;
    double *wght;
    double miss;
    size_t len;
    size_t reserved_len;
    int shape[3];

    unsigned count;
    double duration;            /* in HOUR */
    double total_wght;
    GT3_Date date1, date2;
    GT3_HEADER head;
};

/*
 * global options
 */
#define RANGE_MAX 0x7fffffff
static struct range g_zrange = { 0, RANGE_MAX };
static struct sequence *g_zseq = NULL;
static const char *default_ofile = "gtool.out";

static int calendar_type = GT3_CAL_GREGORIAN;
static int ignore_tdur = 0;
static double limit_factor = 0.;
static char *g_format = "UR4";


static void
init_average(struct average *avr)
{
    memset(avr, 0, sizeof(struct average));
    assert(avr->data == NULL);
    assert(avr->wght == NULL);
}


static void
free_average(struct average *avr)
{
    free(avr->data);
    init_average(avr);
}


static int
alloc_average(struct average *avr, size_t len)
{
    double *ptr;

    if (len > avr->reserved_len) {
        if ((ptr = malloc(2 * sizeof(double) * len)) == NULL) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }
        free_average(avr);
        avr->reserved_len = len;
    } else
        ptr = avr->data;

    avr->len  = len;
    avr->data = ptr;
    avr->wght = ptr + len;
    return 0;
}


static void
clear_average(struct average *avr)
{
    int i;

    for (i = 0; i < avr->len; i++) {
        avr->data[i] = 0.;
        avr->wght[i] = 0.;
    }
    avr->count = 0;
    avr->duration = 0.;
    avr->total_wght = 0.;

    GT3_setDate(&avr->date1, 0, 1, 1, 0, 0, 0);
    GT3_setDate(&avr->date2, 9999, 1, 1, 0, 0, 0);
}


static int
setup_average(struct average *avr, GT3_Varbuf *var)
{
    struct range zrange;
    int zlen;
    int *dimlen = var->fp->dimlen;

    if (g_zseq) {
        reinitSeq(g_zseq, 1, dimlen[2]);
        zlen = countSeq(g_zseq);
    } else {
        zrange.str = max(0, g_zrange.str);
        zrange.end = min(dimlen[2], g_zrange.end);
        zlen = zrange.end - zrange.str;
    }
    if (zlen <= 0) {
        logging(LOG_ERR, "empty z-layer");
        return -1;
    }

    if (alloc_average(avr, dimlen[0] * dimlen[1] * zlen) < 0)
        return -1;

    avr->shape[0] = dimlen[0];
    avr->shape[1] = dimlen[1];
    avr->shape[2] = zlen;
    avr->miss  = -999.0;      /* by default */
    clear_average(avr);
    return 0;
}


static int
get_calendar_type(const char *path)
{
    const char *cname[] = {
        "gregorian", "noleap", "all_leap", "360_day", "julian"
    };
    int ctype;

    ctype = GT3_guessCalendarFile(path);
    if (ctype < 0)
        GT3_printErrorMessages(stderr);

    if (ctype >= 0 && ctype < sizeof cname / sizeof cname[0])
        logging(LOG_NOTICE, "CalendarType: %s", cname[ctype]);
    else {
        logging(LOG_ERR, "Unknown calendar type. Assuming Gregorian.");
        ctype = GT3_CAL_GREGORIAN;
    }
    return ctype;
}


static int
integrate(double *vsum, double *tsum,
          int len, int nz,
          GT3_Varbuf *var, double weight)
{
    int len2;
    int rval = 0;
    int i, n, z;

    len2 = min(len, var->fp->dimlen[0] * var->fp->dimlen[1]);
    if (len != len2)
        logging(LOG_WARN, "# of horizontal grids has changed.");

    if (g_zseq)
        reinitSeq(g_zseq, 1, var->fp->dimlen[2]);

    for (n = 0; n < nz; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                assert(!"NOTREACHED");
            }
            z = g_zseq->curr - 1;
        } else
            z = g_zrange.str + n;

        if (GT3_readVarZ(var, z) < 0) {
            GT3_printErrorMessages(stderr);
            continue;
        }

        if (var->type == GT3_TYPE_DOUBLE) {
            double miss = var->miss;
            double *data = var->data;

            for (i = 0; i < len2; i++)
                if (data[i] != miss) {
                    vsum[i] += data[i] * weight;
                    tsum[i] += weight;
                }
        } else {
            float miss = var->miss;
            float *data = var->data;

            for (i = 0; i < len2; i++)
                if (data[i] != miss) {
                    vsum[i] += data[i] * weight;
                    tsum[i] += weight;
                }
        }
        vsum += len; /* XXX: NOT len2 */
        tsum += len; /* XXX: NOT len2 */
    }
    return rval;
}


static int
cmp_heads(const GT3_HEADER *head1, const GT3_HEADER *head2)
{
    int rval = 0;
    char buf1[17], buf2[17];
    int pos;
    struct { const char *key; int no; } tab[] = {
        { "ITEM",   3 },
        { "UNIT",  16 },
        { "AITM1", 29 },
        { "ASTR1", 30 },
        { "AEND1", 31 },
        { "AITM2", 32 },
        { "ASTR2", 33 },
        { "AEND2", 34 },
        { "AITM3", 35 },
        { "ASTR3", 36 },
        { "AEND3", 37 }
    };
    int i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        pos = 16 * (tab[i].no - 1);

        if (memcmp(head1->h + pos, head2->h + pos, 16) != 0) {
            GT3_copyHeaderItem(buf1, sizeof buf1, head1, tab[i].key);
            GT3_copyHeaderItem(buf2, sizeof buf2, head2, tab[i].key);

            logging(LOG_WARN, "%s has changed from %s to %s.",
                    tab[i].key, buf1, buf2);
            rval = -1;
        }
    }
    return rval;
}


/*
 * return time-duration value in HOUR.
 */
static double
get_tstepsize(const GT3_HEADER *head,
              const GT3_Date *date1, const GT3_Date *date2,
              int date_missing)
{
    int tdur = 0, unit = -1;
    double dt;

    if (GT3_decodeHeaderInt(&tdur, head, "TDUR") < 0
        || (unit = GT3_decodeHeaderTunit(head)) < 0)
        GT3_printErrorMessages(stderr);

    if ((tdur > 0 && unit >= 0) || date_missing) {
        /*
         * XXX: Wrong DATA1/DATE2 fields often happen.
         * So we use TDUR rather than DATE[12] if TDUR > 0.
         */
        dt = tdur;
        switch (unit) {
        case GT3_UNIT_DAY:
            dt *= 24.;
            break;

        case GT3_UNIT_MIN:
            dt *= 1. / 60;
            break;

        case GT3_UNIT_SEC:
            dt *= 1. / 3600;
            break;
        }
    } else
        dt = GT3_getTime(date2, date1, GT3_UNIT_HOUR, calendar_type);

    return dt;
}


static int
write_average(const struct average *avr, FILE *fp)
{
    int rval;
    GT3_Date date, origin;
    GT3_HEADER head;
    double time;
    int itime;

    if (avr->count == 0)
        return 0;

    GT3_copyHeader(&head, &avr->head);

    /*
     * set DATE1, DATE2 and TDUR.
     */
    GT3_setHeaderDate(&head, "DATE1", &avr->date1);
    GT3_setHeaderDate(&head, "DATE2", &avr->date2);
    GT3_setHeaderString(&head, "UTIM", "HOUR");
    GT3_setHeaderInt(&head, "TDUR", (int)(avr->duration + 0.5));

    /*
     * set DATE
     */
    if (GT3_midDate(&date, &avr->date1, &avr->date2, calendar_type) < 0) {
        GT3_printErrorMessages(stderr);
        date = avr->date1;
    }
    GT3_setHeaderDate(&head, "DATE", &date);

    /*
     * set TIME (in HOUR)
     */
    GT3_setDate(&origin, 0, 1, 1, 0, 0, 0);
    time = GT3_getTime(&date, &origin, GT3_UNIT_HOUR, calendar_type);
    itime = (int)round(time);
    GT3_setHeaderInt(&head, "TIME", itime);
    if (time != itime)
        logging(LOG_NOTICE, "TIME(=%.2f) is truncated to %d", time, itime);

    /* ASTR3 */
    GT3_setHeaderInt(&head, "ASTR3", g_zrange.str + 1);
    if (g_zseq) {
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
        GT3_setHeaderInt(&head, "ASTR3", 1);
    }

    /*
     * set EDIT & ETTL
     */
    GT3_setHeaderEdit(&head, "TM");
    {
        char hbuf[17];

        snprintf(hbuf, sizeof hbuf,
                 "av %02d%02d%02d-%02d%02d%02d",
                 avr->date1.year % 100, avr->date1.mon, avr->date1.day,
                 avr->date2.year % 100, avr->date2.mon, avr->date2.day);

        GT3_setHeaderEttl(&head, hbuf);
    }

    logging(LOG_INFO,
            "Write AVE(from %d-%02d-%02d %02d:%02d:%02d"
            " to %d-%02d-%02d %02d:%02d:%02d)",
            avr->date1.year, avr->date1.mon, avr->date1.day,
            avr->date1.hour, avr->date1.min, avr->date1.sec,
            avr->date2.year, avr->date2.mon, avr->date2.day,
            avr->date2.hour, avr->date2.min, avr->date2.sec);

    rval = GT3_write(avr->data, GT3_TYPE_DOUBLE,
                     avr->shape[0], avr->shape[1], avr->shape[2],
                     &head, g_format, fp);
    if (rval < 0)
        GT3_printErrorMessages(stderr);

    return rval;
}


static void
average(struct average *avr)
{
    int i;
    double thres;

    thres = limit_factor * avr->total_wght;

    for (i = 0; i < avr->len; i++) {
        if (avr->wght[i] < thres || avr->wght[i] == 0.)
            avr->data[i] = avr->miss;
        else
            avr->data[i] /= avr->wght[i];
    }
}


/*
 * integrate_chunk() integrates a current chunk.
 */
static int
integrate_chunk(struct average *avr, GT3_Varbuf *var)
{
    double dt;
    char item[17];
    GT3_HEADER head;
    GT3_Date date1, date2;
    int date_missing = 0;
    double wght;


    if (GT3_readHeader(&head, var->fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (avr->count > 0 && cmp_heads(&avr->head, &head) < 0)
        logging(LOG_WARN, "at %d in %s.", var->fp->curr + 1, var->fp->path);

    if (   GT3_decodeHeaderDate(&date1, &head, "DATE1") < 0
        || GT3_decodeHeaderDate(&date2, &head, "DATE2") < 0) {

        logging(LOG_WARN, "DATE1 or DATE2 is missing (%s: %d)",
                var->fp->path, var->fp->curr + 1);

        date_missing = 1;
        if (GT3_decodeHeaderDate(&date1, &head, "DATE") < 0)
            GT3_setDate(&date1, 0, 1, 1, 0, 0, 0);

        date2 = date1;
    }

    /*
     * get time-stepsize in HOUR.
     */
    dt = get_tstepsize(&head, &date1, &date2, date_missing);
    if (dt < 0.) {
        logging(LOG_WARN, "Negative time-duration: %f (hour)", dt);
        dt = 0.;
    }
    if (dt == 0. && avr->duration > 0. && !ignore_tdur) {
        logging(LOG_ERR, "Time-duration has changed from non-zero to zero.");
        logging(LOG_ERR, "Use \"-n\" option to work around.");
        return -1;
    }

    GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");

    /*
     * integral
     */
    wght = (ignore_tdur || dt == 0.) ? 1. : dt;
    if (integrate(avr->data, avr->wght,
                  avr->shape[0] * avr->shape[1], avr->shape[2],
                  var, wght) < 0)
        return -1;


    if (avr->count == 0) {
        GT3_copyHeader(&avr->head, &head);
        avr->date1 = date1;

        GT3_decodeHeaderDouble(&avr->miss, &avr->head, "MISS");
    }
    avr->date2 = date2;
    avr->count++;
    avr->duration += dt;
    avr->total_wght += wght;

    logging(LOG_INFO, "Read %s (No.%d), weight(%g), count(%d)",
            item, var->fp->curr + 1, wght, avr->count);
    return 0;
}


/*
 * ngtavr_seq() averages specifed chunks.
 */
static int
ngtavr_seq(struct average *avr, const char *path, struct sequence *seq)
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
        calendar_type = get_calendar_type(path);

        if ((var = GT3_getVarbuf(fp)) == NULL) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
        if (setup_average(avr, var) < 0)
            goto finish;
    } else {
        /*
         * Replace file-pointer in Varbuf.
         */
        GT3_reattachVarbuf(var, fp);
    }

    setup_file_iterator(&it, fp, seq);
    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        if (integrate_chunk(avr, var) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_close(fp);
    return rval;
}


/*
 * ngtavr_eachstep() averages data for each time-duration.
 */
static int
ngtavr_eachstep(struct average *avr,
                const char *path, const GT3_Date *step,
                struct sequence *seq, FILE *output)
{
    static GT3_Varbuf *var = NULL;
    static DateIterator it;
    static int last = RANGE_MAX;
    GT3_HEADER head;
    GT3_Date date;
    GT3_File *fp;
    int rval = -1;
    int diff;


    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    logging(LOG_INFO, "Open %s", path);

    if (var == NULL) {
        /*
         * XXX: 'seq' is applied ONLY the first time.
         */
        if (seq) {
            if (nextSeq(seq) < 0) {
                logging(LOG_ERR, "invalid t-sequence");
                goto finish;
            }

            if (GT3_seek(fp, seq->curr - 1, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }
            logging(LOG_INFO, "At first, skipping to %d", fp->curr + 1);

            last = seq->tail;
        }

        calendar_type = get_calendar_type(path);

        if (GT3_readHeader(&head, fp) < 0
            || (var = GT3_getVarbuf(fp)) == NULL) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }

        if (GT3_decodeHeaderDate(&date, &head, "DATE1") < 0) {
            logging(LOG_ERR, "DATE1 is missing");
            goto finish;
        }

        setDateIterator(&it, &date, step, calendar_type);
        if (setup_average(avr, var) < 0)
            goto finish;
    } else {
        /*
         * Replace file-pointer in Varbuf.
         */
        GT3_reattachVarbuf(var, fp);
    }

    while (!GT3_eof(fp) && fp->curr < last) {
        if (integrate_chunk(avr, var) < 0)
            goto finish;

        diff = cmpDateIterator(&it, &avr->date2);
        if (diff > 0)
            logging(LOG_WARN, "Too large time-duration in the input data");

        if (diff >= 0) {
            average(avr);
            if (write_average(avr, output) < 0)
                goto finish;
            clear_average(avr);

            nextDateIterator(&it);
        }

        if (GT3_next(fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    }
    rval = 0;

finish:
    GT3_close(fp);
    return rval;
}


static int
ngtavr_cyc(char **ppath, int nfile, struct sequence *seq, FILE *ofp)
{
    GT3_File *fp = NULL;
    GT3_Varbuf *var = NULL;
    struct average avr;
    int n, rval = -1;

    init_average(&avr);
    while (nextSeq(seq)) {
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

            if (n == 0 && setup_average(&avr, var) < 0)
                goto finish;

            if (integrate_chunk(&avr, var) < 0)
                goto finish;

            GT3_close(fp);
            fp = NULL;
        }

        average(&avr);
        if (write_average(&avr, ofp) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_close(fp);
    GT3_freeVarbuf(var);
    free_average(&avr);
    return rval;
}


/*
 * parse an argument of the -m option.
 */
static int
setStepsize(GT3_Date *step, const char *str)
{
    int d[] = { 0, 0, 0, 0, 0, 0 };
    struct { const char *key; int val; } tab[] = {
        { "yr",   0   },
        { "mo",   1   },
        { "dy",   2   },
        { "hr",   3   },
        { "mn",   4   },
        { "s",    5   },

        { "year", 0   },
        { "mon",  1   },
        { "day",  2   },
        { "hour", 3   },
        { "min",  4   },
        { "sec",  5   }
    };
    char *endptr;
    int i, num;

    num = strtol(str, &endptr, 10);
    if (str == endptr || *endptr == '\0')
        return -1;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(endptr, tab[i].key) == 0)
            break;

    if (i == sizeof tab / sizeof tab[0])
        return -1;

    d[tab[i].val] = num;
    GT3_setDate(step, d[0], d[1], d[2], d[3], d[4], d[5]);
    return 0;
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
        "Usage: " PROGNAME " [options] File1 ...\n"
        "\n"
        "Average files.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -a        append to output file\n"
        "    -c        cyclic mode\n"
        "    -f fmt    specify output format\n"
        "    -l dble   specify limit factor (by default 0.)\n"
        "    -m tdur   specify time-duration\n"
        "    -n        ignore TDUR (weight of integration)\n"
        "    -o path   specify output filename\n"
        "    -t LIST   specify data No. to average\n"
        "    -v        be verbose\n"
        "    -z LIST   specify z-layer\n";
    const char *examples =
        "Examples:\n"
        "  " PROGNAME " -o Tavr y19*/T          # "
        "Average all y19*/T files.\n"
        "  " PROGNAME " -o T -m 1mo 1dy/T       # "
        "Convert daily mean to monthly mean.\n"
        "  " PROGNAME " -o T -m 1mo 6hr/T       # "
        "Convert 6-hourly mean to monthly mean.\n"
        "  " PROGNAME " -o T -m 1yr y19*/1dy/T  # "
        "Convert daily mean to annual mean.\n";


    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
    fprintf(stderr, "%s\n", examples);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int ch, exitval = 0;
    const char *ofile = NULL;
    GT3_Date step;
    FILE *ofp;
    char *mode = "wb";
    enum { SEQUENCE_MODE, EACH_TIMESTEP_MODE, CYCLIC_MODE };
    int avrmode = SEQUENCE_MODE;
    char *endptr;
    char dummy[17];


    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "acf:l:hm:no:t:vz:")) != -1)
        switch (ch) {
        case 'a':
            mode = "ab";
            break;

        case 'c':
            avrmode = CYCLIC_MODE;
            break;

        case 'f':
            toupper_string(optarg);
            if (GT3_output_format(dummy, optarg) < 0) {
                logging(LOG_ERR, "%s: Unknown format name", optarg);
                exit(1);
            }
            g_format = strdup(optarg);
            break;

        case 'l':
            limit_factor = strtod(optarg, &endptr);
            if (optarg == endptr || limit_factor < 0. || limit_factor > 1.) {
                logging(LOG_ERR,
                        "%s: Invalid argument of -l option", optarg);
                usage();
                exit(1);
            }
            break;

        case 'm':
            if (setStepsize(&step, optarg) < 0) {
                logging(LOG_ERR,
                        "%s: invalid argument for -m option",
                        optarg);
                exit(1);
            }
            avrmode = EACH_TIMESTEP_MODE;
            break;

        case 'n':
            ignore_tdur = 1;
            break;

        case 'o':
            ofile = optarg;
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


    if (!ofile)
        ofile = default_ofile;

    argc -= optind;
    argv += optind;

    if (argc == 0) {
        logging(LOG_NOTICE, "No input data");
        usage();
        exit(1);
    }

    if ((ofp = fopen(ofile, mode)) == NULL) {
        logging(LOG_SYSERR, ofile);
        exit(1);
    }

    if (avrmode == EACH_TIMESTEP_MODE) {
        struct average avr;

        init_average(&avr);
        for (;argc > 0 && *argv; argc--, argv++)
            if (ngtavr_eachstep(&avr, *argv, &step, seq, ofp) < 0) {
                logging(LOG_ERR, "failed to process %s.", *argv);
                exit(1);
            }

        if (avr.count > 0) {
            logging(LOG_INFO, "write buffered data.");
            average(&avr);
            if (write_average(&avr, ofp) < 0) {
                logging(LOG_ERR, ofile);
                exitval = 1;
            }
        }
    } else if (avrmode == CYCLIC_MODE) {
        int chmax;

        if ((chmax = GT3_countChunk(*argv)) < 0) {
            GT3_printErrorMessages(stderr);
            exit(1);
        }
        calendar_type = get_calendar_type(*argv);

        if (!seq)
            seq = initSeq(":", 1, chmax);

        reinitSeq(seq, 1, chmax);
        if (ngtavr_cyc(argv, argc, seq, ofp) < 0) {
            logging(LOG_ERR, "failed to process in cyclic mode.");
            exitval = 1;
        }
    } else {
        struct average avr;

        init_average(&avr);
        for (;argc > 0 && *argv; argc--, argv++) {
            if (ngtavr_seq(&avr, *argv, seq) < 0) {
                logging(LOG_ERR, "failed to process %s.", *argv);
                exit(1);
            }
            if (seq)
                reinitSeq(seq, 1, RANGE_MAX);
        }

        average(&avr);
        if (write_average(&avr, ofp) < 0) {
            logging(LOG_ERR, ofile);
            exitval = 1;
        }
    }
    fclose(ofp);

    return exitval;
}
