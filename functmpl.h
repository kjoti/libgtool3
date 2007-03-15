/*
 *  functmpl.h
 */
#include <math.h>


#define FUNCTMPL_MINVAL(RTYPE,TYPE, NAME)       \
RTYPE                                           \
NAME(const void *ptr, int len)                  \
{                                               \
    const TYPE *data = (const TYPE *)ptr;       \
    TYPE val = HUGE_VAL;                        \
    int i;                                      \
                                                \
    for (i = 0; i < len; i++)                   \
        if (data[i] < val)                      \
            val = data[i];                      \
                                                \
    return (RTYPE)val;                          \
}

#define FUNCTMPL_MAXVAL(RTYPE,TYPE, NAME)       \
RTYPE                                           \
NAME(const void *ptr, int len)                  \
{                                               \
    const TYPE *data = (const TYPE *)ptr;       \
    TYPE val = -HUGE_VAL;                       \
    int i;                                      \
                                                \
    for (i = 0; i < len; i++)                   \
        if (data[i] > val)                      \
            val = data[i];                      \
                                                \
    return (RTYPE)val;                          \
}

#define FUNCTMPL_SUM(RTYPE,TYPE, NAME)          \
RTYPE                                           \
NAME(const void *ptr, int len)                  \
{                                               \
    const TYPE *data = (const TYPE *)ptr;       \
    RTYPE sum = .0;                             \
    int i;                                      \
                                                \
    for (i = 0; i < len; i++)                   \
        sum += data[i];                         \
                                                \
    return sum;                                 \
}


#define FUNCTMPL_AVR(RTYPE, TYPE, NAME)         \
int                                             \
NAME(RTYPE *avr, const void *ptr, int len)      \
{                                               \
    const TYPE *data = (const TYPE *)ptr;       \
    RTYPE sum = .0;                             \
    int i;                                      \
                                                \
    if (len <= 0)                               \
        return -1;                              \
                                                \
    for (i = 0; i < len; i++)                   \
        sum += data[i];                         \
                                                \
    *avr = sum / len;                           \
    return 0;                                   \
}

#define FUNCTMPL_SDEVIATION(RTYPE,TYPE, NAME)           \
int                                                     \
NAME(RTYPE *sd, const void *ptr, double avr, int len)   \
{                                                       \
    const TYPE *data = (const TYPE *)ptr;               \
    double var = .0;                                    \
    int i;                                              \
                                                        \
    if (len <= 0)                                       \
        return -1;                                      \
                                                        \
    for (i = 0; i < len; i++)                           \
        var += (data[i] - avr) * (data[i] - avr);       \
                                                        \
    *sd = sqrt(var / len);                              \
    return 0;                                           \
}
