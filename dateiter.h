#ifndef DATEITER__H
#define DATEITER__H

#include "gtool3.h"
#include "caltime.h"


struct DateIterator {
	int count;					/* the # of passed stops */
	int dmon, dday, dsec;		/* stepsize */
	caltime next;				/* next stop */
};
typedef struct DateIterator DateIterator;

void setDateIterator(DateIterator *it,
					 const GT3_Date *initial,
					 const GT3_Date *step,
					 int ctype);

void nextDateIterator(DateIterator *it);
int cmpDateIterator(const DateIterator *it, const GT3_Date *date);

#endif
