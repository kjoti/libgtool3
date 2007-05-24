/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  get_ints.c
 */
#include <string.h>
#include <stdlib.h>


/*
 *	get_ints() fetches integers in a string which are separated
 *	by a delimiter(4th argument).
 *
 *	get_ints() returns one more 'the number of delimiters' on success,
 *  and -1 if an error occurs.
 *
 *	Example) In case of maxnum=3, delim=':'.
 *
 *	str	   RetVal vals[]
 *	-------------------------------
 *	""	   1	  (?,?,?)    '?' means an original value in vals[].
 *	"10"   1	  (10,?,?)
 *	":10"  2	  (?,10)
 *	"10:"  2	  (10,?,?)
 *	":"	   2	  (?,?,?)
 *	"1:10" 2	  (1,10,?)
 *	"::2"  3	  (?,?,2)
 */
int
get_ints(int vals[], int maxnum, const char *str, char delim)
{
	int cnt = 0, temp;
	char *endptr;

	while (*str != '\0' && cnt < maxnum) {
		if (*str == delim) {
			cnt++;
			str++;
			continue;
		}
		temp = (int)strtol(str, &endptr, 10);

		if (*endptr != delim && *endptr != '\0')
			return -1;

		vals[cnt] = temp;
		str = endptr;
	}
	return cnt + 1;
}


#ifdef TEST_MAIN
#include <assert.h>

int
main(int argc, char **argv)
{
	int x[3];
	int rval;

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "", ':');
	assert(rval == 1 && x[0] == -999 && x[1] == -999 && x[2] == -999);

	rval = get_ints(x, 3, "10", ':');
	assert(rval == 1 && x[0] == 10 && x[1] == -999 && x[2] == -999);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "10:20", ':');
	assert(rval == 2 && x[0] == 10 && x[1] == 20 && x[2] == -999);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "10:20:2", ':');
	assert(rval == 3 && x[0] == 10 && x[1] == 20 && x[2] == 2);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, ":10", ':');
	assert(rval == 2 && x[0] == -999 && x[1] == 10 && x[2] == -999);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "10:", ':');
	assert(rval == 2 && x[0] == 10 && x[1] == -999 && x[2] == -999);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, ":", ':');
	assert(rval == 2 && x[0] == -999 && x[1] == -999 && x[2] == -999);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "::2", ':');
	assert(rval == 3 && x[0] == -999 && x[1] == -999 && x[2] == 2);

	x[0] = x[1] = x[2] = -999;
	rval = get_ints(x, 3, "10:20.", ':');
	assert(rval == -1);

	return 0;
}
#endif /* TEST_MAIN */
