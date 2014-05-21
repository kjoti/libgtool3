/*
 * dateiter.c -- date/time iterator.
 *
 * use caltime routine.
 */
#include "internal.h"

#include "gtool3.h"
#include "caltime.h"
#include "dateiter.h"


void
setDateIterator(DateIterator *it,
                const GT3_Date *initial,
                const GT3_Date *step,
                int ctype)
{
    it->count = 0;
    it->dmon = 12 * step->year + step->mon;
    it->dday = step->day;
    it->dsec = step->sec + 60 * (step->min + 60 * step->hour);

    ct_init_caltime(&it->next, ctype,
                    initial->year, initial->mon, initial->day);

    ct_add_seconds(&it->next,
                   initial->sec + 60 * (initial->min + 60 * initial->hour));


    ct_add_months(&it->next, it->dmon);
    ct_add_days(&it->next, it->dday);
    ct_add_seconds(&it->next, it->dsec);
}


/*
 * go to next stop.
 */
void
nextDateIterator(DateIterator *it)
{
    ct_add_months(&it->next, it->dmon);
    ct_add_days(&it->next, it->dday);
    ct_add_seconds(&it->next, it->dsec);
    it->count++;
}


/*
 * return value:
 *     date - it->next
 */
int
cmpDateIterator(const DateIterator *it, const GT3_Date *date)
{
    int v1[4], v2[4];
    int i, diff;

    v1[0] = it->next.year;
    v1[1] = it->next.month;
    v1[2] = it->next.day;
    v1[3] = it->next.sec;
    v2[0] = date->year;
    v2[1] = date->mon - 1;
    v2[2] = date->day - 1;
    v2[3] = date->sec + 60 * (date->min + 60 * date->hour);

    for (i = 0; i < 4; i++) {
        diff = v2[i] - v1[i];
        if (diff != 0)
            return diff > 0 ? 1 : -1;
    }
    return 0;
}


#ifdef TEST_MAIN
#include <assert.h>

void
print(const DateIterator *it)
{
    int h, m, s;

    s = it->next.sec;
    h = s / 3600;
    s -= 3600 * h;
    m = s / 60;
    s -= 60 * m;
    printf("%8d: %4d-%02d-%02d %02d:%02d:%02d\n",
           it->count,
           it->next.year,
           it->next.month + 1,
           it->next.day + 1,
           h, m, s);
}


void
test1(void)
{
    DateIterator it;
    GT3_Date initial, step;
    GT3_Date last;

    GT3_setDate(&initial, 2000, 1, 1, 0, 0, 0);
    GT3_setDate(&last,    2001, 1, 1, 0, 0, 0);

    GT3_setDate(&step, 1, 0, 0, 0, 0, 0);
    setDateIterator(&it, &initial, &step, CALTIME_GREGORIAN);
    while (cmpDateIterator(&it, &last) >= 0)
        nextDateIterator(&it);
    assert(it.count == 1);


    GT3_setDate(&step, 0, 2, 0, 0, 0, 0);
    setDateIterator(&it, &initial, &step, CALTIME_GREGORIAN);
    while (cmpDateIterator(&it, &last) >= 0)
        nextDateIterator(&it);
    assert(it.count == 6);


    GT3_setDate(&step, 0, 0, 1, 0, 0, 0);
    setDateIterator(&it, &initial, &step, CALTIME_GREGORIAN);
    while (cmpDateIterator(&it, &last) >= 0)
        nextDateIterator(&it);
    assert(it.count == 366);

    GT3_setDate(&step, 0, 0, 0, 0, 0, 1);
    setDateIterator(&it, &initial, &step, CALTIME_GREGORIAN);
    while (cmpDateIterator(&it, &last) >= 0)
        nextDateIterator(&it);
    assert(it.count == 366 * 24 * 3600);
}


int
main(int argc, char **argv)
{
    test1();
    return 0;
}
#endif /* TEST_MAIN */
