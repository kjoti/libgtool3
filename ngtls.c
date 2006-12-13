/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtls.c -- list gtool3 file (like 'gtshow -ls').
 *
 *  $Date: 2006/11/07 03:26:06 $
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"

static const char *usage_message =
	"Usage: ngtls [-hn] [files...]\n"
	"\n"
	"Options:\n"
	"    -h: print help message\n"
	"    -n: print axis-size instead of axis-name\n";

static int (*print_item)(int cnt, GT3_File *fp);


void
print_error(int cnt)
{
	printf("%4d **** BROKEN CHUNK *****\n", cnt);
}


int
print_item1(int cnt, GT3_File *fp)
{
	GT3_HEADER head;
	char item[17];
	char time[17];
	char utim[17];
	char tdur[17];
	char date[17];
	char dim1[17];
	char dim2[17];
	char dim3[17];
	char dfmt[17];

	if (GT3_readHeader(&head, fp) < 0)
		return -1;

	(void)GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
	(void)GT3_copyHeaderItem(time, sizeof time, &head, "TIME");
	(void)GT3_copyHeaderItem(utim, sizeof utim, &head, "UTIM");
	(void)GT3_copyHeaderItem(tdur, sizeof tdur, &head, "TDUR");
	(void)GT3_copyHeaderItem(date, sizeof date, &head, "DATE");
	(void)GT3_copyHeaderItem(dim1, sizeof dim1, &head, "AITM1");
	(void)GT3_copyHeaderItem(dim2, sizeof dim2, &head, "AITM2");
	(void)GT3_copyHeaderItem(dim3, sizeof dim3, &head, "AITM3");
	(void)GT3_copyHeaderItem(dfmt, sizeof dfmt, &head, "DFMT");

	if (utim[0] == '\0')
		utim[0] = '?';

	printf("%4d %-8s %8s%c %5s %4s %15s %s,%s,%s\n",
		   cnt, item, time, utim[0], tdur, dfmt, date, dim1, dim2, dim3);
	return 0;
}


int
print_item2(int cnt, GT3_File *fp)
{
	GT3_HEADER head;
	char item[17];
	char time[17];
	char utim[17];
	char tdur[17];
	char date[17];
	char dfmt[17];

	if (GT3_readHeader(&head, fp) < 0)
		return -1;

	(void)GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
	(void)GT3_copyHeaderItem(time, sizeof time, &head, "TIME");
	(void)GT3_copyHeaderItem(utim, sizeof utim, &head, "UTIM");
	(void)GT3_copyHeaderItem(tdur, sizeof tdur, &head, "TDUR");
	(void)GT3_copyHeaderItem(date, sizeof date, &head, "DATE");
	(void)GT3_copyHeaderItem(dfmt, sizeof dfmt, &head, "DFMT");

	if (utim[0] == '\0')
		utim[0] = '?';

	printf("%4d %-8s %8s%c %5s %4s %15s %4d %4d %4d\n",
		   cnt, item, time, utim[0], tdur, dfmt, date,
		   fp->dimlen[0], fp->dimlen[1], fp->dimlen[2]);
	return 0;
}


int
print_list(const char *path)
{
	GT3_File *fp;
	int cnt;
	int rval = 0;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	cnt = 0;
	while (!GT3_eof(fp)) {
		++cnt;
		if ((*print_item)(cnt, fp) < 0) {
			print_error(cnt);
			rval = -1;
			break;
		}

		if (GT3_next(fp) < 0) {
			print_error(cnt + 1);
			rval = -1;
			break;
		}
	}

	GT3_close(fp);
	return rval;
}


int
main(int argc, char **argv)
{
	int ch;

	print_item = print_item1;

	while ((ch = getopt(argc, argv, "nh")) != -1)
		switch (ch) {
		case 'n':
			print_item = print_item2;
			break;

		case 'h':
		default:
			fprintf(stderr, usage_message);
			exit(1);
			break;
		}

	argc -= optind;
	argv += optind;
	GT3_setProgname("ngtls");

	while (argc > 0 && *argv) {
		print_list(*argv);

		--argc;
		++argv;
	}
	return 0;
}
