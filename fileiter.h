#ifndef FILEITER__H
#define FILEITER__H

#include "gtool3.h"
#include "seq.h"

enum {
    ITER_CONTINUE,
    ITER_END,
    ITER_OUTRANGE,
    ITER_ERROR,
    ITER_ERRORCHUNK
};

struct file_iterator {
    GT3_File *fp;
    struct sequence *seq;
    unsigned flags_;            /* internal flags */
};

typedef struct file_iterator file_iterator;

void rewind_file_iterator(file_iterator *it);
void setup_file_iterator(file_iterator *it, GT3_File *fp,
                         struct sequence *seq);
int iterate_file(struct file_iterator *it);

#endif
