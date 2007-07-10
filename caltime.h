/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  caltime.h
 */
#ifndef CALTIME__H
#define CALTIME__H

#ifdef __cplusplus
extern "C" {
#endif

struct caltime {
	int caltype;

	int year;
	int month, day;  /* starting with 0 */
	int sec;         /* seconds since midnight [0-86399] */
};

typedef struct caltime caltime;

/*
 *  calendar type
 */
enum {
	CALTIME_GREGORIAN,  /* proleptic_gregorian */
	CALTIME_NOLEAP,
	CALTIME_ALLLEAP,
	CALTIME_360_DAY,
	CALTIME_JULIAN,
	CALTIME_DUMMY
};

/*
 *  constructor
 */
caltime* ct_caltime(int year, int month, int day, int type);
caltime* ct_gregorian(int year, int month, int day);
caltime* ct_noleap(int year, int month, int day);
caltime* ct_allleap(int year, int month, int day);
caltime* ct_360day(int year, int month, int day);

/*
 *  operators (+/-)
 */
caltime* ct_add_days(caltime *date, int days);
caltime* ct_add_months(caltime *date, int month);
caltime* ct_add_seconds(caltime *date, int sec);

/*
 *  operators (==, <)
 */
int ct_equal(const caltime *date, int yr, int mo, int day);
int ct_equal2(const caltime *date, int, int, int, int, int, int);
int ct_less_than(const caltime *date, int yr, int mo, int day);

/*
 *  utils
 */
int ct_verify_date(int type, int yr, int mo, int dy);

int ct_diff_days(const caltime *date2, const caltime *date1);
int ct_diff_hours(const caltime *date2, const caltime *date1);
int ct_diff_seconds(const caltime *date2, const caltime *date1);
double ct_diff_daysd(const caltime *date2, const caltime *date1);

int ct_day_of_year(const caltime *date);
int ct_set_date(caltime *date, int year, int month, int day);
int ct_set_time(caltime *date, int sec);

int ct_num_days_in_year(const caltime *date);
int ct_num_days_in_month(const caltime *date);

char *ct_caltime_str(const caltime *date);

#ifdef __cplusplus
}
#endif
#endif /* !CALTIME__H */
