/*
 *  ngtavr.c -- average by time
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
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

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtavr"

/*
 *  globals data
 */
static double  *g_vardata;
static double  *g_weight;
static size_t g_dimlen[3];      /* dimension length */
static GT3_HEADER g_head;       /* gt3-header of the 1st chunk */
static GT3_Date g_date1;
static GT3_Date g_date2;
static double g_miss_value;

static int g_counter;           /* # of chunks integrated */
static double g_totaltdur;
static int g_snapshot_done;

/*
 *  global options
 */
static const char *default_ofile = "gtool.out";
static int g_zrange[] = { 0, 0x7ffffff };
static int g_zsliced = 0;
static int calendar_type = GT3_CAL_GREGORIAN;
static int ignore_tdur = 0;
static double limit_factor = 0.;
static char *g_format = "UR8";

void
clear_global()
{
    int i, len;

    len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];
    for (i = 0; i < len; i++) {
        g_vardata[i] = 0.;
        g_weight[i] = 0.;
    }
    g_counter = 0;
    g_totaltdur = 0.;
    g_snapshot_done = 0;

    GT3_setDate(&g_date1, 0, 1, 1, 0, 0, 0);
    GT3_setDate(&g_date2, 9999, 1, 1, 0, 0, 0);
}


int
init_global(GT3_Varbuf *var)
{
    size_t len;
    int zrange[2];
    int zlen;
    double *tempptr = NULL;

    if (g_zsliced) {
        zrange[0] = g_zrange[0];
        zrange[1] = g_zrange[1];

        if (zrange[1] > var->dimlen[2])
            zrange[1] = var->dimlen[2];

        zlen = zrange[1] - zrange[0];
        if (zlen < 0) {
            logging(LOG_ERR, "invalid z-range");
            exit(1);
            /* return -1; */
        }
    } else
        zlen = var->dimlen[2];


    len = var->dimlen[0] * var->dimlen[1] * zlen;

    if (len > g_dimlen[0] * g_dimlen[1] * g_dimlen[2]
        && (tempptr = (double *)malloc(2 * sizeof(double) * len)) == NULL) {
        logging(LOG_SYSERR, "allocation failed.");
        exit(1);
        /* return -1; */
    }

    if (tempptr) {
        free(g_vardata);

        g_vardata = tempptr;
        g_weight = tempptr + len;
    }
    g_dimlen[0] = var->dimlen[0];
    g_dimlen[1] = var->dimlen[1];
    g_dimlen[2] = zlen;

    g_miss_value = -999.0;      /* by default */
    GT3_decodeHeaderDouble(&g_miss_value, &g_head, "MISS");

    clear_global();
    return 0;
}


int
get_calendar_type(const char *path)
{
    const char *cname[] = {
        "gregorian", "noleap", "all_leap", "360_day",
        "julian", "???"
    };
    int ctype;

    ctype = GT3_guessCalendarFile(path);
    if (ctype != GT3_CAL_GREGORIAN)
        logging(LOG_NOTICE, "CalendarType: %s", cname[ctype]);

    if (ctype == GT3_CAL_DUMMY) {
        logging(LOG_WARN, "Calendar type cannot be determined.");
        ctype = GT3_CAL_GREGORIAN;
    }
    return ctype;
}


/*
 *  time-integral
 *
 *  'vsum':  an integrated value for each grid point.
 *  'tsum':  sum of delta-t for each grid point.
 */
int
time_integral(double *vsum, double *tsum,
              GT3_Varbuf *var, double weight, int z0, int z1)
{
    int len = var->dimlen[0] * var->dimlen[1];
    int rval = 0;
    int i, z;

    for (z = z0; z < z1; z++) {
        if (GT3_readVarZ(var, z) < 0) {
            GT3_printErrorMessages(stderr);
            rval = -1;
            break;
        }

        if (var->type == GT3_TYPE_DOUBLE) {
            double miss = var->miss;
            double *data = var->data;

            for (i = 0; i < len; i++)
                if (data[i] != miss) {
                    vsum[i] += data[i] * weight;
                    tsum[i] += weight;
                }
        } else {
            float miss = var->miss;
            float *data = var->data;

            for (i = 0; i < len; i++)
                if (data[i] != miss) {
                    vsum[i] += data[i] * weight;
                    tsum[i] += weight;
                }
        }
        vsum += len;
        tsum += len;
    }
    return rval;
}


int
check_input(const GT3_Varbuf *var, const GT3_HEADER *head)
{
    if (var->dimlen[0] != g_dimlen[0]
        || var->dimlen[1] != g_dimlen[1]) {
        logging(LOG_ERR, "Dimension mismatch in averaging data.");
        return -1;
    }

    /*
     *  check ITEM.
     */
    if (memcmp(head->h + 32, g_head.h + 32, 16) != 0) {
        char from[17], to[17];

        GT3_copyHeaderItem(from, sizeof from, &g_head, "ITEM");
        GT3_copyHeaderItem(to,   sizeof to,   head,    "ITEM");
        logging(LOG_WARN, "ITEM changed from %s to %s.", from, to);
        logging(LOG_WARN, "at %d in %s", var->fp->curr + 1, var->fp->path);
    }
    return 0;
}


void
set_zrange(int *z0, int *z1, int zend)
{
    int zstr = 0;

    if (g_zsliced) {
        zstr = g_zrange[0];
        if (g_zrange[1] < zend)
            zend = g_zrange[1];
    } else
        if (zend > g_dimlen[2])
            zend = g_dimlen[2];

    *z0 = zstr;
    *z1 = zend;
}


/*
 *  return time-duration value in HOUR.
 */
double
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
         *  XXX: Wrong DATA1/DATE2 fields often happen.
         *  So we use TDUR rather than DATE[12] if TDUR > 0.
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


int
write_average(FILE *fp)
{
    int rval;
    GT3_Date date, origin;
    GT3_HEADER head;
    double time;
    int timeunit;

    GT3_copyHeader(&head, &g_head);

    /*
     *  set DATE1, DATE2 and TDUR.
     */
    GT3_setHeaderDate(&head, "DATE1", &g_date1);
    GT3_setHeaderDate(&head, "DATE2", &g_date2);
    GT3_setHeaderInt(&head, "TDUR", (int)(g_totaltdur + 0.5));

    if (   GT3_checkDate(&g_date1, calendar_type) < 0
        || GT3_checkDate(&g_date2, calendar_type) < 0)
        date = g_date1;

    /*
     *  set DATE
     */
    GT3_midDate(&date, &g_date1, &g_date2, calendar_type);
    GT3_setHeaderDate(&head, "DATE", &date);

    /*
     *  set TIME
     */
    timeunit = GT3_decodeHeaderTunit(&head);
    if (timeunit < 0) {
        GT3_printErrorMessages(stderr);

        logging(LOG_NOTICE, "overwrite UTIM=HOUR");
        GT3_setHeaderString(&head, "UTIM", "HOUR");
        timeunit = GT3_UNIT_HOUR;
    }
    GT3_setDate(&origin, 0, 1, 1, 0, 0, 0);
    time = GT3_getTime(&date, &origin, timeunit, calendar_type);
    GT3_setHeaderInt(&head, "TIME", (int)time);
    if (time != (int)time)
        logging(LOG_NOTICE, "TIME(=%.2f) is truncated to %d",
                time, (int)time);

    if (g_zsliced) {
        GT3_setHeaderInt(&head, "ASTR3", g_zrange[0] + 1);
        GT3_setHeaderInt(&head, "AEND3", g_zrange[1]    );
    }

    /*
     *  set EDIT & ETTL
     */
    GT3_setHeaderEdit(&head, "TM");
    {
        char hbuf[17];

        snprintf(hbuf, sizeof hbuf,
                 "av %02d%02d%02d-%02d%02d%02d",
                 g_date1.year % 100, g_date1.mon, g_date1.day,
                 g_date2.year % 100, g_date2.mon, g_date2.day);

        GT3_setHeaderEttl(&head, hbuf);
    }

    logging(LOG_INFO,
            "Write AVE(from %d-%02d-%02d %02d:%02d:%02d"
            " to %d-%02d-%02d %02d:%02d:%02d)",
            g_date1.year, g_date1.mon, g_date1.day,
            g_date1.hour, g_date1.min, g_date1.sec,
            g_date2.year, g_date2.mon, g_date2.day,
            g_date2.hour, g_date2.min, g_date2.sec);

    rval = GT3_write(g_vardata, GT3_TYPE_DOUBLE,
                     g_dimlen[0], g_dimlen[1], g_dimlen[2],
                     &head, g_format, fp);
    if (rval < 0)
        GT3_printErrorMessages(stdout);

    return rval;
}


void
average()
{
    size_t i, len;
    double tdur_lowlim;

    len = g_dimlen[0] * g_dimlen[1] * g_dimlen[2];

    /*
     *  The lower limit of time-duration.
     */
    tdur_lowlim = limit_factor * g_totaltdur;

    for (i = 0; i < len; i++) {
        if (g_weight[i] < tdur_lowlim || g_weight[i] == 0.)
            g_vardata[i] = g_miss_value;
        else
            g_vardata[i] /= g_weight[i];
    }
}


/*
 *  integrate_chunk() integrates a current chunk.
 *
 *  This function updates some global variable.
 */
int
integrate_chunk(GT3_Varbuf *var)
{
    double dt;
    int z0, z1;
    char item[17];
    GT3_HEADER head;
    GT3_Date date1, date2;
    int date_missing = 0;


    if (GT3_readHeader(&head, var->fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (g_counter > 0 && check_input(var, &head) < 0)
        return -1;

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
     *  get time-stepsize in HOUR.
     */
    dt = get_tstepsize(&head, &date1, &date2, date_missing);
#if 0
    if (!g_snapshot_done && dt == 0.) {
        logging(LOG_NOTICE, "Processing snapshot data...");
        g_snapshot_done = 1;
    }
#endif
    if (dt < 0.) {
        logging(LOG_WARN, "Negative time-duration: %f (hour)", dt);
        dt = 0.;
    }

    set_zrange(&z0, &z1, var->fp->dimlen[2]);

    GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
    logging(LOG_INFO, "Read %s (No. %d)", item, var->fp->curr + 1);

    if (time_integral(g_vardata, g_weight, var,
                      ignore_tdur || dt == 0. ? 1. : dt,
                      z0, z1) < 0)
        return -1;

    /*
     *  update global data.
     */
    if (g_counter == 0) {
        GT3_copyHeader(&g_head, &head);
        g_date1 = date1;
    }
    g_totaltdur += dt;
    g_date2 = date2;
    g_counter++;
    return 0;
}


/*
 *  ngtavr_seq() averages specifed chunks.
 */
int
ngtavr_seq(const char *path, struct sequence *seq)
{
    static GT3_Varbuf *var = NULL;
    GT3_File *fp;
    int rval = 0;
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
            GT3_close(fp);
            return -1;
        }
        init_global(var);
    } else {
        /*
         *  Replace file-pointer in Varbuf.
         */
        GT3_reattachVarbuf(var, fp);
    }

    if (!seq) {
        while (!GT3_eof(fp)) {
            if (integrate_chunk(var) < 0) {
                rval = -1;
                break;
            }
            if (GT3_next(fp) < 0) {
                GT3_printErrorMessages(stderr);
                rval = -1;
                break;
            }
        }
    } else {
        while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
            if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
                rval = -1;
                break;
            }
            if (stat == ITER_OUTRANGE)
                continue;

            if (integrate_chunk(var) < 0) {
                rval = -1;
                break;
            }
        }
    }
    GT3_close(fp);
    return rval;
}


/*
 *  ngtavr_eachstep() averages data for each time-duration.
 */
int
ngtavr_eachstep(const char *path, const GT3_Date *step,
                struct sequence *seq, FILE *output)
{
    static GT3_Varbuf *var = NULL;
    static DateIterator it;
    static int last = 0x7fffffff;
    GT3_Date date;
    GT3_File *fp;
    int rval = 0;
    int diff;


    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    logging(LOG_INFO, "Open %s", path);

    if (var == NULL) {
        /*
         *  XXX: 'seq' is applied ONLY the first time.
         */
        if (seq) {
            nextSeq(seq);

            if (GT3_seek(fp, seq->curr - 1, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                return -1;
            }
            logging(LOG_INFO, "At first, skipping to %d", fp->curr + 1);

            last = seq->tail;
        }

        calendar_type = get_calendar_type(path);

        if (GT3_readHeader(&g_head, fp) < 0
            || (var = GT3_getVarbuf(fp)) == NULL) {
            GT3_printErrorMessages(stderr);
            GT3_close(fp);
            return -1;
        }

        if (GT3_decodeHeaderDate(&date, &g_head, "DATE1") < 0) {
            logging(LOG_ERR, "DATE1 is missing");
            GT3_close(fp);
            return -1;
        }

        setDateIterator(&it, &date, step, calendar_type);
        init_global(var);
    } else {
        /*
         *  Replace file-pointer in Varbuf.
         */
        GT3_reattachVarbuf(var, fp);
    }

    while (!GT3_eof(fp) && fp->curr < last) {
        if (integrate_chunk(var) < 0) {
            rval = -1;
            break;
        }

        diff = cmpDateIterator(&it, &g_date2);
        if (diff > 0)
            logging(LOG_WARN, "Too large time-duration in the input data");

        if (diff >= 0) {
            average();
            write_average(output);
            clear_global();

            nextDateIterator(&it);
        }

        if (GT3_next(fp) < 0) {
            GT3_printErrorMessages(stderr);
            rval = -1;
            break;
        }
    }

    GT3_close(fp);
    return rval;
}


int
ngtavr_cyc(char **ppath, int nfile, struct sequence *seq, FILE *ofp)
{
    GT3_File *fp;
    GT3_Varbuf *var = NULL;
    int n;

    while (nextSeq(seq)) {
        for (n = 0; n < nfile; n++) {
            fp = NULL;
            if ((fp = GT3_open(ppath[n])) == NULL
                || GT3_seek(fp, seq->curr - 1, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                GT3_close(fp);
                return -1;
            }

            if (var == NULL) {
                if ((var = GT3_getVarbuf(fp)) == NULL) {
                    GT3_printErrorMessages(stderr);
                    GT3_close(fp);
                    return -1;
                }
            } else
                GT3_reattachVarbuf(var, fp);

            if (n == 0)
                init_global(var);

            if (integrate_chunk(var) < 0)
                return -1;

            GT3_close(fp);
        }

        average();
        if (write_average(ofp) < 0) {
            logging(LOG_ERR, "failed to output");
            return -1;
        }
    }

    GT3_freeVarbuf(var);
    return 0;
}


/*
 *  parse an argument of the -m option.
 */
int
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


void
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
        "    -v        be verbose\n";
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
    int ch, rval = 0;
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

    while ((ch = getopt(argc, argv, "acf:l:hm:no:t:v")) != -1)
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
            seq = initSeq(optarg, 1, 0x7fffffff);
            break;

        case 'v':
            set_logging_level("verbose");
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
        exit(0);
    }


    if ((ofp = fopen(ofile, mode)) == NULL) {
        logging(LOG_SYSERR, ofile);
        exit(1);
    }

    if (avrmode == EACH_TIMESTEP_MODE) {
        for (;argc > 0 && *argv; argc--, argv++)
            if (ngtavr_eachstep(*argv, &step, seq, ofp) < 0) {
                logging(LOG_ERR, "failed to process %s.", *argv);
                exit(1);
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
            rval = 1;
        }
    } else {
        for (;argc > 0 && *argv; argc--, argv++) {
            if (ngtavr_seq(*argv, seq) < 0) {
                logging(LOG_ERR, "failed to process %s.", *argv);
                exit(1);
            }
            if (seq)
                reinitSeq(seq, 1, 0x7fffffff);
        }

        average();
        if (write_average(ofp) < 0) {
            logging(LOG_ERR, ofile);
            rval = 1;
        }
    }

    fclose(ofp);

    return rval;
}
