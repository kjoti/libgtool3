/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtcat.c -- concatenate gtool files.
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"


static const char *usage_messages =
	"Usage: ngtcat [options] [files...]\n"
	"\n"
	"concatenate gtool files (output into stdout).\n"
	"\n"
	"Options:\n"
	"    -h        print help message\n"
	"    -c        cyclic mode\n"
	"    -t LIST   specify a list of data numbers\n"
	"\n"
	"    LIST   := RANGE[,RANGE]*\n"
	"    RANGE  := start[:[end]] | :[end]\n";


static int
fcopy(FILE *dest, FILE *src, size_t size)
{
	char buf[BUFSIZ];
	size_t nread;

	while (size > 0) {
		nread = size > sizeof buf ? sizeof buf : size;
		if (fread(buf, 1, nread, src) != nread
			|| fwrite(buf, 1, nread, dest) != nread)
			break;
		size -= nread;
	}
	return (size == 0) ? 0 : -1;
}


int
gtcat_cyclic(int num, char *path[], struct sequence *seq)
{
	GT3_File *fp;
	int i, last, stat, errflag;

	if (num < 1)
		return 0;

	if ((last = GT3_countChunk(path[0])) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	reinitSeq(seq, 1, last);

	errflag = 0;
	while (nextSeq(seq) != ITER_END && errflag == 0) {
		for (i = 0; i < num && errflag == 0; i++) {
			if ((fp = GT3_open(path[i])) == NULL) {
				GT3_printErrorMessages(stderr);
				return -1;
			}

			stat = seq->curr > 0
				? GT3_seek(fp, seq->curr - 1, SEEK_SET)
				: GT3_seek(fp, seq->curr, SEEK_END);

			if (stat == 0) {
				if (fcopy(stdout, fp->fp, fp->chsize) < 0) {
					perror(path[i]);
					errflag = 1;
				}
			} else if (stat != GT3_ERR_INDEX)
				errflag = 1;

			/* GT3_ERR_INDEX (out of range) is ignored. */

			GT3_close(fp);
		}
	}
	return errflag ? -1 : 0;
}


int
gtcat(const char *path, struct sequence *seq)
{
	GT3_File *fp;
	int stat, rval;

	if ((fp = GT3_open(path)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}

	rval = 0;
	while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
		if (stat == ITER_OUTRANGE)
			continue;

		if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
			rval = -1;
			break;
		}

		if (fcopy(stdout, fp->fp, fp->chsize) < 0) {
			perror(path);
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
	struct sequence *seq = NULL;
	int cyclic = 0;
	int ch, rval = 0;

	while ((ch = getopt(argc, argv, "cht:")) != -1)
		switch (ch) {
		case 'c':
			cyclic = 1;
			break;

		case 't':
			seq = initSeq(optarg, 1, 0x7fffffff);
			break;

		case 'h':
		default:
			fprintf(stderr, usage_messages);
			exit(1);
			break;
		}

	if (!seq)
		seq = initSeq(":", 1, 0x7fffffff);

	argc -= optind;
	argv += optind;
	GT3_setProgname("ngtcat");

	if (cyclic) {
		if (gtcat_cyclic(argc, argv, seq) < 0)
			rval = 1;
	} else
		for (; argc > 0 && *argv; argc--, argv++) {
			if (gtcat(*argv, seq) < 0)
				rval = 1;

			reinitSeq(seq, 1, 0x7fffffff);
		}

	return rval;
}
