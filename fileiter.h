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

int iterate_chunk(GT3_File *fp, struct sequence *seq);
#endif
