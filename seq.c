/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  seq.c - sequence generator
 *
 *
 *  specifier(input)     sequence(output)
 *  -------------------------------------------
 *  "2  3  5  7"   =>    2 3 5 7
 *  "1:10"         =>    1 2 3 4 5 6 7 8 9 10
 *  "2:10:2"       =>    2 4 6 8 10
 *  "10:"          =>    10 11 12 ... LAST
 *  ":10"          =>    FIRST ... 9 10
 *  "::2"          =>    FIRST FIRST+2 ...
 *  "4:1:-1"       =>    4 3 2 1
 *  "1:3  7:10"    =>    1 2 3 7 8 9 10
 *  "2,3, 5, 7"    =>    2 3 5 7
 *
 *  NOTE: ',' is also treated as a while-space.
 *
 *  Usage:
 *
 *      seq = initSeq(specifier, first, last);
 *      while (nextSeq(seq) > 0) {
 *          printf("current number is %d.\n", seq->curr);
 *          do_something(seq->curr, ...);
 *      }
 *      freeSeq(seq);
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seq.h"


static char *
get_next_token(char *buf, int bufsiz,
			   const char *head, const char *tail,
			   const char *wspc, int *status)
{
	char *bufend;

	if (buf == NULL || bufsiz < 2) {
		/* no room to store a C-string */
		*status = -2;
		return NULL;
	}
	*status = 0;				/* status ok */

	/* skip preceding white spaces */
	while (head < tail && strchr(wspc, *head))
		++head;

	bufend = buf + bufsiz - 1;
	while (head < tail && !strchr(wspc, *head)) {
		if (buf >= bufend) {
			*status = -1;		/* buffer overrun */
			break;
		}
		*buf++ = *head++;
	}
	*buf = '\0';

	return (char *)head; /* return the next */
}


static int
get_ints(int vals[], int numval, const char *str, char delim)
{
	const char *tail = str + strlen(str);
	int cnt, num;
	char *endptr;

	cnt = 0;
	while (str < tail && cnt < numval) {
		num = (int)strtol(str, &endptr, 0);

		if (*endptr != delim && *endptr != '\0')
			return -1;			/* invalid char  */

		if (*str != delim)
			vals[cnt] = num;
		str = endptr + 1;
		cnt++;
	}
	return 0;
}


struct sequence *
initSeq(const char *spec, int first, int last)
{
	struct sequence *p;

	if ((p = (struct sequence *)malloc(sizeof(struct sequence))) == NULL
		|| (p->spec = strdup(spec)) == NULL)
		return NULL;

	p->spec_tail = p->spec + strlen(p->spec);
	p->first = first;
	p->last  = last;

	p->it   = p->spec;
	p->curr = 0;
	p->head = p->tail = p->step = 0;
	return p;
}


void
freeSeq(struct sequence *seq)
{
	free(seq->spec);
}


/*
 *  nextSeq() makes a step forward in a sequence
 *  as a ++operator of C++ STL iterator.
 *
 *  'seq->curr' is set to the next value after nextSeq() invorked.
 *
 *  RETURN VALUE
 *   -1: error
 *    0: reach the end of sequence
 *    1: continued
 */
int
nextSeq(struct sequence *seq)
{
	int curr;
	char token[40];
	char *next;
	int status, triplet[3];

	if (seq->step != 0) {
		curr = seq->curr + seq->step;

		if ((seq->step > 0 && curr <= seq->tail)
			|| (seq->step < 0 && curr >= seq->tail)) {
			seq->curr = curr;
			return 1;
		}
	}

	/*
	 *  get a sequence specifier.
	 */
	next = get_next_token(token, sizeof token,
						  seq->it, seq->spec_tail,
						  " \t\n\v\f\r,", &status);

	if (status < 0) /* an error in the token */
		return -1;

	if (token[0] == '\0')
		return 0;

	seq->it = next;

	/*
	 *  parse a sequence specifier.
	 */
	triplet[0] = seq->first;  /* set default */
	triplet[1] = seq->last;   /* set default */
	triplet[2] = 1;           /* set default */

	if (get_ints(triplet, 3, token, ':') < 0)
		return -1;

	if (strchr(token, ':')) {
		seq->head = triplet[0];
		seq->tail = triplet[1];
		seq->step = triplet[2];
	} else {
		seq->step = 0;
	}

	seq->curr = triplet[0];

	return ((seq->step > 0 && seq->tail < seq->head)
			|| (seq->step < 0 && seq->tail > seq->head)) ? 0 : 1;
}


#ifdef TEST_MAIN
void
test1(const char *str, int val[], int num)
{
	struct sequence *seq;
	int i, stat;

	seq = initSeq(str, 1, 100);

	for (i = 0; i < num; i++) {
		stat = nextSeq(seq);

		assert(seq->curr == val[i]);
	}
	stat = nextSeq(seq);
	assert(stat == 0);

	stat = nextSeq(seq);
	assert(stat == 0);

	freeSeq(seq);
	free(seq);
}


int
main(int argc, char **argv)
{
	int val[20];

	val[0] = 1;
	val[1] = 10;
	val[2] = 15;
	test1("  1   10   15   ", val, 3);

	val[0] = 1;
	val[1] = 10;
	val[2] = 15;
	test1("  1 ,   10,   15,  ", val, 3);

	val[0] = 10;
	val[1] = 11;
	val[2] = 12;
	test1("  10:12   ", val, 3);

	val[0] = 10;
	val[1] = 12;
	val[2] = 14;
	test1("  10:14:2   ", val, 3);

	val[0] = 10;
	val[1] = 8;
	val[2] = 6;
	test1("  10:5:-2   ", val, 3);

	val[0] = 1;
	val[1] = 2;
	val[2] = 3;
	val[3] = -1;
	val[4] = 0;
	val[5] = 1;
	test1("  :3   -1:1   ", val, 6);

	return 0;
}
#endif
