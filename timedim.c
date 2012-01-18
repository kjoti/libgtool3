/*
 * timedim.c -- for GT3_Date type
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "caltime.h"
#include "debug.h"
#include "gtool3.h"


/*
 * convert data-type from 'GT3_Date' to 'caltime'.
 */
static int
conv_date_to_ct(struct caltime *p, const GT3_Date *date, int ctype)
{
    int rval;

    if (ctype < 0 || ctype >= GT3_CAL_DUMMY) {
        gt3_error(GT3_ERR_CALL, "Invalid calendar type: %d", ctype);
        return -1;
    }

    rval = ct_init_caltime(p, ctype, date->year, date->mon, date->day);
    if (rval < 0) {
        gt3_error(GT3_ERR_CALL,
                  "Invalid date: (%s) %d-%02d-%02d",
                  GT3_calendar_name(ctype),
                  date->year, date->mon, date->day);
    }
    ct_add_seconds(p, date->sec + 60 * (date->min + 60 * date->hour));
    return rval;
}


static void
conv_ct_to_date(GT3_Date *date, const struct caltime* p)
{
    int sec;

    date->year = p->year;
    date->mon  = 1 + p->month;
    date->day  = 1 + p->day;

    sec = p->sec;
    date->hour = sec / 3600;
    sec -= 3600 * date->hour;
    date->min  = sec / 60;
    sec -= 60 * date->min;
    date->sec = sec;
}


void
GT3_setDate(GT3_Date *date, int y, int mo, int d, int h, int m, int s)
{
    date->year = y;
    date->mon  = mo;
    date->day  = d;
    date->hour = h;
    date->min  = m;
    date->sec  = s;
}


int
GT3_cmpDate(const GT3_Date *date,
            int y, int mo, int d, int h, int m, int s)
{
    int arg[6], ref[6];
    int i, diff;

    arg[0] = date->year;
    arg[1] = date->mon;
    arg[2] = date->day;
    arg[3] = date->hour;
    arg[4] = date->min;
    arg[5] = date->sec;

    ref[0] = y;
    ref[1] = mo;
    ref[2] = d;
    ref[3] = h;
    ref[4] = m;
    ref[5] = s;
    for (i = 0; i < 6; i++) {
        diff = arg[i] - ref[i];
        if (diff != 0)
            return diff > 0 ? 1 : -1;
    }
    return 0;
}


int
GT3_cmpDate2(const GT3_Date *date1, const GT3_Date *date2)
{
    return GT3_cmpDate(date1,
                       date2->year, date2->mon, date2->day,
                       date2->hour, date2->min, date2->sec);
}


int
GT3_midDate(GT3_Date *mid, const GT3_Date *date1, const GT3_Date *date2,
            int calendar)
{
    struct caltime from, to;
    int days, secs;

    if (conv_date_to_ct(&from, date1, calendar) < 0
        || conv_date_to_ct(&to, date2, calendar) < 0)
        return -1;

    days = ct_diff_days(&to, &from);
    ct_add_days(&to, -days);
    secs = ct_diff_seconds(&to, &from);

    if (days % 2) {
        days--;
        secs += 24 * 3600;
    }
    ct_add_days(&from, days / 2);
    ct_add_seconds(&from, secs / 2);

    conv_ct_to_date(mid, &from);
    return 0;
}


void
GT3_copyDate(GT3_Date *dest, const GT3_Date *src)
{
    memcpy(dest, src, sizeof(GT3_Date));
}


void
GT3_addDuration2(GT3_Date *date, const GT3_Duration *dur,
                 int ntimes, int calendar)
{
    struct caltime temp;

    if (conv_date_to_ct(&temp, date, calendar) < 0)
        return;

    switch (dur->unit) {
    case GT3_UNIT_YEAR:
        ct_add_years(&temp, ntimes * dur->value);
        break;

    case GT3_UNIT_MON:
        ct_add_months(&temp, ntimes * dur->value);
        break;

    case GT3_UNIT_DAY:
        ct_add_days(&temp, ntimes * dur->value);
        break;

    case GT3_UNIT_HOUR:
        ct_add_hours(&temp, ntimes * dur->value);
        break;

    case GT3_UNIT_MIN:
        ct_add_minutes(&temp, ntimes * dur->value);
        break;

    case GT3_UNIT_SEC:
        ct_add_seconds(&temp, ntimes * dur->value);
        break;

    default:
        gt3_error(GT3_ERR_CALL, "Invalid GT3_Duration unit");
        break;
    }
    conv_ct_to_date(date, &temp);
}


void
GT3_addDuration(GT3_Date *date, const GT3_Duration *dur, int calendar)
{
    GT3_addDuration2(date, dur, 1, calendar);
}


double
GT3_getTime(const GT3_Date *date, const GT3_Date *since,
            int tunit, int calendar)
{
    caltime from, to;
    double sec, fact;

    if (conv_date_to_ct(&from, since, calendar) < 0
        || conv_date_to_ct(&to,   date,  calendar) < 0)
        return 0.;

    sec = ct_diff_seconds(&to, &from);

    switch (tunit) {
    case GT3_UNIT_DAY:
        fact = 1. / (24. * 3600.);
        break;
    case GT3_UNIT_HOUR:
        fact = 1. / 3600.;
        break;
    case GT3_UNIT_MIN:
        fact = 1. / 60.;
        break;
    case GT3_UNIT_SEC:
        fact = 1.;
        break;
    default:
        fact = 1. / 3600.;
        break;
    };

    return fact * sec;
}


int
guess_calendar(double sec, const GT3_Date *date)
{
    caltime orig, curr;
    int i, ct = CALTIME_DUMMY;
    double time;
    int ctab[] = {
        CALTIME_360_DAY,
        CALTIME_GREGORIAN,
        CALTIME_NOLEAP,
        CALTIME_ALLLEAP,
        CALTIME_JULIAN,
    };

    /*
     * At first time, we assume that the original date is
     * 1st Jan, B.C.1 (0-1-1 00:00:00).
     */
    for (i = 0; i < sizeof ctab / sizeof ctab[0]; i++) {
        ct_init_caltime(&orig, ctab[i], 0, 1, 1);
        if (conv_date_to_ct(&curr, date, ctab[i]) < 0) {
            GT3_clearLastError();
            continue;
        }
        time = ct_diff_seconds(&curr, &orig);
        /*
         * XXX: We have a margin of error of 1-hour.
         * Because TIME is integer-typed and it is in HOUR.
         */
        if (fabs(sec - time) < 3600.) {
            ct = ctab[i];
            break;
        }
    }

    /*
     * compute the origin reversely.
     */
    if (ct == CALTIME_DUMMY) {
        int ndays, nsec;

        ndays = (int)(sec / (24. * 3600.));
        nsec  = (int)(sec - 24. * 3600. * ndays);

        for (i = 0; i < sizeof ctab / sizeof ctab[0]; i++) {
            if (conv_date_to_ct(&curr, date, ctab[i]) < 0) {
                GT3_clearLastError();
                continue;
            }
            ct_add_days(&curr, -ndays);
            ct_add_seconds(&curr, -nsec);

            /* 00:00:00, 1st Jan in any year */
            if (curr.month == 0 && curr.day == 0 && curr.sec == 0) {
                ct = ctab[i];
                break;
            }
        }
    }
    return ct;
}


/*
 * GT3_guessCalendarHeader() guess a calendar from a header
 * and return a calendar type (GT3_CAL_XXX).
 */
int
GT3_guessCalendarHeader(const GT3_HEADER *head)
{
    double sec = 0.;
    GT3_Date date;
    int time, tunit;


    if (GT3_decodeHeaderDate(&date, head, "DATE") < 0
        || (tunit = GT3_decodeHeaderTunit(head)) < 0
        || GT3_decodeHeaderInt(&time, head, "TIME") < 0)
        return -1;

    switch (tunit) {
    case GT3_UNIT_SEC:
        sec = 1.;
        break;
    case GT3_UNIT_MIN:
        sec = 60.;
        break;
    case GT3_UNIT_HOUR:
        sec = 3600.;
        break;
    case GT3_UNIT_DAY:
        sec = 24. * 3600.;
        break;
    default:
        break;
    }
    sec *= time;

    return guess_calendar(sec, &date);
}


/*
 * GT3_guessCalendarFile() returns a calendar type (GT3_CAL_XXX),
 */
int
GT3_guessCalendarFile(const char *path)
{
    GT3_File *fp;
    GT3_HEADER head;
    GT3_Date date;
    int calendar;

    if ((fp = GT3_open(path)) == NULL
        || GT3_readHeader(&head, fp) < 0
        || GT3_decodeHeaderDate(&date, &head, "DATE") < 0)
        return -1;

    if (date.year < 1 && GT3_seek(fp, -1, SEEK_END) == 0)
        /* we need more info.  */
        GT3_readHeader(&head, fp);

    calendar = GT3_guessCalendarHeader(&head);
    GT3_close(fp);
    return calendar;
}


/*
 * calculate time-duration between 'date1' and 'date2'.
 */
int
GT3_calcDuration(GT3_Duration *dur,
                 const GT3_Date *date1, const GT3_Date *date2,
                 int calendar)
{
    unsigned flag = 0U;
    int dmon, dsec;

    dmon = (date2->mon - date1->mon)
        + 12 * (date2->year - date1->year);

    dsec = (date2->sec - date1->sec)
        + 60 * (date2->min - date1->min)
        + 3600 * (date2->hour - date1->hour);

    if (dmon != 0)
        flag |= 1;
    if (dsec != 0)
        flag |= 2;
    if (date1->day != date2->day)
        flag |= 4;

    if (flag == 1U) {
        if (dmon % 12 == 0) {
            dur->value = dmon / 12;
            dur->unit = GT3_UNIT_YEAR;
        } else {
            dur->value = dmon;
            dur->unit = GT3_UNIT_MON;
        }
        return 0;
    }

    if (flag == 4U) {
        dur->value = date2->day - date1->day;
        dur->unit  = GT3_UNIT_DAY;
        return 0;
    }

    if (flag & 5U) {
        /*
         * we need a calendar to compute the duration.
         */
        struct caltime ctdate1, ctdate2;

        if (conv_date_to_ct(&ctdate1, date1, calendar) < 0
            || conv_date_to_ct(&ctdate2, date2, calendar) < 0)
            return -1;

        if (dsec == 0) {
            dur->value = ct_diff_days(&ctdate2, &ctdate1);
            dur->unit  = GT3_UNIT_DAY;
            return 0;
        } else
            dsec = (int)ct_diff_seconds(&ctdate2, &ctdate1);
    }

    if (dsec % (24 * 3600) == 0) {
        dur->value = dsec / (24 * 3600);
        dur->unit = GT3_UNIT_DAY;
    } else if (dsec % 3600 == 0) {
        dur->value = dsec / 3600;
        dur->unit = GT3_UNIT_HOUR;
    } else if (dsec % 60 == 0) {
        dur->value = dsec / 60;
        dur->unit = GT3_UNIT_MIN;
    } else {
        dur->value = dsec;
        dur->unit = GT3_UNIT_SEC;
    }
    return 0;
}


/*
 * get time duration between "DATE1" and "DATE2".
 *
 * XXX: use "DATE1" and "DATE2" instead of "TDUR",
 * which is inaccurate in some case.
 */
int
GT3_getDuration(GT3_Duration *dur, GT3_File *fp, int calendar)
{
    GT3_HEADER head;
    GT3_Date date1, date2;
    unsigned flag = 0U;
    int dmon, dsec;

    if (GT3_readHeader(&head, fp) < 0
        || GT3_decodeHeaderDate(&date1, &head, "DATE1") < 0
        || GT3_decodeHeaderDate(&date2, &head, "DATE2") < 0)
        return -1;

    dmon = (date2.mon - date1.mon) + 12 * (date2.year - date1.year);
    dsec = (date2.sec - date1.sec)
        + 60 * (date2.min - date1.min)
        + 3600 * (date2.hour - date1.hour);

    if (dmon != 0)
        flag |= 1;
    if (dsec != 0)
        flag |= 2;
    if (date1.day != date2.day)
        flag |= 4;

    if (calendar == GT3_CAL_DUMMY && (flag & 5U)) {
        /*
         * Guess calendar-type from "TIME" and "DATE".
         */
        calendar = GT3_guessCalendarHeader(&head);
        if (calendar < 0 || calendar == GT3_CAL_DUMMY)
            calendar = GT3_CAL_GREGORIAN;
    }

    return GT3_calcDuration(dur, &date1, &date2, calendar);
}


/*
 * Check GT3_Date
 *
 * Return value:
 *     0: valid date
 *    -1: invalid date
 */
int
GT3_checkDate(const GT3_Date *date, int calendar)
{
    return ct_verify_date(calendar, date->year, date->mon, date->day);
}


const char *
GT3_calendar_name(int calendar)
{
    return ct_calendar_name(calendar);
}


int
GT3_calendar_type(const char *name)
{
    return ct_calendar_type(name);
}


#ifdef TEST_MAIN
void
printdate(const GT3_Date *date)
{
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           date->year,
           date->mon,
           date->day,
           date->hour,
           date->min,
           date->sec);
}


int
main(int argc, char **argv)
{
    GT3_Date a;

    assert(CALTIME_GREGORIAN == GT3_CAL_GREGORIAN);
    assert(CALTIME_NOLEAP == GT3_CAL_NOLEAP);
    assert(CALTIME_ALLLEAP == GT3_CAL_ALL_LEAP);
    assert(CALTIME_360_DAY == GT3_CAL_360_DAY);
    assert(CALTIME_JULIAN == GT3_CAL_JULIAN);
    assert(CALTIME_DUMMY == GT3_CAL_DUMMY);

    /* GT3_cmpDate */
    GT3_setDate(&a, 1970, 7, 15, 12, 20, 45);
    assert(GT3_cmpDate(&a, 1969, 8, 1,   0,  0,  0) > 0);
    assert(GT3_cmpDate(&a, 1970, 7, 15, 12, 20, 40) > 0);
    assert(GT3_cmpDate(&a, 1970, 7, 15, 12, 20, 45) == 0);
    assert(GT3_cmpDate(&a, 1970, 7, 15, 12, 20, 50) < 0);
    assert(GT3_cmpDate(&a, 1970, 7, 15, 12, 21, 10) < 0);
    assert(GT3_cmpDate(&a, 1970, 7, 15, 13, 10, 10) < 0);

    {
        GT3_Date date;
        int ctype;

        GT3_setDate(&date, 2000, 1, 16, 12, 0, 0);
        ctype = guess_calendar(3600. * 17532012., &date);
        assert(ctype == CALTIME_GREGORIAN);
    }

    /*
     * test of GT3_midDate().
     */
    {
        GT3_Date date1, date2, date;

        GT3_setDate(&date1, 1901, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 1900, 1, 1, 0, 0, 0);

        /* mid-point in a year(non-leap year): Jul 2 12:00 */
        GT3_midDate(&date, &date1, &date2, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 1900, 7, 2, 12, 0, 0) == 0);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_JULIAN);
        assert(GT3_cmpDate(&date, 1900, 7, 2, 0, 0, 0) == 0);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_360_DAY);
        assert(GT3_cmpDate(&date, 1900, 7, 1, 0, 0, 0) == 0);

        GT3_setDate(&date1, 1901, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 1900, 1, 1, 0, 0, 0);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 1900, 7, 2, 12, 0, 0) == 0);
    }

    /*
     * test of GT3_midDate() (part 2)
     */
    {
        GT3_Date date1, date2, date;

        GT3_setDate(&date1, 1999, 12, 31, 12, 0, 0);
        GT3_setDate(&date2, 2000,  1,  1, 12, 0, 0);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2000, 1, 1, 0, 0, 0) == 0);

        GT3_setDate(&date1, 1999, 12, 31, 23, 59, 59);
        GT3_setDate(&date2, 2000,  1,  1, 0, 0, 1);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2000, 1, 1, 0, 0, 0) == 0);

        GT3_setDate(&date1, 2000,  1,  1, 0, 0, 1);
        GT3_setDate(&date2, 1999, 12, 31, 23, 59, 59);

        GT3_midDate(&date, &date1, &date2, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2000, 1, 1, 0, 0, 0) == 0);
    }

    /*
     * test of GT3_getTime()
     */
    {
        GT3_Date date, origin;
        double time;

        GT3_setDate(&origin, 0, 1, 1, 0, 0, 0);

        GT3_setDate(&date, 2100, 1, 1, 12, 0, 0);
        time = GT3_getTime(&date, &origin, GT3_UNIT_HOUR, GT3_CAL_GREGORIAN);
        assert(time == 18408252.0);

        GT3_setDate(&date, 2100, 12, 16, 12, 0, 0);
        time = GT3_getTime(&date, &origin, GT3_UNIT_HOUR, GT3_CAL_GREGORIAN);
        assert(time == 18416628.0);
    }

    /*
     * test of GT3_calcDuration()
     */
    {
        GT3_Date date1, date2;
        GT3_Duration dur;

        GT3_setDate(&date1, 1900, 12, 1, 0, 0, 0);
        GT3_setDate(&date2, 1901,  1, 1, 0, 0, 0);
        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 1 && dur.unit == GT3_UNIT_MON);

        GT3_setDate(&date1, 1900, 12, 31, 23, 59, 59);
        GT3_setDate(&date2, 1901,  1, 1, 0, 0, 0);
        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 1 && dur.unit == GT3_UNIT_SEC);

        GT3_setDate(&date1, 1900, 12, 31, 23, 40, 0);
        GT3_setDate(&date2, 1901,  1, 1, 0, 0, 0);
        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 20 && dur.unit == GT3_UNIT_MIN);

        GT3_setDate(&date1, 1901,  1, 1, 0, 0, 0);
        GT3_setDate(&date2, 1900, 12, 31, 23, 40, 0);
        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == -20 && dur.unit == GT3_UNIT_MIN);

        GT3_setDate(&date1, 1900, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 1900, 1, 2, 12, 0, 0);
        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 36 && dur.unit == GT3_UNIT_HOUR);

        GT3_setDate(&date1, 2000, 2, 28, 0, 0, 0);
        GT3_setDate(&date2, 2000, 3,  1, 0, 0, 0);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 2 && dur.unit == GT3_UNIT_DAY);

        GT3_setDate(&date1, 2000, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 2001, 1, 1, 0, 0, 1);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 31622401 && dur.unit == GT3_UNIT_SEC);

        GT3_setDate(&date1, 2000, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 2001, 2, 2, 1, 1, 1);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_GREGORIAN);

        assert(dur.value == 34390861 && dur.unit == GT3_UNIT_SEC);

        GT3_setDate(&date1, 2000, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 2001, 2, 2, 1, 1, 1);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_NOLEAP);

        assert(dur.value == 34304461 && dur.unit == GT3_UNIT_SEC);

        GT3_setDate(&date1, 2000, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 2001, 2, 2, 1, 1, 1);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_360_DAY);

        assert(dur.value == 33786061 && dur.unit == GT3_UNIT_SEC);

        GT3_setDate(&date1, 2000, 1, 1, 0, 0, 0);
        GT3_setDate(&date2, 2001, 2, 2, 1, 0, 0);

        GT3_calcDuration(&dur, &date1, &date2, GT3_CAL_360_DAY);

        assert(dur.value == 9385 && dur.unit == GT3_UNIT_HOUR);
    }

    /*
     * test of GT3_addDuration()
     */
    {
        GT3_Date date;
        GT3_Duration dur;

        GT3_setDate(&date, 2000, 1, 1, 0, 0, 0);
        dur.value = 6;
        dur.unit = GT3_UNIT_YEAR;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);

        assert(GT3_cmpDate(&date, 2006, 1, 1, 0, 0, 0) == 0);

        dur.unit = GT3_UNIT_MON;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2006, 7, 1, 0, 0, 0) == 0);

        dur.unit = GT3_UNIT_DAY;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2006, 7, 7, 0, 0, 0) == 0);

        dur.value = 60;
        dur.unit = GT3_UNIT_HOUR;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2006, 7, 9, 12, 0, 0) == 0);

        dur.unit = GT3_UNIT_MIN;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2006, 7, 9, 13, 0, 0) == 0);

        dur.unit = GT3_UNIT_SEC;
        GT3_addDuration(&date, &dur, GT3_CAL_GREGORIAN);
        assert(GT3_cmpDate(&date, 2006, 7, 9, 13, 1, 0) == 0);
    }

    /*
     * test of GT3_checkDate().
     */
    {
        GT3_Date date;

        GT3_setDate(&date, 1900, 2, 28, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == 0);

        GT3_setDate(&date, 1900, 2, 29, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == 0);

        GT3_setDate(&date, 1900, 2, 30, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == -1);

        GT3_setDate(&date, 1900, 2, 31, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == -1);

        GT3_setDate(&date, 1900, 3, 0, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == -1);

        GT3_setDate(&date, 1900, 0, 0, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == -1);

        GT3_setDate(&date, 1900, 13, 0, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == -1);

        GT3_setDate(&date, 1900, 12, 31, 0, 0, 0);
        assert(GT3_checkDate(&date, GT3_CAL_GREGORIAN) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_JULIAN) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_360_DAY) == -1);
        assert(GT3_checkDate(&date, GT3_CAL_NOLEAP) == 0);
        assert(GT3_checkDate(&date, GT3_CAL_ALL_LEAP) == 0);
    }

    {
        int ctype[] = {
            GT3_CAL_NOLEAP,
            GT3_CAL_GREGORIAN,
            GT3_CAL_ALL_LEAP,
            GT3_CAL_360_DAY,
            GT3_CAL_JULIAN
        };
        int i, rval;
        const char *name;

        for (i = 0; i < sizeof ctype / sizeof ctype[0]; i++) {
            name = GT3_calendar_name(ctype[i]);
            rval = GT3_calendar_type(name);
            assert(rval == ctype[i]);
        }
    }
    return 0;
}
#endif /* TEST_MAIN */
