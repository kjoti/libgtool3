/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  split.c -- splits a string into substrings.
 */
#include <ctype.h>
#include <string.h>

#ifndef min
#define min(a,b) ((a)<(b) ? (a) : (b))
#endif

/*
 *	split() splits a string into substrings,
 *	which are separated by any white-spaces.
 *	These substrings are stored in the specified buffer('buf').
 *	The maximum length of each substring is 'maxlen - 1',
 *	and the number of substring is up to 'maxnum'.
 *
 *	'buf' must have at least "maxlen * maxnum" space.
 *
 *	split() returns the number of substring in successful end.
 *	If overflow error is occurred, -1 is returned.
 */
int
split(char *buf, int maxlen, int maxnum,
	  const char *head, const char *tail, char **endptr)
{
	const char *p = head;
	int cnt = 0;
	const char *tail2;
	char *q;
	int spc;

	if (!tail)
		tail = head + strlen(head);

	while (p < tail && cnt < maxnum) {
		/* skip white spaces */
		if (isspace(*p)) {
			p++;
			while (p < tail && isspace(*p))
				p++;
			if (p == tail)
				break;
		}

		tail2 = min(tail, p + maxlen - 1);
		q = buf + maxlen * cnt;
		while (!(spc = isspace(*p)) && p < tail2)
			*q++ = *p++;

		*q = '\0';
		cnt++;

		if (p < tail && !spc) {
			/* error: exceeds the maximum length */
			cnt = -1;
			break;
		}
	}
	if (endptr)
		*endptr = (char *)p;
	return cnt;
}


#ifdef TEST_MAIN
#include <assert.h>
#include <stdio.h>

#define MAXNUM 6
#define MAXLEN 8

char buf[MAXNUM][MAXLEN];

void
reset(void)
{
	int i;

	for (i = 0; i < MAXNUM; i++)
		strcpy(buf[i], "NONE");
}


int
main(int argc, char **argv)
{
	int num;
	char *endptr;

	reset();
	num = split((char *)buf, MAXLEN, MAXNUM,
				"				   ", NULL, NULL);
	assert(num == 0 && strcmp(buf[0], "NONE") == 0);


	reset();
	num = split((char *)buf, MAXLEN, MAXNUM,
				"	 hello	  world				 ", NULL, &endptr);

	assert(num == 2 && strcmp(buf[0], "hello") == 0);
	assert(strcmp(buf[1], "world") == 0);
	assert(strcmp(buf[2], "NONE") == 0);
	assert(*endptr == '\0');

	reset();
	num = split((char *)buf, MAXLEN, MAXNUM,
				" 0 1 2 3 4 5 6 7 8 9", NULL, &endptr);
	assert(num == MAXNUM && strcmp(buf[0], "0") == 0);
	assert(strcmp(buf[1], "1") == 0 && strcmp(buf[5], "5") == 0);
	assert(strcmp(endptr, " 6 7 8 9") == 0);


	reset();
	num = split((char *)buf, MAXLEN, MAXNUM,
				" foobarbaz ", NULL, &endptr);
	assert(strcmp(buf[0], "foobarb") == 0);
	assert(strcmp(endptr, "az ") == 0);
	assert(num == -1);


	reset();
	num = split((char *)buf, MAXLEN, MAXNUM,
				" foobarb ", NULL, &endptr);
	assert(strcmp(buf[0], "foobarb") == 0);
	assert(strcmp(buf[1], "NONE") == 0);
	assert(*endptr == '\0');
	assert(num == 1);

	return 0;
}
#endif /* TEST_MAIN */
