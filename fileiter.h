#ifndef FILEITER__H
#define FILEITER__H

#include "gtool3.h"
#include "seq.h"

enum {
	ITER_END,
	ITER_CONTINUE,
	ITER_ERROR,
	ITER_ERRORCHUNK,
	ITER_OUTRANGE
};

int iterate_chunk(GT3_File *fp, struct sequence *seq);
#endif
