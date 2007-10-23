/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  timedim.c -- support GTOOL3-formatted file.
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
 *  convert data-type from 'GT3_Date' to 'caltime'.
 */
static void
conv2caltime(struct caltime *p, const GT3_Date *date, int ctype)
{
	ct_init_caltime(p, ctype, date->year, date->mon, date->day);
	ct_add_seconds(p, date->sec + 60 * (date->min + 60 * date->hour));
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


/*
 *  Return value:
 *     0:  no difference
 *     1:  month/year
 *     2:  day
 *     4:  hour/min/sec
 *     otherwise:  mix
 */
int
GT3_diffDate(GT3_Date *diff, const GT3_Date *from, const GT3_Date *to,
			 int ctype)
{
	unsigned flag = 0U;
	int d_ym, d_sec;

	d_ym = 12 * (to->year - from->year) + to->mon - from->mon;
	d_sec = to->sec - from->sec
		  + 60 * (to->min - from->min)
          + 3600 * (to->hour - from->hour);

	if (d_ym != 0)
		flag |= 1;
	if (to->day != from->day)
		flag |= 2;
	if (d_sec != 0)
		flag |= 4;

	GT3_setDate(diff, 0, 0, 0, 0, 0, 0);

	if (flag == 1) {
		diff->year = d_ym / 12;
		diff->mon  = d_ym % 12;
	} else if ((flag & 1) == 0 && (flag & 6U)) {
		diff->day = to->day - from->day;
		if (d_sec < 0 && diff->day > 0) {
			diff->day--;
			d_sec += 24 * 3600;
		}
		diff->hour = d_sec / 3600;
		d_sec -= 3600 * diff->hour;
		diff->min = d_sec / 60;
		diff->sec = d_sec - 60 * diff->min;
	} else if (flag != 0) {
		struct caltime ct_from, ct_to;
		double dsec;
		int sec;

		conv2caltime(&ct_from, from, ctype);
		conv2caltime(&ct_to,   to,   ctype);

		dsec = ct_diff_seconds(&ct_to, &ct_from);
		diff->day = (int)(dsec / (24. * 3600));
		if (diff->day < 28)
			flag &= ~1U;

		dsec -= 24. * 3600 * diff->day;
		sec  = (int)dsec;

		diff->hour = dsec / 3600;
		dsec -= 3600 * diff->hour;
		diff->min  = dsec / 60;
		diff->sec  = dsec - 60 * diff->min;
	}
	return (int)flag;
}


int
guess_calendar(double sec, const GT3_Date *date, const GT3_Date *origin)
{
	caltime orig, curr;
	int i, ct;
	double time;
	int ctab[] = {
		CALTIME_360_DAY,
		CALTIME_GREGORIAN,
		CALTIME_NOLEAP,
		CALTIME_ALLLEAP,
		CALTIME_JULIAN,
	};

	ct = CALTIME_DUMMY;
	for (i = 0; i < sizeof ctab / sizeof ctab[0]; i++) {
		conv2caltime(&orig, origin, ctab[i]);
		conv2caltime(&curr, date, ctab[i]);

		time = ct_diff_seconds(&curr, &orig);
		if (sec - time == 0.) {
			ct = ctab[i];
			break;
		}
	}
	if (ct == CALTIME_DUMMY) {
		for (i = 0; i < sizeof ctab / sizeof ctab[0]; i++) {
			conv2caltime(&orig, origin, ctab[i]);
			conv2caltime(&curr, date, ctab[i]);

			time = ct_diff_seconds(&curr, &orig);
			if (fabs(sec - time) <= 3600.) {
				ct = ctab[i];
				break;
			}
		}
	}
	return ct;
}


/*
 *  GT3_guessCalendarFile() returns a calendar type (GT3_CAL_XXX),
 */
int
GT3_guessCalendarFile(const char *path)
{
	GT3_File *fp;
	GT3_HEADER head;
	GT3_Date date, origin;
	int ctype;
	int i, time;
	double sec;
	char hbuf[17];
	struct { const char *unit; double fact; } tab[] = {
		{ "SEC",   1. },
		{ "MIN",   60. },
		{ "HOUR",  3600. },
		{ "DAY",   24. * 3600. }
	};

	if ((fp = GT3_open(path)) == NULL
		|| GT3_readHeader(&head, fp) < 0
		|| GT3_decodeHeaderDate(&date, &head, "DATE") < 0)
		return -1;

	if (date.year < 1 && GT3_seek(fp, -1, SEEK_END) == 0) {
		/* we need more info.  */
	 	GT3_readHeader(&head, fp);
	 	GT3_decodeHeaderDate(&date, &head, "DATE");
	}

	GT3_copyHeaderItem(hbuf, sizeof hbuf, &head, "UTIM");
	GT3_decodeHeaderInt(&time, &head, "TIME");

	sec = 3600.;
	for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
 		if (strcmp(hbuf, tab[i].unit) == 0) {
			sec = tab[i].fact;
			break;
		}

	sec *= time;

	GT3_setDate(&origin, 0, 1, 1, 0, 0, 0);
	ctype = guess_calendar(sec, &date, &origin);

	GT3_close(fp);
	return ctype;
}


#ifdef TEST_MAIN
int
main(int argc, char **argv)
{
	GT3_Date a, b, c;
	int rval;

	assert(CALTIME_GREGORIAN == GT3_CAL_GREGORIAN);
	assert(CALTIME_NOLEAP == GT3_CAL_NOLEAP);
	assert(CALTIME_ALLLEAP == GT3_CAL_ALL_LEAP);
	assert(CALTIME_360_DAY == GT3_CAL_360_DAY);
	assert(CALTIME_JULIAN == GT3_CAL_JULIAN);
	assert(CALTIME_DUMMY == GT3_CAL_DUMMY);

	/* GT3_cmpDate */
	GT3_setDate(&a, 1970, 7, 15, 12, 0, 0);
	assert(GT3_cmpDate(&a, 1970, 7, 1, 12, 0, 0) > 0);
	assert(GT3_cmpDate(&a, 1970, 8, 1, 12, 0, 0) < 0);
	assert(GT3_cmpDate(&a, 1970, 7, 15, 0, 0, 0) > 0);
	assert(GT3_cmpDate(&a, 1970, 7, 15, 15, 0, 0) < 0);
	assert(GT3_cmpDate(&a, 1970, 7, 15, 12, 0, 0) == 0);

	GT3_setDate(&a, 1900, 10, 14, 3, 20, 40);
	GT3_setDate(&b, 1900, 10, 14, 4, 25, 50);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 4);
	assert(GT3_cmpDate(&c, 0, 0, 0, 1, 5, 10) == 0);

	GT3_setDate(&b, 1900, 10, 14, 0, 0, 0);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 4);
	assert(GT3_cmpDate(&c, 0, 0, 0, -3, -20, -40) == 0);

	GT3_setDate(&b, 1900, 10, 16, 0, 0, 0);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 6);
	assert(GT3_cmpDate(&c, 0, 0, 1, 20, 39, 20) == 0);

	GT3_setDate(&b, 1900, 10, 1, 3, 20, 40);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 2);
	assert(GT3_cmpDate(&c, 0, 0, -13, 0, 0, 0) == 0);

	GT3_setDate(&b, 1901, 12, 14, 3, 20, 40);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 1);
	assert(GT3_cmpDate(&c, 1, 2, 0, 0, 0, 0) == 0);

	GT3_setDate(&a, 1900, 12, 31, 23, 59, 59);
	GT3_setDate(&b, 1901, 1, 1, 0, 0, 0);
	assert(GT3_cmpDate2(&a, &b) < 0);
	assert(GT3_cmpDate2(&b, &a) > 0);
	GT3_diffDate(&c, &a, &b, 0);
	assert(GT3_cmpDate(&c, 0, 0, 0, 0, 0, 1) == 0);

	GT3_diffDate(&c, &b, &a, 0);
	assert(GT3_cmpDate(&c, 0, 0, 0, 0, 0, -1) == 0);

	GT3_setDate(&b, 1901, 1, 2, 12, 30, 0);
	GT3_diffDate(&c, &a, &b, 0);
	assert(GT3_cmpDate(&c, 0, 0, 1, 12, 30, 1) == 0);

	GT3_setDate(&a, 1900, 1, 1, 0, 0, 0);
	GT3_setDate(&b, 1900, 1, 1, 1, 30, 0);
	GT3_diffDate(&c, &a, &b, 0);
	assert(GT3_cmpDate(&c, 0, 0, 0, 1, 30, 0) == 0);

	GT3_setDate(&b, 1901, 7, 1, 0, 0, 0);
	rval = GT3_diffDate(&c, &a, &b, 0);
	assert(rval == 1);
	assert(GT3_cmpDate(&c, 1, 6, 0, 0, 0, 0) == 0);

	/*
	 *  calendar check.
	 */
	GT3_setDate(&a, 1900, 2, 28, 0, 0, 0);
	GT3_setDate(&b, 1900, 3, 1, 0, 0, 0);
	GT3_diffDate(&c, &a, &b, GT3_CAL_GREGORIAN);
	assert(GT3_cmpDate(&c, 0, 0, 1, 0, 0, 0) == 0);
	GT3_diffDate(&c, &a, &b, GT3_CAL_ALL_LEAP);
	assert(GT3_cmpDate(&c, 0, 0, 2, 0, 0, 0) == 0);
	GT3_diffDate(&c, &a, &b, GT3_CAL_JULIAN);
	assert(GT3_cmpDate(&c, 0, 0, 2, 0, 0, 0) == 0);
	GT3_diffDate(&c, &a, &b, GT3_CAL_360_DAY);
	assert(GT3_cmpDate(&c, 0, 0, 3, 0, 0, 0) == 0);

	GT3_setDate(&a, 1904, 2, 28, 0, 0, 0);
	GT3_setDate(&b, 1904, 3, 1, 0, 0, 0);
	rval = GT3_diffDate(&c, &a, &b, GT3_CAL_GREGORIAN);
	assert(rval == 2);
	assert(GT3_cmpDate(&c, 0, 0, 2, 0, 0, 0) == 0);

	{
		GT3_Date date, orig;
		int ctype;

		GT3_setDate(&orig, 0, 1, 1, 0, 0, 0);
		GT3_setDate(&date, 2000, 1, 16, 12, 0, 0);
		ctype = guess_calendar(3600. * 17532012., &date, &orig);
		assert(ctype == CALTIME_GREGORIAN);
	}
	return 0;
}
#endif /* TEST_MAIN */
