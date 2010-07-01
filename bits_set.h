/*
 * bits_set.h
 */
#ifndef BITS_SET__H
#define BITS_SET__H

#include "internal.h"
#include <sys/types.h>

struct bits_set {
    uint32_t *set;
    size_t size;
};

/*
 * bits_set manipulation (using uint32_t)
 */
#define BS_SET(bs, x)  { (bs).set[(x) >> 5] |=  (1U << ((x) & 31));  }
#define BS_CLS(bs, x)  { (bs).set[(x) >> 5] &= ~(1U << ((x) & 31));  }
#define BS_TEST(bs, x) ( (bs).set[(x) >> 5] &   (1U << ((x) & 31))   )

#define BS_SETALL(bs) {                    \
     int i_;                               \
     for (i_ = 0; i_ < (bs).size; i_++)    \
         (bs).set[i_] = 0xffffffffU;       \
}

#define BS_CLSALL(bs) {                    \
     int i_;                               \
     for (i_ = 0; i_ < (bs).size; i_++)    \
         (bs).set[i_] = 0;                 \
}

typedef struct bits_set bits_set;

int resize_bits_set(bits_set *bs, unsigned nbits);
void free_bits_set(bits_set *bs);
#endif /* !BITS_SET__H */
