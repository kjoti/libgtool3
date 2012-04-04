/*
 * ngtick.c -- set date/time in GTOOL3 files.
 */
#include "internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "myutils.h"
#include "fileiter.h"
#include "logging.h"

#define PROGNAME "ngtick"

/*
 * 'basetime' is a date/time of the origin of time-axis.
 */
static GT3_Date basetime;
static int calendar = GT3_CAL_GREGORIAN;
static int date_validated = 0;

static int snapshot_flag = 0;
static int dryrun_mode = 0;
static struct message_buffer message_buffer;

struct message_buffer {
    int num;
    char buf[8][64];
};


static void
usage(void)
{
    static const char *messages =
        "\n"
        "Overwrite header fields related to time-axis.\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -n        dryrun mode\n"
        "    -s        specify a snapshot\n"
        "    -c CAL    specify a calendar\n"
        "    -t LIST   specify data No.\n"
        "\n"
        "    CAL : gregorian(default), noleap, all_leap, 360_day, julian\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "Usage: %s [options] time-def [files...]\n", PROGNAME);
    fprintf(stderr, "%s\n", messages);
}


static void
put_message(const char *key, const char *oldvalue, const char *newvalue)
{
    snprintf(&message_buffer.buf[message_buffer.num][0],
             sizeof message_buffer.buf[message_buffer.num],
             "%8s: (%16s) -> (%16s)",
             key, oldvalue, newvalue);
    message_buffer.num++;
}


static void
reset_message_buffer(void)
{
    message_buffer.num = 0;
}


static void
print_message_buffer(void)
{
    int i;

    for (i = 0; i < message_buffer.num; i++)
        printf("%s\n", message_buffer.buf[i]);
}


static int
modify_field(GT3_HEADER *head, const char *key, const char *new_value)
{
    char value[17];
    int rval = 0;

    GT3_copyHeaderItem(value, sizeof value, head, key);
    if (strcmp(value, new_value) != 0) {
        if (dryrun_mode)
            put_message(key, value, new_value);

        GT3_setHeaderString(head, key, new_value);
        rval = 1;
    }
    return rval;
}


static int
modify_field_int(GT3_HEADER *head, const char *key, int new_value)
{
    int rval = 0, value = 0;

    rval = GT3_decodeHeaderInt(&value, head, key);
    if (rval < 0 || value != new_value) {
        char old[17], new[17];

        if (dryrun_mode)
            GT3_copyHeaderItem(old, sizeof old, head, key);

        GT3_setHeaderInt(head, key, new_value);
        if (dryrun_mode) {
            GT3_copyHeaderItem(new, sizeof new, head, key);
            put_message(key, old, new);
        }
        rval = 1;
    }
    return rval;
}


static int
modify_field_date(GT3_HEADER *head, const char *key,
                  const GT3_Date *new_value)
{
    int rval = 0;
    GT3_Date value;

    rval = GT3_decodeHeaderDate(&value, head, key);
    if (rval < 0 || GT3_cmpDate2(&value, new_value) != 0) {
        char old[17], new[17];

        if (dryrun_mode)
            GT3_copyHeaderItem(old, sizeof old, head, key);

        GT3_setHeaderDate(head, key, new_value);
        if (dryrun_mode) {
            GT3_copyHeaderItem(new, sizeof new, head, key);
            put_message(key, old, new);
        }
        rval = 1;
    }
    return rval;
}


static int
modify_items(GT3_HEADER *head,
             const GT3_Date *lower,
             const GT3_Date *upper,
             const GT3_Date *date,
             double time, double tdur)
{
    int rval = 0;

    rval += modify_field(head, "UTIM", "HOUR");
    rval += modify_field_int(head, "TIME", time);
    rval += modify_field_int(head, "TDUR", tdur);

    rval += modify_field_date(head, "DATE",  date);
    rval += modify_field_date(head, "DATE1", lower);
    rval += modify_field_date(head, "DATE2", upper);
    return rval;
}


static double
get_time(const GT3_Date *date)
{
    return GT3_getTime(date, &basetime, GT3_UNIT_HOUR, calendar);
}


static int
tick(file_iterator *it, GT3_Date *start, const GT3_Duration *intv)
{
    GT3_Date date_bnd[2], date;
    GT3_Date *lower, *upper;
    GT3_HEADER head;
    double time_bnd[2], time, tdur;
    int i, stat;
    int modified, print_filename = 0;

    date_bnd[0] = *start;
    date_bnd[1] = *start;
    GT3_addDuration2(&date_bnd[1], intv, 1, calendar);

    time_bnd[0] = get_time(&date_bnd[0]);
    time_bnd[1] = get_time(&date_bnd[1]);

    for (i = 0; ; i ^= 1) {
        lower = &date_bnd[i];
        upper = &date_bnd[i ^ 1];

        stat = iterate_file(it);
        if (stat == ITER_END) {
            *start = *lower;
            break;
        }
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            return -1;
        if (stat == ITER_OUTRANGE)
            continue;

        if (GT3_readHeader(&head, it->fp) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }

        reset_message_buffer();

        /*
         * calculate the midpoint of the time_bnd and modify a header.
         */
        if (snapshot_flag) {
            time = time_bnd[i ^ 1]; /* use upper */

            modified = modify_items(&head, upper, upper, upper, time, 0.);
        } else {
            time = 0.5 * (time_bnd[0] + time_bnd[1]);
            tdur = time_bnd[i ^ 1] - time_bnd[i];

            GT3_midDate(&date, lower, upper, calendar);

            modified = modify_items(&head, lower, upper, &date, time, tdur);
        }

        /*
         * print notice.
         */
        if (modified && dryrun_mode) {
            if (!print_filename) {
                printf("# Filename: %s\n", it->fp->path);
                print_filename = 1;
            }
            printf("# No. %d:\n", it->fp->curr + 1);
            print_message_buffer();
        }

        /*
         * rewrite the modified header.
         */
        if (!dryrun_mode && modified
            && (fseeko(it->fp->fp, it->fp->off + 4, SEEK_SET) < 0
                || fwrite(head.h, 1, GT3_HEADER_SIZE, it->fp->fp)
                != GT3_HEADER_SIZE)) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }

        /*
         * make a step forward.
         */
        GT3_addDuration2(lower, intv, 2, calendar);
        time_bnd[i] = get_time(lower);
    }
    return 0;
}


/*
 * Set "TIME", "DATE", "TDUR", "DATE1", and "DATE2".
 */
static int
tick_file(const char *path, GT3_Date *start, const GT3_Duration *intv,
          struct sequence *seq)
{
    GT3_File *fp;
    file_iterator it;
    int ctype, rval = 0;

    if ((fp = dryrun_mode ? GT3_open(path) : GT3_openRW(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (calendar == GT3_CAL_DUMMY) {
        /*
         * determine the calendar from input file.
         */
        ctype = GT3_guessCalendarFile(path);
        if (ctype < 0)
            GT3_printErrorMessages(stderr);
        if (ctype < 0 || ctype == GT3_CAL_DUMMY) {
            logging(LOG_WARN,
                    "The input file does not have correct DATE/TIME."
                    " So the calendar type cannot be determined."
                    " Use Gregorian.");
            ctype = GT3_CAL_GREGORIAN;
        } else
            logging(LOG_NOTICE, "Calendar type is %s.",
                    GT3_calendar_name(ctype));
        calendar = ctype;
    }

    /*
     * Now, the calendar is determined. Check date.
     */
    if (!date_validated && GT3_checkDate(start, calendar) < 0) {
        rval = -1;
        logging(LOG_ERR,
                "%04d-%02d-%02d: Invalid date.",
                start->year, start->mon, start->day);
        goto finish;
    }

    date_validated = 1;
    setup_file_iterator(&it, fp, seq);
    rval = tick(&it, start, intv);

finish:
    GT3_close(fp);
    return rval;
}


static int
get_tdur(int tdur[], const char *str)
{
    struct { const char *key; int value; } tab[] = {
        { "yr", GT3_UNIT_YEAR   },
        { "mo", GT3_UNIT_MON    },
        { "dy", GT3_UNIT_DAY    },
        { "hr", GT3_UNIT_HOUR   },
        { "mn", GT3_UNIT_MIN    },

        { "year", GT3_UNIT_YEAR },
        { "mon",  GT3_UNIT_MON  },
        { "day",  GT3_UNIT_DAY  },
        { "hour", GT3_UNIT_HOUR }
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

    tdur[0] = num;
    tdur[1] = tab[i].value;
    return 0;
}


static int
parse_tdef(const char *str, GT3_Date *gdate, GT3_Duration *intv)
{
    char buf[3][32];
    int num;
    int date[] = { 0, 1, 1 };
    int time[] = { 0, 0, 0 };
    int timedur[2];
    int ipos;

    num = split((char *)buf, sizeof buf[0], 3, str, NULL, NULL);
    if (num < 2)
        return -1;

    /* get date: yy-mm-dd */
    if (get_ints(date, 3, buf[0], '-') < 0)
        return -1;

    ipos = 1;
    if (num == 3) {
        /*  get time: hh:mm:ss */
        if (get_ints(time, 3, buf[1], ':') < 0)
            return -1;
        ipos++;
    }

    /*
     * time-duration (e.g., 1yr, 1mo, 6hr, ...etc)
     */
    if (get_tdur(timedur, buf[ipos]) < 0)
        return -1;

    GT3_setDate(gdate, date[0], date[1], date[2],
                time[0], time[1], time[2]);

    intv->value = timedur[0];
    intv->unit = timedur[1];
    return 0;
}


int
main(int argc, char **argv)
{
    int ch;
    GT3_Date start;
    GT3_Duration intv;          /* time-interval of each chunk. */
    struct sequence *tseq = NULL;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "c:nhst:")) != -1)
        switch (ch) {
        case 'c':
            if (strcmp(optarg, "auto") == 0)
                calendar = GT3_CAL_DUMMY;
            else
                if ((calendar = GT3_calendar_type(optarg)) == GT3_CAL_DUMMY) {
                    logging(LOG_ERR, "%s: Unknown calendar name.", optarg);
                    exit(1);
                }
            break;

        case 'n':
            dryrun_mode = 1;
            break;

        case 's':
            snapshot_flag = 1;
            break;

        case 't':
            if ((tseq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
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

    /*
     * The origin of the time-axis is set to 0-1-1 (1st Jan, B.C. 1).
     * It's default value.
     */
    GT3_setDate(&basetime, 0, 1, 1, 0, 0, 0);

    /*
     * 1st argument (time-def specifier) is mandatory.
     */
    if (argc <= 0) {
        usage();
        exit(1);
    }
    if (parse_tdef(*argv, &start, &intv) < 0) {
        logging(LOG_ERR, "%s: Invalid argument.", *argv);
        exit(1);
    }

    /*
     * process each file.
     */
    argc--;
    argv++;
    for (; argc > 0 && *argv; argc--, argv++) {
        if (tick_file(*argv, &start, &intv, tseq) < 0) {
            logging(LOG_ERR, "%s: abnormal end.", *argv);
            exit(1);
        }
        if (tseq)
            reinitSeq(tseq, 1, 0x7fffffff);
    }
    return 0;
}
