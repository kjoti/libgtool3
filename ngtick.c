/*
 * ngtick.c -- set time-dimension.
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
#include "caltime.h"
#include "myutils.h"
#include "fileiter.h"
#include "logging.h"

#define PROGNAME "ngtick"

static caltime global_origin;
static int snapshot_flag = 0;
static int dryrun_mode = 0;
static struct message_buffer message_buffer;

struct message_buffer {
    int num;
    char buf[8][64];
};

enum {
    UNIT_MIN,
    UNIT_HOUR,
    UNIT_DAY,
    UNIT_MON,
    UNIT_YEAR
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


static caltime *
step(caltime *date, int n, int unit)
{
    switch (unit) {
    case UNIT_MIN:
        ct_add_seconds(date, 60 * n);
        break;
    case UNIT_HOUR:
        ct_add_hours(date, n);
        break;
    case UNIT_DAY:
        ct_add_days(date, n);
        break;
    case UNIT_MON:
        ct_add_months(date, n);
        break;
    case UNIT_YEAR:
        ct_add_months(date, 12 * n);
        break;
    }
    return date;
}


static char *
date_str(char *buf, const caltime *date)
{
    int hh, mm, ss;

    hh = date->sec / 3600;
    mm = (date->sec - 3600 * hh) / 60;
    ss = (date->sec - 3600 * hh - 60 *mm);

    /* yr = (date->year > 9999) ? 9999 : date->year; */
    snprintf(buf, 17, "%04d%02d%02d %02d%02d%02d",
             date->year, date->month + 1, date->day + 1,
             hh, mm, ss);
    return buf;
}


static char *
int_str(char *buf, int value)
{
    snprintf(buf, 17, "%d", value);
    return buf;
}


static int
modify_field(GT3_HEADER *head, const char *key, const char *value)
{
    char buf[17];
    int rval = 0;

    GT3_copyHeaderItem(buf, sizeof buf, head, key);
    if (strcmp(buf, value) != 0) {
        if (dryrun_mode)
            put_message(key, buf, value);

        GT3_setHeaderString(head, key, value);
        rval = 1;
    }
    return rval;
}


static int
modify_items(GT3_HEADER *head,
             const caltime *lower, const caltime *upper,
             const caltime *date,
             double time, double tdur)
{
    char str[17];
    int rval = 0;

    rval += modify_field(head, "UTIM", "HOUR");
    rval += modify_field(head, "TIME", int_str(str, time / 3600.));
    rval += modify_field(head, "TDUR", int_str(str, tdur / 3600.));

    rval += modify_field(head, "DATE",  date_str(str, date));
    rval += modify_field(head, "DATE1", date_str(str, lower));
    rval += modify_field(head, "DATE2", date_str(str, upper));
    return rval;
}


static int
tick(file_iterator *it, struct caltime *start, int dur, int durunit)
{
    struct caltime date_bnd[2], date;
    struct caltime *lower, *upper;
    GT3_HEADER head;
    double time_bnd[2], time, tdur, secs;
    int days;
    int i, stat;
    int modified, print_filename = 0;

    date_bnd[0] = *start;
    date_bnd[1] = *start;
    step(&date_bnd[1], dur, durunit);

    time_bnd[0] = ct_diff_seconds(&date_bnd[0], &global_origin);
    time_bnd[1] = ct_diff_seconds(&date_bnd[1], &global_origin);

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
         * calculate the midpoint of the time_bnd[].
         */
        if (snapshot_flag) {
            time = time_bnd[i ^ 1]; /* use upper */

            /* modify the header */
            modified = modify_items(&head, upper, upper, upper, time, 0.);
        } else {
            time = 0.5 * (time_bnd[0] + time_bnd[1]);
            tdur = time_bnd[i ^ 1] - time_bnd[i];

            date = *lower;
            secs = 0.5 * tdur;
            days = (int)(secs / (24. * 3600));
            secs -= 24 * 3600 * days;
            ct_add_days(&date, days);
            ct_add_seconds(&date, (int)secs);

            /* modify the header */
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
        step(lower, 2 * dur, durunit);
        time_bnd[i] = ct_diff_seconds(lower, &global_origin);
    }
    return 0;
}


/*
 * Set "TIME", "DATE", "TDUR", "DATE1", and "DATE2".
 */
static int
tick_file(const char *path, caltime *start, int dur, int durunit,
          struct sequence *seq)
{
    GT3_File *fp;
    file_iterator it;
    int rval;

    if ((fp = dryrun_mode ? GT3_open(path) : GT3_openRW(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    setup_file_iterator(&it, fp, seq);
    rval = tick(&it, start, dur, durunit);

    GT3_close(fp);
    return rval;
}


static int
get_tdur(int tdur[], const char *str)
{
    struct { const char *key; int value; } tab[] = {
        { "yr", UNIT_YEAR   },
        { "mo", UNIT_MON    },
        { "dy", UNIT_DAY    },
        { "hr", UNIT_HOUR   },
        { "mn", UNIT_MIN    },

        { "year", UNIT_YEAR },
        { "mon",  UNIT_MON  },
        { "day",  UNIT_DAY  },
        { "hour", UNIT_HOUR }
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
parse_tdef(const char *head, int *yr, int *mon, int *day, int *sec,
           int *tdur, int *unit)
{
    char buf[3][32];
    int num;
    int date[] = { 0, 1, 1 };
    int time[] = { 0, 0, 0 };
    int timedur[2];
    int ipos;

    num = split((char *)buf, sizeof buf[0], 3, head, NULL, NULL);
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

    *yr  = date[0];
    *mon = date[1];
    *day = date[2];
    *sec = time[2] + 60 * (time[1] + 60 * time[0]);
    *tdur = timedur[0];
    *unit = timedur[1];

    return 0;
}


int
main(int argc, char **argv)
{
    int ch;
    caltime start;
    struct sequence *tseq = NULL;
    int caltype = CALTIME_GREGORIAN;
    int yr, mon, day, sec, tdur, unit;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "c:nhst:")) != -1)
        switch (ch) {
        case 'c':
            if ((caltype = ct_calendar_type(optarg)) == CALTIME_DUMMY) {
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
    ct_init_caltime(&global_origin, caltype, 0, 1, 1);

    /*
     * 1st argument: time-def specifier.
     */
    if (argc <= 0) {
        usage();
        exit(1);
    }

    if (parse_tdef(*argv, &yr, &mon, &day, &sec, &tdur, &unit) < 0) {
        logging(LOG_ERR, "%s: Invalid argument", *argv);
        exit(1);
    }

    if (ct_init_caltime(&start, caltype, yr, mon, day) < 0) {
        logging(LOG_ERR, "%s: Invalid DATE", *argv);
        exit(1);
    }
    ct_add_seconds(&start, sec);

    /*
     * process each file.
     */
    argc--;
    argv++;
    for (; argc > 0 && *argv; argc--, argv++) {
        if (tick_file(*argv, &start, tdur, unit, tseq) < 0) {
            logging(LOG_ERR, "%s: abnormal end", *argv);
            exit(1);
        }
        if (tseq)
            reinitSeq(tseq, 1, 0x7fffffff);
    }
    return 0;
}
