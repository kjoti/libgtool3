#include <sys/types.h>
#include <stdio.h>
#include "gtool3.h"
#include "fileiter.h"


int
iterate_chunk(GT3_File *fp, struct sequence *seq)
{
	int stat;

	stat = nextSeq(seq);
	if (stat < 0)
		return ITER_ERROR;
	if (stat == 0)
		return ITER_END;

	if (seq->curr < 0) {
		stat = GT3_seek(fp, seq->curr, SEEK_END);
		seq->last = fp->num_chunk;
		if (seq->step > 0)
			seq->tail = fp->num_chunk;
	} else {
		stat = GT3_seek(fp, seq->curr - 1, SEEK_SET);
		if (GT3_eof(fp)) {
			seq->last = fp->num_chunk;
			if (seq->step > 0)
				seq->tail = fp->num_chunk;
			return ITER_OUTRANGE;
		}
	}

	if (stat < 0) {
		if (GT3_getLastError() != GT3_ERR_INDEX) {
			GT3_printLastErrorMessage(stderr);
			return ITER_ERRORCHUNK;
		}
		return ITER_OUTRANGE;
	}

	return ITER_CONTINUE;
}