/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtick.c -- set time-dimension.
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
static struct sequence *global_timeseq = NULL;
static int snapshot_flag = 0;

enum {
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
		"    -s        specify a snapshot\n"
		"    -c CAL    specify a calendar\n"
		"    -t LIST   specify data No.\n"
		"\n"
		"    CAL : gregorian(default), noleap, all_leap, 360_day, julian\n";

	fprintf(stderr, "%s\n", GT3_version());
	fprintf(stderr, "Usage: %s [options] time-def [files...]\n", PROGNAME);
	fprintf(stderr, "%s\n", messages);
}


static caltime *
step(caltime *date, int n, int unit)
{
	switch (unit) {
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
	int yr, hh, mm, ss;

	hh = date->sec / 3600.;
	mm = (date->sec - 3600 * hh) / 60;
	ss = (date->sec - 3600 * hh - 60 *mm);

	yr = (date->year > 9999) ? 9999 : date->year;
	snprintf(buf, 16, "%04d%02d%02d %02d%02d%02d",
			 yr, date->month + 1, date->day + 1,
			 hh, mm, ss);
	return buf;
}


static void
modify_date(GT3_HEADER *head,
			const caltime *lower, const caltime *upper,
			const caltime *date,
			double time, double tdur)
{
	char str[16];

	GT3_setHeaderString(head, "UTIM", "HOUR");
	GT3_setHeaderInt(head, "TIME", (int)(time / 3600.));
	GT3_setHeaderInt(head, "TDUR", (int)(tdur / 3600.));

	GT3_setHeaderString(head, "DATE",  date_str(str, date));
	GT3_setHeaderString(head, "DATE1", date_str(str, lower));
	GT3_setHeaderString(head, "DATE2", date_str(str, upper));
}


/*
 *  Return value:
 *             -1: some error has occurred.
 *       ITER_END: no more timeseq (only when -t option specified)
 *  ITER_CONTINUE: the others
 */
static int
get_next(GT3_File *fp)
{
	int rval;

	if (!global_timeseq) {
		if (GT3_next(fp) < 0) {
			GT3_printErrorMessages(stderr);
			return -1;
		}
		rval = ITER_CONTINUE;
	} else {
		while ((rval = iterate_chunk(fp, global_timeseq)) == ITER_OUTRANGE)
			if (rval == ITER_ERROR || rval == ITER_ERRORCHUNK)
				return -1;
	}
	return rval;
}


static int
tick(GT3_File *fp, struct caltime *start, int dur, int durunit)
{
	struct caltime date_bnd[2], date;
	struct caltime *lower, *upper;
	GT3_HEADER head;
	double time_bnd[2], time, tdur, secs;
	int days;
	int i, it;

	date_bnd[0] = *start;
	date_bnd[1] = *start;
	step(&date_bnd[1], dur, durunit);

	time_bnd[0] = ct_diff_seconds(&date_bnd[0], &global_origin);
	time_bnd[1] = ct_diff_seconds(&date_bnd[1], &global_origin);

	for (i = 0; ; i ^= 1) {
		lower = &date_bnd[i];
		upper = &date_bnd[i ^ 1];

		if (GT3_readHeader(&head, fp) < 0) {
			if (GT3_eof(fp)) {
				*start = *lower;
				break;
			}
			GT3_printErrorMessages(stderr);
			return -1;
		}

		/*
		 *  calculate the midpoint of the time_bnd[].
		 */
		if (snapshot_flag) {
			time = time_bnd[i ^ 1]; /* use upper */

			/* modify the header */
			modify_date(&head, upper, upper, upper, time, 0.);
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
			modify_date(&head, lower, upper, &date, time, tdur);
		}

		/*
		 *  rewrite the modified header.
		 */
		if (fseeko(fp->fp, fp->off + 4, SEEK_SET) < 0
			|| fwrite(head.h, 1, GT3_HEADER_SIZE, fp->fp) != GT3_HEADER_SIZE) {
			logging(LOG_SYSERR, NULL);
			return -1;
		}

		/*
		 *  make a step forward.
		 */
		step(lower, 2 * dur, durunit);
		time_bnd[i] = ct_diff_seconds(lower, &global_origin);

		it = get_next(fp);
		if (it < 0)
			return -1;
		if (it == ITER_END) {
			*start = *upper;
			break;
		}
	}
	return 0;
}


/*
 *  Set "TIME", "DATE", "TDUR", "DATE1", and "DATE2".
 */
static int
tick_file(const char *path, caltime *start, int dur, int durunit)
{
	GT3_File *fp;
	int rval;

	if ((fp = GT3_openRW(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	if (global_timeseq)
		rval = get_next(fp);

	if (!global_timeseq || rval == ITER_CONTINUE)
		rval = tick(fp, start, dur, durunit);

	GT3_close(fp);
	return rval;
}


static int
get_calendar(const char *name)
{
	struct { const char *key; int value; } tab[] = {
		{ "gregorian",  CALTIME_GREGORIAN },
		{ "360_day",    CALTIME_360_DAY   },
		{ "noleap",     CALTIME_NOLEAP    },
		{ "365_day",    CALTIME_NOLEAP    },
		{ "all_leap",   CALTIME_ALLLEAP   },
		{ "366_day",    CALTIME_ALLLEAP   },
		{ "julian",     CALTIME_JULIAN    }
	};
	int i;

	for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
		if (strcmp(name, tab[i].key) == 0)
			return tab[i].value;

	return -1;
}


static int
get_tdur(int tdur[], const char *str)
{
	struct { const char *key; int value; } tab[] = {
		{ "yr", UNIT_YEAR   },
		{ "mo", UNIT_MON    },
		{ "dy", UNIT_DAY    },
		{ "hr", UNIT_HOUR   },

		{ "year", UNIT_YEAR },
		{ "mon",  UNIT_MON  },
		{ "day",  UNIT_DAY  },
		{ "hour", UNIT_HOUR }
	};
	char *endptr;
	int i, num;

	num = strtol(str, &endptr, 10);
	if (str == endptr && *endptr == '\0')
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
	 *  time-duration (e.g., 1yr, 1mo, 6hr, ...etc)
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
	int caltype = CALTIME_GREGORIAN;
	int yr, mon, day, sec, tdur, unit;

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);
	while ((ch = getopt(argc, argv, "c:hst:")) != -1)
		switch (ch) {
		case 'c':
			if ((caltype = get_calendar(optarg)) < 0) {
				logging(LOG_ERR, "%s: Unknown calendar name.", optarg);
				exit(1);
			}
			break;

		case 's':
			snapshot_flag = 1;
			break;
		case 't':
			if ((global_timeseq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
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
	 *  The origin of the time-axis is set to 0-1-1 (1st Jan, B.C. 1).
	 *  in most GTOOL3-files.
	 */
	ct_init_caltime(&global_origin, caltype, 0, 1, 1);

	/*
	 *  1st argument: time-def specifier.
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
	 *  process each file.
	 */
	argc--;
	argv++;
	for (; argc > 0 && *argv; argc--, argv++) {
		if (tick_file(*argv, &start, tdur, unit) < 0) {
			logging(LOG_ERR, "%s: abnormal end", *argv);
			exit(1);
		}
		if (global_timeseq)
			reinitSeq(global_timeseq, 1, 0x7fffffff);
	}
	return 0;
}
