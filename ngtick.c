/*	-*- tab-width: 4; -*-
 *	vim: ts=4
 *
 *	ngtick.c -- set time-dimension.
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

#define PROGNAME "ngtick"

static caltime *global_origin = NULL;
static struct sequence *global_timeseq = NULL;
static int snapshot_flag = 0;

enum {
	UNIT_HOUR,
	UNIT_DAY,
	UNIT_MON,
	UNIT_YEAR
};


static void
myperror(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (errno != 0) {
		fprintf(stderr, "%s:", PROGNAME);
		if (fmt) {
			vfprintf(stderr, fmt, ap);
			fprintf(stderr, ":");
		}
		fprintf(stderr, " %s\n", strerror(errno));
	}
	va_end(ap);
}


static void
usage()
{
	static const char *messages =
		"\n"
		"set time-axis.\n"
		"\n"
		"Options:\n"
		"    -h        print help message\n"
		"    -s        make snapshot data\n"
		"    -c        specify a calendar\n"
		"    -t LIST   specify a list of data numbers\n";

	fprintf(stderr, "Usage: %s [options] time-def [files...]\n", PROGNAME);
	fprintf(stderr, messages);
}


static caltime *
step(caltime *date, int n, int unit)
{
	switch (unit) {
	case UNIT_HOUR:
		ct_add_seconds(date, 3600 * n);
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
	GT3_setHeaderInt(head, "TIME", (int)(24. * time));
	GT3_setHeaderInt(head, "TDUR", (int)(24. * tdur));

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
	double time_bnd[2], time, tdur;
	int ndays, nsecs;
	int i, it;

	date_bnd[0] = *start;
	date_bnd[1] = *start;
	step(&date_bnd[1], dur, durunit);

	time_bnd[0] = ct_diff_daysd(&date_bnd[0], global_origin);
	time_bnd[1] = ct_diff_daysd(&date_bnd[1], global_origin);

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
			time = 5e-1 * (time_bnd[0] + time_bnd[1]);
			tdur = 5e-1 * (time_bnd[i ^ 1] - time_bnd[i]);

			ndays = (int)tdur;
			nsecs = (int)((tdur - ndays) * 24. * 3600.);
			date = *lower;
			ct_add_days(&date, ndays);
			ct_add_seconds(&date, nsecs);

			/* modify the header */
			modify_date(&head, lower, upper, &date, time, 2. * tdur);
		}

		/*
		 *  rewrite the modified header.
		 */
		if (fseeko(fp->fp, fp->off + 4, SEEK_SET) < 0
			|| fwrite(head.h, 1, GT3_HEADER_SIZE, fp->fp) != GT3_HEADER_SIZE) {
			myperror(NULL);
			return -1;
		}

		/*
		 *  make a step forward.
		 */
		step(lower, 2 * dur, durunit);
		time_bnd[i] = ct_diff_daysd(lower, global_origin);

		it = get_next(fp);
		if (it < 0)
			return -1;
		if (it == ITER_END) {
			*start = *lower;
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
		{ "allleap",    CALTIME_ALLLEAP   },
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
	caltime *start;
	int caltype = CALTIME_GREGORIAN;
	int yr, mon, day, sec, tdur, unit;


	GT3_setProgname(PROGNAME);

	while ((ch = getopt(argc, argv, "c:hst:")) != -1)
		switch (ch) {
		case 'c':
			if ((caltype = get_calendar(optarg)) < 0) {
				fprintf(stderr, "%s: %s: Unknown calendar name.\n",
						PROGNAME, optarg);
				exit(1);
			}
			break;

		case 's':
			snapshot_flag = 1;
			break;
		case 't':
			if ((global_timeseq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
				myperror(NULL);
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

	global_origin = ct_caltime(0, 1, 1, caltype);

	/*
	 *  1st argument: time-def specifier.
	 */
	if (argc <= 0) {
		usage();
		exit(1);
	}

	if (parse_tdef(*argv, &yr, &mon, &day, &sec, &tdur, &unit) < 0) {
		fprintf(stderr, "%s: %s: invalid argument\n", PROGNAME, *argv);
		exit(1);
	}

	if ((start = ct_caltime(yr, mon, day, caltype)) == NULL) {
		fprintf(stderr, "%s: %s: invalid DATE\n", PROGNAME, *argv);
		exit(1);
	}
	ct_add_seconds(start, sec);

	/*
	 *  process each file.
	 */
	argc--;
	argv++;
	for (; argc > 0 && *argv; argc--, argv++) {
		if (tick_file(*argv, start, tdur, unit) < 0) {
			fprintf(stderr, "%s: %s: abnormal end\n", PROGNAME, *argv);
			exit(1);
		}
		if (global_timeseq)
			reinitSeq(global_timeseq, 1, 0x7fffffff);
	}
	return 0;
}
