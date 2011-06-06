/*
 * range.c
 */
#include <stdlib.h>
#include <string.h>

#include "range.h"

#include "logging.h"
#include "myutils.h"
#include "seq.h"


struct range *
dup_range(const struct range *range)
{
    struct range *ptr;

    if ((ptr = malloc(sizeof(struct range)))) {
        ptr->str = range->str;
        ptr->end = range->end;
    }
    return ptr;
}


/*
 * XXX: get_range() transforms the range from 1-offset to 0-offset.
 *
 *  2:5 == 2, 3, 4, 5 (1-offset)
 *      => 1, 2, 3, 4 (0-offset)
 *         == [1, 5)
 *         == r.str = 1, r.end = 5
 *
 *  str    => range
 *  ------------------
 *  "1:10" => r.str = 0, r.end = 10, 0 <= x < 10
 *  ":"    => r.str = low - 1, r.end = high
 */
int
get_range(struct range *range, const char *str, int low, int high)
{
    int vals[2];
    int num;

    vals[0] = low;
    vals[1] = high;
    if (str == NULL || (num = get_ints(vals, 2, str, ':')) < 0)
        return -1;

    range->str = vals[0] - 1;
    range->end = vals[1];

    if (num == 1)
        range->end = range->str + 1;
    return 0;
}


/*
 * convert a sequence to a range if possible.
 *
 * return value:
 *   -1: unexpected error
 *    0: not converted.
 *    1: converted.
 *
 * seq: 1-offset => range: 0-offset.
 */
static int
conv_seq_to_range(struct range *range, struct sequence *seq)
{
    int rval;
    int first, prev, in_order;
    unsigned flag = 0;

    first = 1;
    prev = 0;
    in_order = 1;
    for (;;) {
        rval = nextToken(seq);
        if (rval < 0) {
            logging(LOG_ERR, "invalid sequence");
            return -1;
        }
        if (rval == 0)
            break;

        if (seq->step < 0 || seq->step > 1
            || (flag > 0 && seq->curr != prev + 1)) {
            in_order = 0;
            break;
        }
        if (flag == 0)
            first = seq->head;
        prev = seq->tail;
        flag = 1;
    }
    if (in_order) {
        range->str = first - 1;
        range->end = prev;
    }
    return in_order;
}


/*
 * return value:
 *    < 0: error
 *      0: use "struct sequence" as output.
 *      1: use "struct range" as output.
 */
int
get_seq_or_range(struct range *range, struct sequence **seq,
                 const char *str, int low, int high)
{
    struct sequence *temp;
    struct range r;
    int use_range;

    if (str == NULL || range == NULL || seq == NULL)
        return -1;

    if ((temp = initSeq(str, low, high)) == NULL) {
        logging(LOG_SYSERR, NULL);
        return -1;
    }
    use_range = conv_seq_to_range(&r, temp);
    if (use_range < 0)
        return -1;

    if (use_range)
        *range = r;
    else {
        rewindSeq(temp);
        *seq = temp;
    }
    return use_range;
}


#ifdef TEST_MAIN
#include <assert.h>

void
test1(void)
{
    int rval;
    struct range r = {0, 0};

    rval = get_range(&r, ":", 1, 0x7fffffff);
    assert(rval == 0);
    assert(r.str == 0 && r.end == 0x7fffffff);

    rval = get_range(&r, "5:", 1, 0x7fffffff);
    assert(rval == 0);
    assert(r.str == 4 && r.end == 0x7fffffff);

    rval = get_range(&r, ":10", 1, 0x7fffffff);
    assert(rval == 0);
    assert(r.str == 0 && r.end == 10);

    rval = get_range(&r, "4:6", 1, 0x7fffffff);
    assert(rval == 0);
    assert(r.str == 3 && r.end == 6);

    rval = get_range(&r, "10", 1, 0x7fffffff);
    assert(rval == 0);
    assert(r.str == 9 && r.end == 10);
}


void
test2(void)
{
    int rval;
    struct range r = {0, 0};
    struct sequence *seq;

    seq = initSeq("1:10", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 0 && r.end == 10);

    freeSeq(seq);
    seq = initSeq("11:20,21:30", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 10 && r.end == 30);

    freeSeq(seq);
    seq = initSeq("11:20,22:30", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 0);

    freeSeq(seq);
    seq = initSeq("5,6,7,8,9,10", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 4 && r.end == 10);

    freeSeq(seq);
    seq = initSeq("5,6,7,8,9,10,12", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 0);

    freeSeq(seq);
    seq = initSeq(":", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 0 && r.end == 0x7fffffff);

    freeSeq(seq);
    seq = initSeq("10:", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 9 && r.end == 0x7fffffff);

    freeSeq(seq);
    seq = initSeq(":10", 1, 0x7fffffff);
    rval = conv_seq_to_range(&r, seq);
    assert(rval == 1);
    assert(r.str == 0 && r.end == 10);
}


void
test3(void)
{
    int rval;
    struct range r;
    struct sequence *seq;

    rval = get_seq_or_range(&r, &seq, ":", 1, 10);
    assert(rval == 1);
    assert(r.str == 0 && r.end == 10);

    rval = get_seq_or_range(&r, &seq, "3:8", 1, 10);
    assert(rval == 1);
    assert(r.str == 2 && r.end == 8);

    rval = get_seq_or_range(&r, &seq, ":8", 1, 10);
    assert(rval == 1);
    assert(r.str == 0 && r.end == 8);

    rval = get_seq_or_range(&r, &seq, "6:", 1, 10);
    assert(rval == 1);
    assert(r.str == 5 && r.end == 10);
}


int
main(int argc, char **argv)
{
    test1();
    test2();
    test3();
    return 0;
}
#endif
