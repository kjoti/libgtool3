#ifndef MYUTILS__H
#define MYUTILS__H

int split(char *buf, int maxlen, int maxnum,
		  const char *head, const char *tail, char **endptr);
int get_ints(int vals[], int maxnum, const char *str, char delim);

#endif /* !MYUTILS__H */
