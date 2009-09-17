/*
 *  find_minmax.c
 */
#include <stdlib.h>
#include "find_minmax.h"

#define find_max_any(T) \
int find_max_##T(const T *values, int nelems, const T * miss) \
{ \
    int i, idx = -1; \
    T x; \
    for (i = 0; i < nelems; i++) { \
        x = values[i]; \
        if ((miss == NULL || x != *miss) && (idx == -1 || x > values[idx])) \
            idx = i; \
    } \
    return idx; \
}

#define find_min_any(T) \
int find_min_##T(const T *values, int nelems, const T * miss) \
{ \
    int i, idx = -1; \
    T x; \
    for (i = 0; i < nelems; i++) { \
        x = values[i]; \
        if ((miss == NULL || x != *miss) && (idx == -1 || x < values[idx])) \
            idx = i; \
    } \
    return idx; \
}


find_max_any(int)
find_max_any(unsigned)
find_max_any(float)
find_max_any(double)

find_min_any(int)
find_min_any(unsigned)
find_min_any(float)
find_min_any(double)


#ifdef TEST_MAIN
#include <assert.h>

void
test1(void)
{
    unsigned v[] = {0U, 0x7fffffffU, 0x80000000U, 0xffffffffU};
    int nelems = sizeof v / sizeof(unsigned);
    int *w = (int *)v;
    int idx;

    idx = find_max_unsigned(v, nelems, NULL);
    assert(idx == 3);

    idx = find_min_unsigned(v, nelems, NULL);
    assert(idx == 0);

    idx = find_max_int(w, nelems, NULL);
    assert(idx == 1);

    idx = find_min_int(w, nelems, NULL);
    assert(idx == 2);
}

void
test2(void)
{
    float v[] = {-999.f, -999.f, -999.f, -999.f, -999.f};
    float miss = -999.f;
    int nelems = sizeof v / sizeof(float);
    int idx;
    
    idx = find_max_float(v, nelems, &miss);
    assert(idx == -1);

    v[4] = 0.f;
    idx = find_max_float(v, nelems, &miss);
    assert(idx == 4);

    v[3] = -1.f;
    v[2] = 2.f;
    idx = find_max_float(v, nelems, &miss);
    assert(idx == 2);

    idx = find_min_float(v, nelems, &miss);
    assert(idx == 3);

    idx = find_min_float(v, nelems, NULL);
    assert(idx == 0);
}


int
main(int argc, char **argv)
{
    test1();
    test2();
    return 0;
}
#endif /* TEST_MAIN */
