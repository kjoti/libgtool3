/*
 * range.h
 */
#ifndef RANGE__H
#define RANGE__H

#include "seq.h"

struct range {
    int str, end;
};

int get_range(struct range *range, const char *str, int low, int high);
int get_seq_or_range(struct range *range, struct sequence **seq,
                     const char *str, int low, int high);

#endif /* range.h */
