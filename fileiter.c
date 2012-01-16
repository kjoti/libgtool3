#include "internal.h"

#include <sys/types.h>
#include <stdio.h>
#include "gtool3.h"
#include "fileiter.h"


void
setup_file_iterator(file_iterator *it, GT3_File *fp, struct sequence *seq)
{
    it->fp = fp;
    it->seq = seq;
    it->flags_ = 0;
    if (it->seq && fp->num_chunk > 0)
        reinitSeq(it->seq, 1, fp->num_chunk);
}


void
rewind_file_iterator(file_iterator *it)
{
    it->flags_ = 0;
    if (it->seq == NULL)
        GT3_rewind(it->fp);
    else
        rewindSeq(it->seq);
}


/*
 * step a forward in the iteration.
 */
int
iterate_file(file_iterator *it)
{
    int stat, err;
    int next;
    int rval;

    if (it->seq == NULL) {
        if (it->flags_ && GT3_next(it->fp) < 0) {
            GT3_printErrorMessages(stderr);
            return ITER_ERRORCHUNK;
        }
        if (GT3_eof(it->fp))
            return ITER_END;

        it->flags_ = 1;
        return ITER_CONTINUE;
    }

    /* using sequence. */
    stat = nextSeq(it->seq);
    if (stat < 0)
        return ITER_ERROR;
    if (stat == 0)
        return ITER_END;

    if (it->seq->curr < 0)
        next = it->seq->curr + GT3_getNumChunk(it->fp);
    else if (it->seq->curr == 0)
        return ITER_OUTRANGE;
    else
        next = it->seq->curr - 1;

    rval = ITER_CONTINUE;
    if (it->seq->step != 0) {
        int nskips = 0;

        if (next < 0) {
            if (it->seq->step < 0)
                it->seq->tail = it->seq->curr;

            nskips = -next / it->seq->step - 1;
            rval = ITER_OUTRANGE;
        }

        if (it->fp->num_chunk > 0 && next >= it->fp->num_chunk) {
            nskips = (it->fp->num_chunk - next) / it->seq->step - 1;
            rval = ITER_OUTRANGE;
        }

        /* skip meaningless iteration. */
        if (nskips > 0)
            it->seq->curr += nskips * it->seq->step;

        if (rval == ITER_OUTRANGE)
            return rval;
    }

    stat = GT3_seek(it->fp, next, SEEK_SET);
    if (GT3_eof(it->fp)) {
        it->seq->last = GT3_getNumChunk(it->fp);
        if (it->seq->step > 0)
            it->seq->tail = it->seq->last;

        rval = ITER_OUTRANGE;
    }

    if (stat < 0) {
        err = GT3_getLastError();
        if (err == GT3_ERR_INDEX) {
            GT3_clearLastError();
            return ITER_OUTRANGE;
        }
        GT3_printErrorMessages(stderr);
        rval = ITER_ERRORCHUNK;
    }
    return rval;
}
