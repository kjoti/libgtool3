/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
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
	double avedays;				/* (average) # of days in a year */
};

static struct cal_trait all_traits[] = {
	{ mon_offset,     ndays_in_years    , 365.2425, },  /* for Gregorian */
	{ mon_offset_365, ndays_in_years_365, 365.0     },  /* for noleap */
	{ mon_offset_366, ndays_in_years_366, 366.0     },  /* for all leap */
	{ mon_offset_360, ndays_in_years_360, 360.0     },  /* for 360_day */
	{ mon_offset_jul, ndays_in_years_jul, 365.25    }   /* for Julian */
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
 *  the number of days since 1st Jan.
 */
int
ct_day_of_year(const caltime *date)
{
	return all_traits[date->caltype].mon_offset(date->year, date->month, NULL)
		+  date->day;
}


/*
 *  adding 'num' days to current date ('date').
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
ct_set_time(caltime *date, int sec)
{
	if (date == NULL || sec < 0 || sec >= 24 * 3600)
		return -1;

	date->sec = sec;
	return 0;
}


int
ct_diff_days(const caltime *date2, const caltime *date1)
{
	struct cal_trait *p;

	if (date2->caltype != date1->caltype)
		return 0;

	p = all_traits + date2->caltype;

	return p->ndays_in_years(date1->year, date2->year)
		+  p->mon_offset(date2->year, date2->month, NULL) + date2->day
		-  p->mon_offset(date1->year, date1->month, NULL) - date1->day;
}


double
ct_diff_daysd(const caltime *date2, const caltime *date1)
{
	struct cal_trait *p;

	if (date2->caltype != date1->caltype)
		return 0;

	p = all_traits + date2->caltype;

	return p->ndays_in_years(date1->year, date2->year)
		+  p->mon_offset(date2->year, date2->month, NULL) + date2->day
		-  p->mon_offset(date1->year, date1->month, NULL) - date1->day
		+  (date2->sec - date1->sec) / (24.0 * 3600);
}


int
ct_diff_seconds(const caltime *date2, const caltime *date1)
{
	struct cal_trait *p;

	if (date2->caltype != date1->caltype)
		return 0;

	p = all_traits + date2->caltype;

	return 24 * 3600
		* (p->ndays_in_years(date1->year, date2->year)
		+  p->mon_offset(date2->year, date2->month, NULL) + date2->day
		-  p->mon_offset(date1->year, date1->month, NULL) - date1->day)
		+  date2->sec - date1->sec;
}


int
ct_diff_hours(const caltime *date2, const caltime *date1)
{
	struct cal_trait *p;

	if (date2->caltype != date1->caltype)
		return 0;

	p = all_traits + date2->caltype;

	return 24
		* (p->ndays_in_years(date1->year, date2->year)
		+  p->mon_offset(date2->year, date2->month, NULL) + date2->day
		-  p->mon_offset(date1->year, date1->month, NULL) - date1->day)
		+  (date2->sec - date1->sec) / 3600;
}


int
ct_equal(const caltime *date, int yr, int mo, int day)
{
	return date->year  == yr
		&& date->month == mo - 1
		&& date->day   == day - 1;
}


int
ct_equal2(const caltime *date, int yr, int mo, int day,
		  int hh, int mm, int ss)
{
	return date->year  == yr
		&& date->month == mo - 1
		&& date->day   == day - 1
		&& date->sec   == 3600 * hh + 60 * mm + ss;
}


int
ct_less_than(const caltime *date, int yr, int mo, int day)
{
	if (date->year != yr)
		return date->year < yr;

	mo--;
	if (date->month != mo)
		return date->month < mo;

	return date->day < day - 1;
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


char *
ct_caltime_str(const caltime *date)
{
	static char buf[20];

	int sec, hh;

	sec = date->sec;
	hh = sec / 3600;
	sec -= 3600 * hh;

	sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
			date->year,
			date->month + 1,
			date->day + 1,
			hh, sec / 60, sec % 60);
	return buf;
}


/*
 *  constructors
 */
caltime *
ct_caltime(int yr, int mo, int d, int type)
{
	caltime *date;

	if (ct_verify_date(type, yr, mo, d) < 0
		|| (date = (caltime *)malloc(sizeof(caltime))) == NULL)
		return NULL;

	date->caltype = type;
	date->year  = yr;
	date->month = mo - 1;
	date->day   = d - 1;
	date->sec   = 0;

	return date;
}

caltime *
ct_gregorian(int yr, int mo, int d)
{
	return ct_caltime(yr, mo, d, CALTIME_GREGORIAN);
}

caltime *
ct_noleap(int yr, int mo, int d)
{
	return ct_caltime(yr, mo, d, CALTIME_NOLEAP);
}

caltime *
ct_allleap(int yr, int mo, int d)
{
	return ct_caltime(yr, mo, d, CALTIME_ALLLEAP);
}

caltime *
ct_360day(int yr, int mo, int d)
{
	return ct_caltime(yr, mo, d, CALTIME_360_DAY);
}



#ifdef TEST_MAIN
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
		caltime *temp, temp2;

		temp = ct_gregorian(1970, 1, 1);
		ct_add_seconds(temp, 0x7fffffff);
		assert(ct_equal2(temp, 2038, 1, 19, 3, 14, 7));
		printf("last time = (%s)\n", ct_caltime_str(temp));

		ct_add_seconds(temp, -0x7fffffff);
		assert(ct_equal2(temp, 1970, 1, 1, 0, 0, 0));

		temp2 = *temp;
		ct_add_seconds(temp, 0x7fffffff);
		assert(ct_diff_seconds(temp, &temp2) == 0x7fffffff);

		free(temp);
	}

	/*
	 *  add & sub test.
	 */
	{
		int i;
		caltime *temp = ct_caltime(1900, 10, 10, CALTIME_GREGORIAN);
		caltime x;
		int testv[] = {-1000, 0, 1000, 10000};

		for (i = 0; i < sizeof testv / sizeof(int); i++) {
			x = *temp;
			ct_add_days(&x, testv[i]);
			ct_add_seconds(&x, testv[i]);

			ct_add_days(&x, -testv[i]);
			ct_add_seconds(&x, -testv[i]);

			assert(x.year == temp->year
				   && x.month == temp->month
				   && x.day == temp->day
				   && x.sec == temp->sec);
		}
	}


	{
		caltime *date;

		date = ct_caltime(2000, 1, 1, CALTIME_NOLEAP);
		ct_add_days(date, 31 + 28);
		assert(ct_equal(date, 2000, 3, 1));
		free(date);

		date = ct_caltime(2000, 1, 1, CALTIME_360_DAY);
		ct_add_days(date, 31 + 28);
		assert(ct_equal(date, 2000, 2, 30));
		free(date);
	}

	{
		caltime *date = ct_gregorian(2000, 12, 10);

		while (ct_less_than(date, 2001, 3, 3))
			ct_add_days(date, 1);

		assert(ct_equal(date, 2001, 3, 3));
		free(date);
	}

	{
		caltime *date = ct_gregorian(2000, 1, 1);

		assert(ct_num_days_in_month(date) == 31);
		ct_add_months(date, 1);
		assert(ct_num_days_in_month(date) == 29);

		ct_add_months(date, 12);
		assert(ct_num_days_in_month(date) == 28);
	}

	{
		caltime *date = ct_caltime(100, 1, 1, CALTIME_JULIAN);

		ct_add_days(date, 365 * 100 + 25 + 61);
		assert(ct_equal(date, 200, 3, 2));
		free(date);
	}
	return 0;
}
#endif
