/*
 * ngtjoin.c -- Concatenate data along X, Y, Z axes.
 */
#include "internal.h"

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fileiter.h"
#include "logging.h"
#include "myutils.h"
#include "seq.h"

#define ALIVE_MAX (OPEN_MAX - 5)
#define RANGE_MAX 0x7fffffff

#define PROGNAME "ngtjoin"


/*
 * A set of input files.
 */
struct input_set {
    int num;
    GT3_File **fp;              /* lenght: num */
    int *offset;                /* length: num */
    unsigned keep_alive;

    GT3_Varbuf *vbuf;
};


struct buffer {
    int shape[3];
    size_t size, reserved_size;
    double *data;
};


/*
 * allocate new buffer and return it.
 */
static struct buffer *
new_buffer(void)
{
    struct buffer *rval;

    if ((rval = malloc(sizeof(struct buffer))) == NULL) {
        logging(LOG_SYSERR, NULL);
        return NULL;
    }

    rval->shape[0] = rval->shape[1] = rval->shape[2] = 0;
    rval->size = rval->reserved_size = 0;
    rval->data = NULL;
    return rval;
}


static int
resize_buffer(struct buffer *buf, int nx, int ny, int nz)
{
    size_t siz;
    double *data;

    assert(nx > 0 && ny > 0 && nz > 0);

    siz = nx * ny * nz;
    if (siz > buf->reserved_size) {
        if ((data = malloc(sizeof(double) * siz)) == NULL) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }
        free(buf->data);
        buf->data = data;
        buf->reserved_size = siz;
    }
    buf->shape[0] = nx;
    buf->shape[1] = ny;
    buf->shape[2] = nz;
    buf->size = siz;
    return 0;
}


static void
free_buffer(struct buffer *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->reserved_size = 0;
}


/*
 * get joined size.
 */
static void
get_joined_size(int *gsize, struct input_set *inset, const int *pattern)
{
    int rank, i, n, step[3];

    step[0] = 1;
    step[1] = pattern[0];
    step[2] = pattern[0] * pattern[1];

    for (rank = 0; rank < 3; rank++) {
        gsize[rank] = 0;
        for (i = 0; i < pattern[rank]; i++) {
            n = step[rank] * i;
            gsize[rank] += inset->fp[n]->dimlen[rank];
        }
    }
}


/*
 * update offset index in 'inset'.
 */
static int
update_offset_index(struct input_set *inset,
                    const int *gsize, const int *pattern)
{
    int n, pos[3];

    inset->offset[0] = pos[0] = pos[1] = pos[2] = 0;
    for (n = 1; n < inset->num; n++) {
        pos[0] += inset->fp[n-1]->dimlen[0];
        if (n % pattern[0] == 0) {
            pos[0] = 0;
            pos[1] += inset->fp[n-1]->dimlen[1];
        }
        if (n % (pattern[0] * pattern[1]) == 0) {
            pos[1] = 0;
            pos[2] += inset->fp[n-1]->dimlen[2];
        }
        inset->offset[n] = pos[0] + gsize[0] * (pos[1] + gsize[1] * pos[2]);
    }
    return 0;
}


/*
 * join() joins data in each of 'inset' into 'dest' buffer.
 */
static int
join_chunk(struct buffer *dest, struct input_set *inset, const int *pattern)
{
    int gsize[3];
    int n, y, z, offset;
    int ncopied;

    /*
     * resize destination area.
     */
    get_joined_size(gsize, inset, pattern);
    if (resize_buffer(dest, gsize[0], gsize[1], gsize[2]) < 0)
        return -1;

    /*
     * update offset index.
     */
    if (update_offset_index(inset, gsize, pattern) < 0)
        return -1;

    assert(inset->offset[inset->num - 1] < dest->size);

    for (n = 0; n < inset->num; n++) {
        /*
         * switch input and update varbuf.
         */
        if (!inset->keep_alive && n > 0 && GT3_resume(inset->fp[n]) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
        inset->vbuf = GT3_getVarbuf2(inset->vbuf, inset->fp[n]);
        if (inset->vbuf == NULL) {
            GT3_printErrorMessages(stderr);
            return -1;
        }

        /*
         * copy all elements.
         */
        for (z = 0; z < inset->fp[n]->dimlen[2]; z++) {
            if (GT3_readVarZ(inset->vbuf, z) < 0) {
                GT3_printErrorMessages(stderr);
                return -1;
            }
            for (y = 0; y < inset->fp[n]->dimlen[1]; y++) {
                offset = inset->offset[n]
                    + dest->shape[0] * (y + dest->shape[1] * z);

                ncopied = GT3_copyVarDouble(dest->data + offset,
                                            inset->fp[n]->dimlen[0],
                                            inset->vbuf,
                                            inset->fp[n]->dimlen[0] * y,
                                            1);
            }
        }

        /*
         * suspend input if needed.
         */
        if (!inset->keep_alive && n > 0 && GT3_suspend(inset->fp[n]) < 0) {
            GT3_printErrorMessages(stderr);
            return -1;
        }
    }
    return 0;
}


static void
free_input_set(struct input_set *inset)
{
    int i;

    GT3_freeVarbuf(inset->vbuf);
    for (i = 0; i < inset->num; i++)
        GT3_close(inset->fp[i]);

    if (inset->fp)
        free(inset->fp);
    if (inset->offset)
        free(inset->offset);
}


/*
 * allocate new (empty) input_set.
 */
static struct input_set *
new_input_set(void)
{
    struct input_set *inset;

    if ((inset = malloc(sizeof(struct input_set))) == NULL) {
        logging(LOG_SYSERR, NULL);
        return NULL;
    }
    inset->num = 0;
    inset->fp = NULL;
    inset->offset = NULL;
    inset->keep_alive = 1;
    inset->vbuf = NULL;
    return inset;
}


/*
 * get an input set from a list of pathname.
 */
static struct input_set *
make_input_set(char * const paths[], int ninputs)
{
    struct input_set *inset;
    int i;

    if ((inset = new_input_set()) == NULL)
        return NULL;

    if ((inset->fp = malloc(sizeof(GT3_File *) * ninputs)) == NULL
        || (inset->offset = malloc(sizeof(int) * ninputs)) == NULL) {
        logging(LOG_SYSERR, NULL);
        goto error;
    }

    /*
     * set members in input set.
     */
    inset->keep_alive = ninputs <= ALIVE_MAX;
    for (i = 0; i < ninputs; i++) {
        if ((inset->fp[i] = GT3_open(paths[i])) == NULL) {
            GT3_printErrorMessages(stderr);
            goto error;
        }
        inset->num++;

        /* Note: inset->fp[0] is never suspended. */
        if (!inset->keep_alive && i > 0 && GT3_suspend(inset->fp[i]) < 0) {
            GT3_printErrorMessages(stderr);
            goto error;
        }
    }

    if ((inset->vbuf = GT3_getVarbuf(inset->fp[0])) == NULL) {
        GT3_printErrorMessages(stderr);
        goto error;
    }
    return inset;

error:
    free_input_set(inset);
    return NULL;
}


/*
 * join main function.
 */
static int
join(FILE *output, struct input_set *inset,
     struct sequence *seq, const int *pattern, const char *fmt)
{
    GT3_HEADER head;
    struct buffer *wkbuf;
    char fmt_asis[17];
    file_iterator it;
    int n, rval, stat;

    assert(inset->num == pattern[0] * pattern[1] * pattern[2]);

    if ((wkbuf = new_buffer()) == NULL)
        return -1;

    setup_file_iterator(&it, inset->fp[0], seq);
    rval = -1;

    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        /*
         * seek to the same position to inset->fp[0].
         */
        for (n = 1; n < inset->num; n++) {
            if (!inset->keep_alive && GT3_resume(inset->fp[n]) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }

            if (GT3_seek(inset->fp[n], inset->fp[0]->curr, SEEK_SET) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }

            if (!inset->keep_alive && GT3_suspend(inset->fp[n]) < 0) {
                GT3_printErrorMessages(stderr);
                goto finish;
            }
        }

        /*
         * set gtool3 header.
         */
        if (GT3_readHeader(&head, inset->fp[0]) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
        if (pattern[0] > 1)
            GT3_setHeaderString(&head, "AITM1", "NUMBER1000");
        if (pattern[1] > 1)
            GT3_setHeaderString(&head, "AITM2", "NUMBER1000");
        if (pattern[2] > 1)
            GT3_setHeaderString(&head, "AITM3", "NUMBER1000");

        if (join_chunk(wkbuf, inset, pattern) < 0)
            goto finish;

        /*
         * get format name if ASIS.
         */
        if (fmt == NULL)
            GT3_copyHeaderItem(fmt_asis, sizeof fmt_asis, &head, "DFMT");

        if (GT3_write(wkbuf->data, GT3_TYPE_DOUBLE,
                      wkbuf->shape[0], wkbuf->shape[1], wkbuf->shape[2],
                      &head,
                      fmt ? fmt : fmt_asis,
                      output) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
    }
    rval = 0;

    if (pattern[0] > 1)
        logging(LOG_NOTICE, "AITM1 is renamed to NUMBER1000.");
    if (pattern[1] > 1)
        logging(LOG_NOTICE, "AITM2 is renamed to NUMBER1000.");
    if (pattern[2] > 1)
        logging(LOG_NOTICE, "AITM3 is renamed to NUMBER1000.");

finish:
    free_buffer(wkbuf);
    return rval;
}


static char *
toupper_string(char *str)
{
    char *p = str;

    while ((*p = toupper(*p)))
        p++;
    return str;
}


static void
usage(void)
{
    const char *message =
        "Usage:\n"
        "  " PROGNAME " [options] -x       File1 ... fileN\n"
        "  " PROGNAME " [options] -y       File1 ... fileN\n"
        "  " PROGNAME " [options] -z       File1 ... fileN\n"
        "  " PROGNAME " [options] -s IxJ   File1 ... fileN\n"
        "\n"
        "Options:\n"
        "    -f fmt    specify output format\n"
        "    -o PATH   specify output file\n"
        "    -t LIST   specify data No.\n"
        "    -v        verbose mode\n"
        "    -h        print help message\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", message);
}


int
main(int argc, char **argv)
{
    char *format = NULL;
    char *output_path = "gtool.out";
    FILE *output;
    struct input_set *inset;
    struct sequence *seq = NULL;
    int pattern[3], rval, ch;
    char dummy[17];
    int join_type = -1;
    enum {JOIN_XYZ, JOIN_X, JOIN_Y, JOIN_Z};

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "f:o:s:t:vxyzh")) != -1)
        switch (ch) {
        case 'f':
            format = strdup(optarg);
            toupper_string(format);
            if (GT3_output_format(dummy, format) < 0) {
                logging(LOG_ERR, "-f: Unknonw format (%s).", optarg);
                exit(1);
            }
            break;

        case 'o':
            output_path = optarg;
            break;

        case 's':
            pattern[0] = pattern[1] = 0;
            if (get_ints(pattern, 2, optarg, 'x') != 2
                || pattern[0] < 1 || pattern[1] < 1) {
                logging(LOG_ERR, "-s: invalid argument (%s).", optarg);
                exit(1);
            }
            join_type = JOIN_XYZ;
            break;

        case 't':
            if ((seq = initSeq(optarg, 1, RANGE_MAX)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;

        case 'v':
            set_logging_level("verbose");
            break;

        case 'x':
            join_type = JOIN_X;
            break;
        case 'y':
            join_type = JOIN_Y;
            break;
        case 'z':
            join_type = JOIN_Z;
            break;
        case 'h':
        default:
            usage();
            exit(1);
        }

    argc -= optind;
    argv += optind;
    if (argc <= 0) {
        logging(LOG_ERR, "No input file.");
        exit(1);
    }
    if ((inset = make_input_set(argv, argc)) == NULL)
        exit(1);

    /*
     * set join pattern.
     */
    switch (join_type) {
    case JOIN_XYZ:
        pattern[2] = inset->num / (pattern[0] * pattern[1]);
        if (inset->num != pattern[0] * pattern[1] * pattern[2]) {
            logging(LOG_ERR, "# of inputs: %d", inset->num);
            logging(LOG_ERR, "# of inputs must be a multiple of %d.",
                    pattern[0] * pattern[1]);
            exit(1);
        }
        break;
    case JOIN_X:
        pattern[0] = inset->num;
        pattern[1] = pattern[2] = 1;
        break;
    case JOIN_Y:
        pattern[1] = inset->num;
        pattern[2] = pattern[0] = 1;
        break;
    case JOIN_Z:
        pattern[2] = inset->num;
        pattern[0] = pattern[1] = 1;
        break;
    default:
        logging(LOG_ERR, "One of -x, -y, -z, -s should be specified.");
        usage();
        exit(1);
        break;
    }

    /*
     * main process: join & output.
     */
    if ((output = fopen(output_path, "wb")) == NULL) {
        logging(LOG_SYSERR, output_path);
        exit(1);
    }
    rval = join(output, inset, seq, pattern, format);
    fclose(output);

    free_input_set(inset);
    return rval < 0 ? 1 : 0;
}
