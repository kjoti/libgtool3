/*
 *  caltime.c
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "caltime.h"

typedef int  (*FUNC1)(int, int, const int **);
typedef int  (*FUNC2)(int, int);


/* calendar trait functions */
static int mon_offset(int yr, int mo, const int **tbl);
static int mon_offset_365(int yr, int mo, const int **tbl);
static int mon_offset_366(int yr, int mo, const int **tbl);
static int mon_offset_360(int yr, int mo, const int **tbl);
static int mon_offset_jul(int yr, int mo, const int **tbl);

static int ndays_in_years(int from, int to);
static int ndays_in_years_365(int from, int to);
static int ndays_in_years_366(int from, int to);
static int ndays_in_years_360(int from, int to);
static int ndays_in_years_jul(int from, int to);

struct cal_trait {
    FUNC1 mon_offset;
    FUNC2 ndays_in_years;
    double avedays;             /* (average) # of days in a year */
};

static struct cal_trait all_traits[] = {
    { mon_offset,     ndays_in_years    , 365.2425, },  /* for Gregorian */
    { mon_offset_365, ndays_in_years_365, 365.0     },  /* for noleap */
    { mon_offset_366, ndays_in_years_366, 366.0     },  /* for all leap */
    { mon_offset_360, ndays_in_years_360, 360.0     },  /* for 360_day */
    { mon_offset_jul, ndays_in_years_jul, 365.25    }   /* for Julian */
};

static const char *nametab[] = {
    "gregorian",
    "noleap",
    "all_leap",
    "360_day",
    "julian"
};


static int
mon_offset(int yr, int mo, const int **mtbl)
{
    static const int tbl[][13] = {
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
        {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
    };
    int isleap;

    isleap = (yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0;

    if (mtbl)
        *mtbl = tbl[isleap];
    return tbl[isleap][mo];
}


/*
 *  The number of days between two years ('from' and 'to') in Gregorian.
 *
 *  e.g.)
 *     ndays_in_years(2000, 2001) returns 366.
 *     ndays_in_years(2000, 2002) returns 731.
 *     ndyas_in_years(2000, 2400) returns 146097.
 */
static int
ndays_in_years(int from, int to)
{
    long ndays, nleap;

    if (from > to)
        return -ndays_in_years(to, from);

    ndays = 365 * (to - from);
    nleap = (to + 3) / 4 - (from + 3) / 4;
    if (nleap > 0) {
        from = (from + 99) / 100;
        to   = (to   + 99) / 100;
        if (from < to) {
            nleap -= to - from;
            nleap += (to + 3) / 4 - (from + 3) / 4;
        }
    }
    return ndays + nleap;
}

/*
 *  for 365_day (no leap)
 */
static int
mon_offset_365(int yr, int mo, const int **mtbl)
{
    static const int tbl[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
    };

    if (mtbl)
        *mtbl = tbl;
    return tbl[mo];
}

static int
ndays_in_years_365(int from, int to)
{
    return 365 * (to - from);
}

/*
 *  for 366_day (all leap)
 */
static int
mon_offset_366(int yr, int mo, const int **mtbl)
{
    static const int tbl[] = {
        0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366
    };

    if (mtbl)
        *mtbl = tbl;
    return tbl[mo];
}

static int
ndays_in_years_366(int from, int to)
{
    return 366 * (to - from);
}

/*
 *  for 360_day (ideal calendar)
 */
static int
mon_offset_360(int yr, int mo, const int **mtbl)
{
    static const int tbl[] = {
        0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330, 360
    };

    if (mtbl)
        *mtbl = tbl;
    return 30 * mo;
}

static int
ndays_in_years_360(int from, int to)
{
    return 360 * (to - from);
}

/*
 *  for Julian
 */
static int
mon_offset_jul(int yr, int mo, const int **mtbl)
{
    static const int tbl[][13] = {
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
        {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
    };

    if (mtbl)
        *mtbl = tbl[yr % 4 == 0];

    return tbl[yr % 4 == 0][mo];
}


static int
ndays_in_years_jul(int from, int to)
{
    int ndays, nleap;

    if (from > to)
        return -ndays_in_years_jul(to, from);

    ndays = 365 * (to - from);
    nleap = (to + 3) / 4 - (from + 3) / 4;
    return ndays + nleap;
}


/*
 *  day_of_year: 1st Jan == 0
 */
int
ct_day_of_year(const caltime *date)
{
    return all_traits[date->caltype].mon_offset(date->year, date->month, NULL)
        +  date->day;
}


/*
 *  add 'num' days to current date ('date').
 */
caltime *
ct_add_days(caltime *date, int num)
{
    struct cal_trait *p;
    int total, m;
    const int *mtbl;

    p = all_traits + date->caltype;

    total = p->mon_offset(date->year, date->month, &mtbl)
          + date->day + num;

    if (total < 0 || total >= mtbl[12]) {
        /*
         *  changing current year.
         */
        int nyr;

        do {
            nyr = (int)(total / p->avedays);
            if (total < 0)
                nyr--;
            if (nyr == 0)
                nyr = 1;

            total -= p->ndays_in_years(date->year, date->year + nyr);
            date->year += nyr;
        } while (total < 0 || total >= p->mon_offset(date->year, 12, &mtbl));
    }

    for (m = 1; total >= mtbl[m]; m++)
        ;

    date->month = m - 1;
    date->day   = total - mtbl[date->month];
    return date;
}


caltime *
ct_add_months(caltime *date, int num)
{
    date->month += num;

    date->year  += date->month / 12;
    date->month %= 12;
    if (date->month < 0) {
        date->year--;
        date->month += 12;
    }
    return date;
}


caltime *
ct_add_seconds(caltime *date, int sec)
{
    const int DAYSEC = 24 * 3600;

    sec += date->sec;
    if (sec < 0) {
        int d = sec / DAYSEC;

        if (sec % DAYSEC)
            d--;

        sec -= DAYSEC * d;
        ct_add_days(date, d);
    }

    if (sec >= DAYSEC)
        ct_add_days(date, sec / DAYSEC);

    date->sec = sec % DAYSEC;

    return date;
}


caltime *
ct_add_hours(caltime *date, int hour)
{
    int days;

    days = hour / 24;
    if (days != 0)
        ct_add_days(date, days);
    hour -= 24 * days;
    return ct_add_seconds(date, 3600 * hour);
}


caltime *
ct_add_minutes(caltime *date, int min)
{
    int hours;

    hours = min / 60;
    if (hours != 0)
        ct_add_hours(date, hours);
    min -= 60 * hours;
    return ct_add_seconds(date, 60 * min);
}


caltime *
ct_add_years(caltime *date, int num)
{
    date->year += num;
    return date;
}


int
ct_verify_date(int type, int yr, int mo, int dy)
{
    mo--;
    dy--;

    return (
        type < 0
        || type >= CALTIME_DUMMY
        || mo < 0
        || mo >= 12
        || dy < 0
        || dy >= (all_traits[type].mon_offset(yr, mo+1, NULL)
                  - all_traits[type].mon_offset(yr, mo, NULL))
        ) ? -1 : 0;
}


/*
 *  set 'struct caltime'.
 *
 *  ex).
 *    ct_init_caltime(&date, CALTIME_GREGORIAN, 1900, 1, 1)
 *    => 1st Jan, 1900 00:00:00
 *
 *  ct_init_caltime() returns 0 on successful end, otherwise -1.
 */
int
ct_init_caltime(caltime *date, int type, int yr, int mo, int dy)
{
    if (date == NULL)
        return -1;

    date->caltype = type;
    date->year    = yr;
    date->month   = mo - 1;
    date->day     = dy - 1;
    date->sec     = 0;
    return ct_verify_date(type, yr, mo, dy);
}


int
ct_set_date(caltime *date, int yr, int mo, int dy)
{
    if (date == NULL || ct_verify_date(date->caltype, yr, mo, dy) < 0)
        return -1;

    date->year  = yr;
    date->month = mo - 1;
    date->day   = dy - 1;
    return 0;
}


int
ct_set_time(caltime *date, int hour, int min, int sec)
{
    if (date == NULL
        || hour < 0 || hour > 23
        || min  < 0 || min  > 59
        || sec  < 0 || sec  > 59)
        return -1;

    date->sec = sec + 60 * min + 3600 * hour;
    return 0;
}


/*
 *  ct_cmp() compares 'date1' and 'date2'.
 *
 *  Return value:
 *     0  if date1 == date2
 *     1  if date1 >  date2
 *    -1  if date1 <  date2
 */
int
ct_cmp(const caltime *date1, const caltime *date2)
{
    int v1[4], v2[4];
    int i, diff;

    v1[0] = date1->year;
    v1[1] = date1->month;
    v1[2] = date1->day;
    v1[3] = date1->sec;
    v2[0] = date2->year;
    v2[1] = date2->month;
    v2[2] = date2->day;
    v2[3] = date2->sec;
    for (i = 0; i < 4; i++) {
        diff = v1[i] - v2[i];
        if (diff != 0)
            return diff > 0 ? 1 : -1;
    }
    return 0;
}


int
ct_cmpto(const caltime *date, int yr, int mo, int day,
         int hour, int min, int sec)
{
    caltime date2;

    date2.caltype = date->caltype;
    if (   ct_set_date(&date2, yr, mo, day)    < 0
        || ct_set_time(&date2, hour, min, sec) < 0)
        return -2;

    return ct_cmp(date, &date2);
}


int
ct_eqdate(const caltime *date, int yr, int mo, int day)
{
    return date->year  == yr
        && date->month == mo - 1
        && date->day   == day - 1;
}


int
ct_equal(const caltime *date1, const caltime *date2)
{
    return date1->caltype == date2->caltype
        && date1->year    == date2->year
        && date1->month   == date2->month
        && date1->day     == date2->day
        && date1->sec     == date2->sec;
}


/*
 *  ct_diff_days() returns the difference of the date.
 *
 *  NOTE: ct_diff_days() does not take the time into account.
 *  e.g.,
 *     date1="1999-12-31 12:00:00" and date2=2000-1-1 00:00:00",
 *     => return 1 (not 0).
 */
int
ct_diff_days(const caltime *date2, const caltime *date1)
{
    struct cal_trait *p;

    if (date2->caltype != date1->caltype)
        return 0;

    p = all_traits + date2->caltype;

    return p->ndays_in_years(date1->year, date2->year)
        +  p->mon_offset(date2->year, date2->month, NULL)
        -  p->mon_offset(date1->year, date1->month, NULL)
        +  date2->day
        -  date1->day;
}


double
ct_diff_daysd(const caltime *date2, const caltime *date1)
{
    return ct_diff_days(date2, date1)
        +  1. * (date2->sec - date1->sec) / (24.0 * 3600);
}


double
ct_diff_seconds(const caltime *date2, const caltime *date1)
{
    return 24. * 3600 * ct_diff_days(date2, date1)
        + date2->sec - date1->sec;
}


/* returns the number of days in current year */
int
ct_num_days_in_year(const caltime *date)
{
    return all_traits[date->caltype].mon_offset(date->year, 12, NULL);
}


/* returns the number of days in current month */
int
ct_num_days_in_month(const caltime *date)
{
    const int *mtbl;

    all_traits[date->caltype].mon_offset(date->year, date->month, &mtbl);
    return mtbl[date->month + 1] - mtbl[date->month];
}


int
ct_snprint(char *buf, size_t num, const caltime *date)
{
    int hour, sec;

    sec = date->sec;
    hour = sec / 3600;
    sec -= 3600 * hour;
    return snprintf(buf, num,
                    date->year > 9999
                    ? "%d-%02d-%02d %02d:%02d:%02d"
                    : "%04d-%02d-%02d %02d:%02d:%02d",
                    date->year, date->month + 1, date->day + 1,
                    hour, sec / 60, sec % 60);
}

int
ct_supported_caltypes(void)
{
    return CALTIME_DUMMY;
}

const char *
ct_calendar_name(int ctype)
{
    return (ctype >= 0 && ctype < CALTIME_DUMMY) ? nametab[ctype] : NULL;
}


#ifdef TEST_MAIN
#include <string.h>

int
main(int argc, char **argv)
{
    assert(ndays_in_years(1900, 1900) == 0);
    assert(ndays_in_years(1900, 1901) == 365);
    assert(ndays_in_years(2000, 2001) == 366);
    assert(ndays_in_years(2000, 2400) == 365 * 400 + 100 - 4 + 1);

    assert(ndays_in_years_jul(100, 200) == 365 * 100 + 25);

    /*
     *  2038 problem test
     *  time_t 0x7fffffff == Tue Jan 19 03:14:07 2038 (UTC)
     */
    {
        caltime temp, temp2;
        int diff;

        ct_init_caltime(&temp, CALTIME_GREGORIAN, 1970, 1, 1);
        ct_add_seconds(&temp, 0x7fffffff);
        assert(ct_cmpto(&temp, 2038, 1, 19, 3, 14, 7) == 0);

        temp2 = temp;
        ct_add_seconds(&temp, -0x7fffffff);
        assert(ct_cmpto(&temp, 1970, 1, 1, 0, 0, 0) == 0);

        diff = (int)ct_diff_seconds(&temp2, &temp);
        assert(diff == 0x7fffffff);
    }

    /*
     *  add & sub test.
     */
    {
        caltime date;

        ct_init_caltime(&date, CALTIME_GREGORIAN, 1999, 12, 31);
        ct_set_time(&date, 23, 59, 59);
        ct_add_seconds(&date, 1);
        assert(ct_cmpto(&date, 2000, 1, 1, 0, 0, 0) == 0);
        ct_add_days(&date, 31 + 29);
        assert(ct_cmpto(&date, 2000, 3, 1, 0, 0, 0) == 0);
        ct_add_days(&date, -366);
        assert(ct_cmpto(&date, 1999, 3, 1, 0, 0, 0) == 0);
        ct_add_seconds(&date, -1);
        assert(ct_cmpto(&date, 1999, 2, 28, 23, 59, 59) == 0);
        assert(ct_cmpto(&date, 1999, 3, 1, 0, 0, 0) < 0);
    }

    /* ct_add_minutes() */
    {
        caltime date;

        ct_init_caltime(&date, CALTIME_GREGORIAN, 2000, 1, 1);
        ct_add_minutes(&date, 527040);
        assert(ct_cmpto(&date, 2001, 1, 1, 0, 0, 0) == 0);
    }

    {
        caltime date1, date2;

        /* 1999-12-31 12:00:00 */
        ct_init_caltime(&date1, CALTIME_GREGORIAN, 1999, 12, 31);
        ct_set_time(&date1, 12, 0, 0);

        /* 2000-01-01 00:00:00 */
        ct_init_caltime(&date2, CALTIME_GREGORIAN, 2000, 1, 1);
        ct_set_time(&date2, 0, 0, 0);

        assert(ct_diff_days(&date2, &date1) == 1);
        assert(ct_diff_daysd(&date2, &date1) == 0.5);

        assert(ct_diff_days(&date1, &date2) == -1);
        assert(ct_diff_daysd(&date1, &date2) == -0.5);
    }

    {
        caltime date;

        ct_init_caltime(&date, CALTIME_NOLEAP, 2000, 1, 1);
        ct_add_days(&date, 31 + 28);
        assert(ct_eqdate(&date, 2000, 3, 1));

        ct_init_caltime(&date, CALTIME_360_DAY, 2000, 1, 1);
        ct_add_days(&date, 31 + 28);
        assert(ct_eqdate(&date, 2000, 2, 30));
    }

    {
        caltime date;

        ct_init_caltime(&date, CALTIME_GREGORIAN, 2000, 1, 1);

        assert(ct_num_days_in_month(&date) == 31);
        ct_add_months(&date, 1);
        assert(ct_num_days_in_month(&date) == 29);

        ct_add_months(&date, 12);
        assert(ct_num_days_in_month(&date) == 28);

        ct_set_date(&date, 2000, 2, 1);
        assert(ct_day_of_year(&date) == 31);
    }

    {
        caltime date;

        ct_init_caltime(&date, CALTIME_JULIAN, 100, 1, 1);
        ct_add_days(&date, 365 * 100 + 25);
        assert(ct_eqdate(&date, 200, 1, 1));
    }

    assert(ct_verify_date(CALTIME_GREGORIAN, 1900, 2, 29) == -1);
    assert(ct_verify_date(CALTIME_GREGORIAN, 2000, 2, 29) == 0);
    assert(ct_verify_date(CALTIME_GREGORIAN, 2000, 1, 32) == -1);
    assert(ct_verify_date(CALTIME_GREGORIAN, 2000, 1, 31) == 0);
    assert(ct_verify_date(CALTIME_360_DAY,   2000, 1, 31) == -1);

    /*
     * calendar name.
     */
    assert(sizeof nametab / sizeof nametab[0] == CALTIME_DUMMY);
    assert(ct_calendar_name(CALTIME_DUMMY) == NULL);
    assert(strcmp(ct_calendar_name(CALTIME_GREGORIAN), "gregorian") == 0);
    assert(strcmp(ct_calendar_name(CALTIME_360_DAY), "360_day") == 0);
    return 0;
}
#endif
